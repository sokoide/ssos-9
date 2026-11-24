# リンカ契約 (Phase 4)

更新日: 2026-11-12

SSOS-9 上で動作するリンカ `ld` (S9M1 アプリ) の CLI、リンクの意味論、出力形式、診断、検証の契約。入力は Phase 3 の S9O1 オブジェクト ([アセンブラ契約](assembler-contract.md)) で、出力は既存 loader が検証・起動できる S9M1 イメージ ([ローダー計画](multiprocess-loader-plan.md) 3.2) とする。既存 loader・validator・process 側の変更は不要で、必要な拡張は CLI の module 登録経路 1 点のみ (「既存 loader との整合と必要な拡張」参照)。

## 対象と上限

- 入力は FAT16 上の 8.3 名 S9O1 ファイル。1 個あたり 65535 byte まで (S9O1 契約)。入力は 1～14 個 (`run` の argv 上限 16 から `ld` と出力名を除いた数)。CLI 入力行は 63 byte なので実用上はさらに短い。
- 結合後の text は 32768 byte まで (S9M1 の code 上限)。初期化 data は 12288 byte、bss は 65536 − 初期化 data まで (S9M1 の data 上限)。シンボルは全入力合計 512 件、名前プール 6144 byte、fixup は 256 件まで。超過時は「領域超過」エラーで停止する。
- 出力 image は 65536 byte 以下に収まる (`ss9_file_store` の上限)。上限内であれば常に 80 + text(4 byte 倍数) + data + 名前 + fixup で最大 48240 byte。
- 出力 module の stack size は 4096 byte 固定 (validator の最小値)。S9O1 に stack 要求を運ぶ field はない。
- entry は結合 text の先頭 (offset 0)、すなわち 1 番目の入力オブジェクトの text[0] に固定する。1 番目の入力は start コードを含むオブジェクトとし、text が空なら「text が空」エラーにする。
- module 名は出力ファイル名の拡張子を除いた base から取る。validator の名前規則 ([A-Za-z0-9_.-]、先頭末尾の `.` なし) を検査する。

## CLI

```text
run ld OUTPUT IN1 [IN2 ...]
```

- `OUTPUT` は書き込む S9M1 ファイル名 (8.3、慣例で拡張子 `.S9M`)。`IN1..` は S9O1 オブジェクト。
- `OUTPUT` が既存なら置き換え、なければ作成する (asm と同じ open_read プローブ → `SS9_FILE_REPLACE`/`SS9_FILE_CREATE`)。`OUTPUT` が入力名と (8.3 の大小文字を同一視して) 重複する場合は引数エラーにする。
- 成功時は `Wrote OUTPUT: N bytes, text T, data D, bss B, fixups F` を stdout に出し、終了コード 0 で終わる。
- エラー時は `ld: メッセージ` または `FILE: メッセージ` を出し、最初のエラーで停止して終了コード 1 (使い方の誤りは 2)。出力ファイルは書かない (image を全体完成させてから 1 回の `ss9_file_store` で書くため、中途半端な出力ファイルは残らない)。

## リンクの意味論

### セクション配置

- 結合順は入力順で固定する。text は全入力の text を入力順に連結し、前の入力の text 長が奇数のときは次の入力の前に 1 byte の 0x00 を入れて偶数番地を保つ (MC68000 の命令は偶数番地から始まるため。GAS の `.text` align 2 と同じ観察結果になる)。data と bss はパディングなしで連結する。
- 各入力の text/data/bss の出力内 base は、この連結順から確定する。出力内の text は 0 から、private data は 0 から数える。
- private data の配置は `[全入力の data 連結][全入力の bss 連結]` とする。A5 = private data 先頭であり、シンボルの A5 変位 = private data 内オフセットである。

### シンボル解決

- シンボルは入力ごとに独立した名前空間を持つ (ローカルは他オブジェクトから見えない)。flags bit0 (グローバル) の定義だけが他オブジェクトから参照できる。
- 同名のグローバル定義が 2 個以上あるとき「シンボル二重定義」エラー。flags bit1 (extern) は、同名のグローバル定義がどれかの入力にあればそれに解決し、定義が同じオブジェクト内にあればその定義を優先する。どこにも定義がなければ「未解決シンボル」エラー (名前を表示)。
- reloc は自分のオブジェクトのシンボル表を index で参照する。リンカは全シンボルを入力順に 1 つの表に載せるので、解決は (オブジェクト番号, index) の算術で行う。
- セクションアンカー `\001text` / `\001data` / `\001bss` は特別扱いしない。そのオブジェクトの該当セクション、value 0 のローカルシンボルとして解決され、出力内ではそのオブジェクトのセクション base になる。
- セクション 0 (abs) のシンボル (`.equ`) は値を絶対定数として解決する。PC16 の対象になる場合は「PIC 違反」エラーにする (実行時アドレスに依存するため)。
- オブジェクトの既定値チェックは [s9ob](../tools/s9ob.py) と同じく、magic/version/flags=0/entry=0/予約 0/ファイル長一致/シンボル flags≤0x07/セクション値/reloc の section∈{1,2} と type∈1..4/offset 範囲/symbol index 範囲を検査し、違反は「形式」エラーにする。

### 再配置の適用

リンカは field の既定値を信頼せず、常に `S + A − P` (PC16) または `S + A` (ABS) を再計算して書く。`S` は解決後のシンボルの出力内値、`A` は reloc の addend、`P` は field の出力内 text オフセット (PC16 の拡張語アドレス)。適用規則を種類 × 配置セクション × 対象セクションで示す。

| reloc (配置) | 対象 | 動作 |
| --- | --- | --- |
| PC16 (text) | text | 出力内で折り畳む。値 = S + A − P、範囲 −32768..32767。同一オブジェクト内は既定値と一致する (連結で相対距離不変) |
| PC16 (text) | data/bss | 「PIC違反」エラー (code と private data の実行時距離は spawn ごとに変わる) |
| PC16 (text) | abs | 「PIC違反」エラー |
| PC16 (data) | すべて | 「PIC違反」エラー |
| ABS32 (text) | すべて | 「PIC違反」エラー (fixup は code を書き換えられない) |
| ABS16 (text) | data/bss | A5 変位として解決する。値 = private data 内オフセット、範囲 −32768..32767 (「A5 / private data 契約」参照) |
| ABS16 (text) | abs | 定数として折り畳む。範囲 −32768..65535 (asm の `.word` と同じ規則) |
| ABS16 (text) | text | 「PIC違反」エラー |
| ABS8 (text) | abs | 定数として折り畳む。範囲 −128..255 (asm の `.byte` と同じ規則) |
| ABS8 (text) | text/data/bss | 「PIC違反」エラー (8 bit の A5 変位命令は存在しない) |
| ABS32 (data) | abs | 定数として折り畳む (fixup 不要) |
| ABS32 (data) | text | CODE32 fixup を 1 件生成する (payload は 80 + S + A) |
| ABS32 (data) | data/bss | DATA32 fixup を 1 件生成する (payload は S + A) |
| ABS16/ABS8 (data) | text/data/bss | エラー (fixup は 4 byte 整列 slot のみ。`.dc.l` で書く) |
| ABS16/ABS8 (data) | abs | 定数として折り畳む |

- fixup の対象 slot は private data 内オフセットで、4 byte 整列と init data 領域内 (bss は image バイトを持たない) を要求する。非整列は「整列」エラー、同一 slot への重複は「二重 fixup」エラー (validator も重複を拒否する)。
- ABS16 (text) → data/bss の解釈は文脈を区別できない (`x(%a5)` の変位も `.dc.w x` も同じ ABS16 になる)。リンカはこれを常に「A5 変位」と読む。text 内での `.dc.w x` / 絶対 `.w` 参照は意味をなさないため、ソース側で書かないこと (「既知の限界」参照)。

### A5 / private data 契約

出力 S9M1 は `got_offset = 0`、`got_size = 4` とし、A5 = private data 先頭を約束する。GOT slot は使わないダミーであり (validator が got_size > 0 を要求するための 4 byte)、data+bss 合計が 4 byte 未満のときは初期化 data を 4 byte まで 0 埋めする。

リンク済みモジュールの entry で保証される初期状態 (既存 spawn frame の通り):

| レジスタ | 値 |
| --- | --- |
| D0 | argc |
| A0 | argv (32 bit ポインタ配列、argv[argc] = 0) |
| A3 | image base |
| A5 | private data 先頭 (= data_base + got_offset = data_base) |
| A7 | private stack (下降、4 byte 整列の先頭) |
| A6 | 0 |

ソース側の書き方 (PIC 制約のもとで link 可能な形):

- 変数の読み書き: `move.l x(%a5),%d0` (ABS16 → A5 変位に解決)
- 変数のアドレス: `lea x(%a5),%a0`
- 呼び出し: `bsr func` または `jsr func(%pc)` (PC16)。`jsr func` (絶対) は ABS32 になりエラー
- text 内のアドレス: `lea func(%pc),%a0`
- data 内のポインタ初期値: `.even` を置いて `.dc.l x` / `.dc.l func` (fixup 生成)。16 bit ポインタは fixup できない
- 終了: `moveq #4,%d0` + `move.l status,%d1` + `trap #0` (SS9_SYS_EXIT)。entry からの `rts` は ABI に含まない

A5 変位は d16 の範囲 (符号付き 16 bit) に収まる必要があるため、A5 でアクセスできるのは private data の先頭 32768 byte までである。超えるシンボルを A5 参照すると「範囲」エラーになる (bss 自体は 65536 − data まで持てるが、届かない領域のシンボルは参照できない)。

## 出力形式

既存形式 S9M1 v1 をそのまま書く。新形式は作らない。リンカが書く header の field 値:

| field | 値 |
| --- | --- |
| magic / ABI version / header size | `S9M1` / 1 / 80 |
| flags | PIC + REENTRANT (validator の必須値) |
| image size | 80 + code_size + init_size + name_size + fixup_size |
| code offset / size | 80 / text 合計を 4 byte 倍数へ 0x00 パディングした値 |
| entry offset | 80 (結合 text の offset 0) |
| data offset / init size | 80 + code_size / max(4, data 合計) |
| bss size | bss 合計 |
| stack size | 4096 |
| name offset / size | data_offset + init_size / module 名の長さ |
| GOT offset / size | 0 / 4 (ダミー) |
| fixup offset / count | name_offset + name_size / 生成した fixup 件数 |
| CRC32 | image 全体 (CRC field を 0 とした値)。IEEE CRC32、反転多項式 0xEDB88320、初期値/最終 XOR 0xFFFFFFFF |

fixup record は 12 byte big endian の `target / kind / value_offset` (ローダー計画 3.4.3)。`target` は private data 内 slot オフセット、`kind` は CODE32=1 / DATA32=2、`value_offset` は CODE32 なら `80 + text 内オフセット`、DATA32 なら private data 内オフセット。spawn 時に既存 `ss_module_prepare_data` が private copy へ適用し、code は書き換えられない。

初期化 data 領域の内容は全入力の data 連結 (4 byte 未満のときは 0 埋めで 4 byte にする)。

## 診断とエラー

エラー種別は固定文字列で、asm と同じ最初のエラーで停止する方式。検査順は (1) 引数、(2) 入力オブジェクトの形式 (1 個ずつ)、(3) シンボル解決 (全入力の読み込み後)、(4) 再配置適用、(5) 上限、(6) 出力書き込み。各入力オブジェクトの走査内では header 形式 → その入力分の上限 → シンボル → reloc 形式 → 名前プール → 二重定義の順に検査する (上限はシンボル表を構築する前に確定させる必要があるため、入力単位で前倒しする。全体としては「形式 → シンボル → 再配置適用」の順序を保つ)。メッセージのカテゴリ:

| 種別 | 例 |
| --- | --- |
| 引数 | `Usage: run ld OUTPUT IN1 [IN2 ...]`、出力名と入力名の重複、不正な module 名 |
| 形式 | magic/version/長さ不一致、不正なシンボル・reloc field、予約値非 0 |
| 未解決シンボル | `Undefined symbol NAME` |
| シンボル二重定義 | `Duplicate symbol NAME` |
| PIC違反 | reloc の種類・配置・オフセットと対象セクションを示す (「再配置の適用」の表の各行) |
| 整列 / 二重 fixup | fixup slot の 4 byte 整列違反、重複 target |
| 範囲 | PC16・A5 変位・折り畳み値の範囲外 |
| 領域超過 | text/data/シンボル/名前プール/fixup/入力数/出力 image サイズ |
| 入出力 | 入力を開けない・読めない、出力の store 失敗 |

## メモリとスタック予算

リンカ自身も asm と同じ S9M1 制約 (静的領域なし、全状態スタック、.text ≤ 32768、libgcc 非依存) で作る。stack 65532 byte を静的分割する。オブジェクトは「逐次読み」で、text/data バイトを入力間で保持しない。

| 領域 | byte |
| --- | --- |
| 出力 image 構造体 (header 80 + code 32768 + data 12288 + name 32 + fixup 12×256) | 48240 |
| シンボル表 (12 byte × 512: value, 名前プール offset, section, flags, オブジェクト番号) | 6144 |
| 名前プール | 6144 |
| オブジェクト台帳 (14 件 × base/サイズ/symbol base) | 約 400 |
| 読み込みチャンクバッファと作業状態 | 約 1000 |
| 合計 | 約 61900 |

読み込みは 2 パスで、入力ごとに open → 閉じるを繰り返す (seek 未実装のため text/data は読み捨てる):

1. パス 1 (走査): header 検査 → text/data を読み捨て → シンボル表を読む。シンボル表構築、セクション base 確定、二重定義と未解決の検査。reloc は読まない (fixup 件数は header に書く必要があるが、出力はメモリ上で完成させてから書くため header は最後に埋める)。
2. パス 2 (適用): header 再検査 → header のサイズ各 field をパス 1 の台帳と突合し、1 つでも相違があれば「パス間変更」エラー (入力ファイルが 2 パスの間に書き換わった場合の防御。相違したままコピーすると出力バッファを超過する) → text を出力 code 領域へコピー → data を出力 data 領域へコピー → シンボルを読み捨て → reloc を読んでその場で出力バッファへパッチ、data の ABS32 から fixup を生成。
3. 仕上げ: code を 4 byte 倍数へパディング、header 埋め、CRC32 計算、`ss9_file_store`。

ストレージ契約により「書き込み記述子が open の間は別の open が拒否される」ため、出力は入力をすべて閉じた後の `ss9_file_store` 1 回で行う (記述子を使わない)。fd による分割書き込みは使わない。

## 既存 loader との整合と必要な拡張

- validator (`ssos/os/module/image.c`) は GOT・fixup 付き S9M1 を既に検査でき、spawn (`ssos/os/process/process.c`) は `A5 = data_base + got_offset`、init data copy、bss clear、fixup 適用を実装済み。リンク済み module は loader 無変更で起動・終了 (`exit` syscall)・回収 (`wait`/reaper) できる。`mkmodule.py` が拒否しているのは code-only プロファイルであって、形式の限界ではない。
- 必要な拡張は CLI 1 点: 現行の `run` は registry と builtin しか解決せず、FAT16 上の S9M1 を registry に登録する経路がない。`run NAME ...` で registry/builtin に見つからない場合、FAT16 マウント済みなら root から `NAME` (8.3 大小同一視) を探し、`ss_storage_open_reader` + `ss_module_load_reader` (サイズは事前に `SS_MODULE_MAX_IMAGE_SIZE` 以下を検査) で読んで DYNAMIC 登録してから spawn する。IOCS lock は既存 `load` コマンドと同じ区間で取る。登録済み image は `modules`/`unload` で観察・解放できる (registry slot 8 の占有は既存契約どおり)。
- syscall ABI・S9M1 形式 version は現行のままで、読み替え・拡張をしない。将来 S9O1 の後継 (S9O2) が必要になった場合は別契約とし、version で拒否する。

## 実装構成

```text
apps/link/
  link.h      上限、構造体、状態コード、API (asm.h と同じスタイル)
  readobj.c   S9O1 の逐次読みと形式検査 (header/シンボル/reloc)
  layout.c    パス 1: シンボル表、セクション base、二重定義・未解決
  fold.c      パス 2: コピーと再配置適用、fixup 生成、PIC 診断
  image.c     出力 image 構造体、header、CRC32
  main.c      S9M1 CLI (引数、fd 順序、メッセージ、ss9_file_store)
  ../support.c memcpy と 32 bit 乗除算 (apps/asm と単一ソースを共有)
  Makefile    apps/asm と同じ構成。mkmodule --name ld --stack-size 65532
tests/unit/
  link_host_main.c        ホスト CLI driver (stdio 版 LinkIo 束縛)
tests/link_golden.py       ゴールデン比較 driver (クロス比較 + 自前 mini-linker)
tests/asm/link/
  start.s/main.s/obj.s     t14 と self-vector 用サンプル (A5 変位、跨ぎ PC16、fixup)
  badmain.s                未解決シンボルの負例
  golden/gcross/*.s        m68k-xelf-ld クロス比較用 (限定 profile)
tests/qemu/pre/
  t14_link_run.c           on-target 統合試験 (asm → ld → 実行)
```

ビルドは `apps/asm` と同じ profile (`-Os -fpic -mpcrel -ffixed-a3 -fno-jump-tables`、`examples/hello/module.ld`) で、`.text` が 32768 以下であることを mkmodule が検査する。asm (29307 byte) より小さい見込みだが、超えた場合は分割または -Os の見直しをする。

## 検証

- ホスト driver (`make -C tests test-link-host`): `tests/unit/link_host_main.c` が stdio 版 LinkIo を束縛し、ターゲットと同じ CLI で `link-host OUTPUT IN1 [IN2...]` として動く。単体試験 (`test_asm_*` に相当する手組みベクトル) の代わりに、次のゴールデンが検査を担う。
- ゴールデン (`make -C tests test-link-golden`、`tests/link_golden.py`): 2 レベル。(1) クロス比較: `tests/asm/link/golden/gcross` を `link-host` と `m68k-xelf-ld` の両方でリンクし text バイト一致を要求する。比較可能なのは限定 profile (text 連結と PC16 折り畳み、定数 data、全オブジェクトの text 長が 4 の倍数 — GNU ld は .text を 4 整列する) のみ。(2) self-vector: スクリプト内の独立 mini-linker が、サンプル 3 本 (A5 変位折り畳み、跨ぎ PC16、CODE32 fixup を含む) の期待 S9M1 (header 全 field、code、init data、fixup、CRC) を計算して出力とバイト比較。`badmain.s` は未解決シンボル診断と出力ファイル未作成を要求する。
- QEMU on-target (`tests/qemu/pre/t14_link_run.c`): FAT16 上にソースを保存し、実 S9M1 `asm` で START.O/MAIN.O/OBJ.O/BADMAIN.O を生成、実 `ld` で (1) `PROG.S9M` を生成してホストゴールデンと全バイト比較、(2) 登録して spawn し exit status 27 と `LNK\n` 出力を確認、(3) 未解決シンボルの負例でエラーメッセージと出力ファイル未作成を確認、(4) unload 後の arena 復帰を確認する。registry 未登録の `PROG` に対する CLI `run` の FAT16 フォールバック (`ssos/os/app/cli.c` の `load_disk_module`) も t14 が通す。
- mux 統合 (完了条件、t15): 画面 1 で `edit` により `START.S` を新規作成・保存 (残りのリンク入力 `MAIN.S`/`OBJ.S` は fixture として事前保存)、画面 2 で `asm` → `ld`、画面 3 で `run`、という mux キーイベント (F1/F2/F3) をまたぐ一連の操作を QEMU で検証する。Phase 3 の t13 が edit/asm で採る駆動方法 (実 S9M1 + mux キーイベント + FAT16) を流用する。
- 実機検証は別途記録する (本契約の範囲では未実施)。

## 設計判断と代替案

- **出力形式を S9M1 にする (新形式を作らない)**: validator が GOT/fixup を既に検査し、spawn が A5/fixup/bss を既に処理するため、新形式 S9O2 + loader 拡張はコード量と検証対象を増やすだけ。S9O1 が「S9M1 の既存契約に合わない」要素は reloc の意味論だけであり、それはリンカの適用規則で吸収できる。
- **A5 = private data 先頭 (ダミー GOT) を選んだ (実 GOT 合成はしない)**: asm には `@GOT` 構文がなく、S9O1 の reloc 種類を増やせないため、実 GOT 方式は「リンカが ABS16 を GOT slot 変位に読み替える」という同じ文脈不明性を抱えたまま、コードが 2 命令 (slot 読み + 参照) になるだけである。直接 A5 変位は 1 命令で自然なソースになる。C toolchain (msep-data GOT) とは別の codegen 規約だが、`A5 = data_base + got_offset` という ABI 式は同一で、両者は別 module として共存する。将来 GOT 方式へ移す場合は ABS16 (text)→data の解釈式を got_offset 分ずらすだけでよい。
- **ABS16 (text) → data/bss を「A5 変位」と解釈する**: asm データアクセスはこれ以外に表現手段がなく (PC16 は code/data 距離が非固定、ABS32 は fixup 対象外)、解釈が一意に決まる。限界は `x(%a5)` と `.dc.w x` を区別できないことで、後者は誤リンクになる (「再配置の適用」と「既知の限界」に明記)。拒否する選択肢はデータアクセスを不可能にするため採らない。
- **出力は `ss9_file_store` 1 回** (fd 分割書き込みではない): ストレージ契約の open 排他を回避し、部分書き込みによる不完全ファイルを原理的に排除する。代償は出力全体 (≤48240 byte) をメモリに持つことで、これはスタック予算に織り込んだ。
- **オブジェクトは逐次読み (2 パス)**: 全ロードは 1 オブジェクト 65535 byte がスタックに載らず不可能。パス数は 2 で、シンボルだけを常駐させる。reloc の再読のために各入力を 2 回 open するが、FAT16 の読み直しコストは許容範囲で、決定性とメモリ上限を優先した。
- **エラーは最初の 1 件で停止** (asm と同じ): 未解決シンボルの一括表示は便利だが、既存のエラー処理様式と検証方法を揃えることを優先した。

## 既知の限界

- text 内の ABS16 は常に A5 変位として適用されるため、`.dc.w x` / 絶対 `.w` を text に書くと誤リンクになる (ソース規約で避ける)。
- A5 変位の範囲により、private data 先頭 32768 byte しか変数として参照できない。
- private data は data をパディングなしで連結するため、fixup slot の 4 バイト整列は入力オブジェクトの data サイズが 4 の倍数であることに依存する。前のオブジェクトの data 合計が 4 非倍数のとき、次のオブジェクトの data 先頭の `.dc.l` は private data 内で 4 非整列になり「整列」エラーになる。跨ぎで data ポインタを書くソースは、各オブジェクトの data 末尾を 4 バイト境界に揃えておくこと (`.even` は 2 バイト整列であり、足りない場合は調整が必要)。
- 出力 module の stack は 4096 固定。再帰の深いプログラムは動かない (拡張は CLI 変更を伴うため別判断)。
- `ss9_file_store` の probe → store の間に他 process が同名を作る競合は検出しない (asm と同じ前提)。
