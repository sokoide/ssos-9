# X68000 エミュレータ (XEiJ / MPX68K) を ZCode から操作する方法と限界

更新日: 2026-11-12 (同日、稼働中 XEiJ での実測を反映)。対象: macOS 上の ZCode (CLI agent)、
XEiJ 0.26.10.08 (`/Users/scott/NoBackup/Emu/emulators/x68000/XEiJ_0261008/XEiJ.jar`、
起動は `~/.local/bin/xeij.sh` = `-ini ~/XEiJ.ini`)、
MPX68K (`/Applications/MPX68K.app`、fork ソース `/Volumes/IO512GB/repo/oss/MPX68K`)。

## 結論

| | XEiJ | MPX68K |
| --- | --- | --- |
| ZCode からの操作 | **条件付きで自動運転可** (下記実測) | **不可**。外部入力経路なし (fork 改造で自作は可能) |
| ファイル連携 | **双方向可**。`~/tmp` がゲストの F: (HFS) | 不可 (ディスクイメージ経由のみ) |
| 画面の読み取り | **可**。Option-Shift-C → クリップボード → `pbpaste` | 不可 (ユーザーが観察・報告) |
| 起動 | 可能 (`java -jar` / `xeij.sh`) | 可能 (`open`) |
| ZCode の関与の形 | キー送信・ファイル・画面・事後検証 | **改変版ビルドの作成** + 検証ディスク + 報告の判定 |

**統合テストの第一候補は XEiJ** (ファイル双方向と画面テキスト読取が最初から揃う)。
キー入力の自動化だけが未達で、(A) macOS のキー操作送信/補助アクセス権限の付与、
または (B) `pastepipe=on` (+XEiJ 再起動) で制御パイプを有効化、のどちらかで閉ループする。

## XEiJ 実測 (2026-11-12、稼働中インスタンス)

`xeij.sh` と同一構成で起動中の XEiJ (java プロセス) に対して検証した結果:

1. **F: = `~/tmp` (HFS ホストファイルシステム) — 動作確認済み**。
   `~/XEiJ.ini` の `hf0=/Users/scott/tmp` + Human68k 側 `device=[rom$00e9f020]` の構成。
   ゲスト (Human68k `A:\>`) の `dir f:` 出力に `~/tmp` の中身 (ファイル・ディレクトリ・
   タイムスタンプ) がそのまま現れることを画面コピー経由で確認。ホスト側で `~/tmp` に
   置いたファイルがゲストから直接見える (= ディスクイメージの再作成が不要)。
   HFS はホストディレクトリの生マッピングのため、ゲストから F: への書き込みは
   そのまま `~/tmp` に現れる。
2. **画面テキスト読み取り — 動作確認済み**。Option-Shift-C (XEiJ のテキスト画面コピー)
   → クリップボード → `pbpaste` で 25 行分の全角空白パディング付きクリーンテキストが
   取得できた。ZCode はこれを grep/比較して合否判定に使える。
3. **キー入力の自動化 — 未達 (解決策 2 経路)**:
   - `osascript` の `keystroke`: エラー 1002「キー操作の送信は許可されません」。macOS 側の
     権限付与 (システム設定 → プライバシーとセキュリティ → 補助アクセス等) で解決見込み。
     付与されれば XEiJ を前面に出してゲストキーも Option-Shift-C も全て送れる
     (ini 変更・再起動不要)。
   - XEiJ 制御パイプ: `~/XEiJ.ini` に `pastepipe=on` を追記して再起動 (または「パイプ設定」
     メニューで ON) で有効化。**macOS では FIFO は `$TMPDIR/XEiJControl` に作られる**
     (Java の `java.io.tmpdir`。`/tmp` は WSL の場合のドキュメント記載)。
     `echo 'typekey colon' > "$TMPDIR/XEiJControl"` でキー送信できる (macOS 権限不要)。
   - メニュー UI スクリプティング: エラー -1728 (補助アクセス不可) に加え、XEiJ は
     Java/AWT 製でメニューが AX に露出しない可能性が高く、この経路は期待しない。
4. **SSOS-9 と F: の関係**: HFS は Human68k のデバイスドライブであり、SSOS-9 の
   ストレージ層 (FAT12 2HD / SCSI FAT16) からは直接見えない。SSOS-9 の結果は
   (a) 画面テキスト (pbpaste)、(b) SCSI FAT16 イメージファイルを `~/tmp` に置いて
   `smount` し、停止後にホストで検証 — の 2 経路で取り出す (後者は 2026-11-11 実績どおり)。

### スモークテスト実装済み (2026-11-12、`tools/emulator-smoke.sh`)

実測で判明した制約を織り込んだ最简の通しテスト。**4/4 PASS** を確認済み:
T1 Human68k プロンプト表示 → T2 `keywitch -r` 後に `f:ssos9` で SSOS-9 CLI 起動 →
T3 `mem` の arena 統計 → T4 `exit` で Human68k 復帰 (`SSOS-9 terminated.`)。

実測で判明した追加制約:

- **ゲストの keywitch ドライバがキーをリマップする** (X68000 `:` が `'` に化ける)。
  テスト前にゲストで `keywitch -r` を実行してリセットする。
- **`typekey` に複数キー名を渡すと壊れる** (実装は全キー press → 全キー release のため
  make/break が干渉する)。**1 キー 1 コマンド**で送り、0.1 秒程度間隔を空ける。
- `typekey` のキー名は X68000 スキャン名 (`colon` `semicolon` `return` `minus` `space`
  等、小文字可)。ホストのキーボードレイアウトに依存しない。
- 画面コピー (Option-Shift-C) だけはホストキーイベントが必要で、アクセシビリティ権限の
  あるターミナルから osascript keystroke で送る。

### XEiJ で可能になる統合テスト像

```text
[host/ZCode]                       [XEiJ ゲスト]
make install → ~/tmp に ssos9.x
+FAT16 イメージ (SCSI ID 1)
キー送信(パイプ or keystroke) → f:ssos9.x 起動
                                  SSOS-9 CLI: smount 1 / run edit / asm / ld …
                                  (結果は FAT16 へのファイルとコンソール出力)
Option-Shift-C → pbpaste      ←   画面テキスト
停止後 mtype/fsck.fat/python   ←   FAT16 イメージの中身
```

`make test-qemu` が代行できない領域 (実 IOCS、実 PIC 起動、Human68k 復帰) を
エミュレータ実機相当で埋める位置づけ。キー入力以外は本日時点で全要素実証済み。

## XEiJ

### Codexからの実行 (2026-11-12)

Codexのサンドボックス内では `pgrep` が sysmon 接続エラーとなり、
`osascript` による前面化もエラー `-10827` で失敗した。同じ操作を
サンドボックス外で実行すると、プロセス名 `java` の検出、画面コピー、
制御パイプへのキー送信が成功した。この環境ではCodexのコマンド実行に
サンドボックス外の実行許可を指定して `tools/emulator-smoke.sh` を実行する。
エラー番号だけからmacOSの許可不足とは断定しない。

実測時はXEiJが起動済みで `pastepipe=on`、FIFOは `${TMPDIR%/}/XEiJControl`、
`~/tmp` はゲストの F: に接続されていた。Ghosttyには System Events の
オートメーションと「デバイスの制御とデータへのアクセス」が許可されていた。
スクリプトはHuman68kの `A:\>` から実行し、`keywitch -r`、`f:ssos9`、
`mem`、`exit`、`dir a:` を順に送る。画面取得はXEiJを前面にして
Option-Shift-Cを送り、`pbpaste` から読む。A1の判定はAドライブの一覧に
`<dir>` が表示されることを前提とする。クリップボードと前面アプリは変化する。

2026-11-12にCodexがこの経路で実行し、6判定すべて成功した。実行した
`~/tmp/ssos9.x` のSHA-256は
`4330cc1df45e8b7442baf37b4e8bb9484e4836d8122c34fbb531575dd757aa85`。
実行中のXEiJの正確なバージョン、CPU、RAM構成は今回確認していない。
物理X68000の検証ではない。

### 実績 (2026-11-11、commit `18e136b` の記録)

出典: `tests/README.md`(On 2026-11-11 〜 の 2 段落)、`docs/status-and-plan.md` 検証状況。

1. **FAT16 ストレージ確認**
   - 構成: XEiJ 0.26.10.08、MC68000、12 MiB、Human68k 3.02 を SCSI ID 0 の HDD コピーから起動。
     フロッピ 1 からスタンドアローン `ssos9.x` (SHA-256 `8a5723bf…b9ef3`) を起動し、
     SCSI ID 1 の使い捨て 32 MiB FAT16 イメージに対して `smount 1`、`swrite XTEST.TXT hello`、
     `sread`、`sreplace XTEST.TXT world`、再度 `sread` が成功するのをユーザーが観察。
   - XEiJ 停止後、ホストで `mtype` が `world` を返し、`fsck.fat -n` がエラーなし。
     物理メディア・全 FAT16 ジオメトリ・書き込み中断の検証は含まない。
2. **mux 3 画面確認**
   - 同一構成で `ssos9.x` (SHA-256 `2d1dabd7…9200`) を実行。
   - **XEiJ 制御パイプで F2 (画面 2 選択) → `help` 実行、F3 (画面 3) → `ps` 実行**。
     F2 → F1 で戻った後、ユーザーが画面テキストをコピーし、画面ごとの出力保持と
     元画面の復帰を確認。物理キーボードや MPX68K の F キー挙動の検証は含まない。

repo 記録にはパイプへの書き込み実行者が明記されていない。技術的には後述のとおり
Bash から書き込めるため ZCode から送信可能だが、「ZCode が書いた」との明示的な記録はない。
`mtype` / `fsck.fat -n` はイメージファイルへのホスト側操作で、ZCode が Bash で実行できる。

### 制御パイプの仕様 (XEiJ 付属 `feature.htm` と同梱ソースより)

- **有効化**: メニュー「パイプ設定」の「貼り付けおよび制御パイプ」チェックボックスを ON、
  または `~/XEiJ.ini` に `pastepipe=on` を追記して再起動 (パラメータ `pastepipe`)。
- **パイプ名**: Windows は `\\.\pipe\XEiJControl`。UNIX 系 (含 macOS) は
  **`$java.io.tmpdir/XEiJControl`** — macOS では `TMPDIR` 下の
  `/var/folders/.../T/XEiJControl` (mkfifo で作成、貼り付けパイプは同ディレクトリの
  `XEiJPaste`)。ドキュメントの `/tmp` 記載は WSL (Ubuntu) の場合。
- **文字コード**: パイプ設定で SJIS (デフォルト) / UTF-8 を選択。
- **コマンド** (1 行をパイプに書き込む。例: `echo interrupt > "$TMPDIR/XEiJControl"`):

  | コマンド | 機能 |
  | --- | --- |
  | `interrupt` | インタラプトスイッチを押す |
  | `reset` | リセットスイッチを押す |
  | `opt1reset` | OPT.1 を押しながらリセット |
  | `presskey keyname ...` | キーを押す |
  | `releasekey keyname ...` | キーを離す |
  | `typekey keyname ...` | キーを押して離す (複数キーは press 全→release 全。1 キー 1 コマンド推奨) |

- **キー名**: X68000 スキャン名の小文字 (`esc` `1`..`0` `minus` `caret` `yen` `bs` `tab`、
  文字 `a`..`z`、`return` `space` `del` `rollup` `rolldown` `undo` `left` `up` `right`
  `down` `clr`、テンキー `tenkey7` 等、`help` `xf1`..`xf5` `f1`..`f10` `shift` `ctrl`
  `opt1` `opt2` `num` `kana` `roma` `code` `caps` `ins` `kigou` `touroku` `break` `copy`
  `hiragana` `zenkaku` など。`colon` `semicolon` `comma` `period` `slash` `at`
  `leftbracket` `rightbracket` もある)。ホストのキーボードレイアウトに依存しない。
- パイプは **送信専用**。XEiJ の状態や画面を返す機能はない。

### ZCode からの操作手順 (XEiJ)

1. **イメージ準備** (ホスト、ZCode が実行):
   `make install` で `ssos9.x` / `ssos9.xdf` を生成し SHA-256 を記録。
   FAT16 検証ディスクは `tests/target/prepare_console_image.py` 等で作成。
2. **XEiJ 起動**: ユーザーが起動するか、`java -jar
   /Users/scott/NoBackup/Emu/emulators/x68000/XEiJ_0261008/XEiJ.jar` を ZCode が Bash で
   起動できる (GUI がユーザーのデスクトップに開く)。HDD/FDD/SCSI 構成とパイプ設定 ON を
   ユーザーに依頼する。
3. **キー送信**: パイプへ書き込む。例 (1 キー 1 コマンド、間隔を空ける):

   ```sh
   PIPE="${TMPDIR%/}/XEiJControl"
   echo typekey f2 > "$PIPE"                # 画面 2 へ切替 (SSOS-9 の mux は F1-F3 相当キー)
   for k in h e l p return; do echo "typekey $k" > "$PIPE"; sleep 0.1; done
   echo interrupt > "$PIPE"
   ```

4. **結果確認**: 画面テキストは Option-Shift-C (osascript keystroke、要アクセシビリティ
   権限) → `pbpaste` で ZCode が直接読める。ユーザー経由は不要。
5. **停止後の検証** (ホスト、ZCode が実行):
   `mtype` (`/opt/homebrew/bin/mtype`)、`fsck.fat -n` (`/opt/homebrew/sbin/fsck.fat`)、
   python での直接パースでディスクイメージの内容と整合性を確認。

## MPX68K

### 結論 (MPX68K)

ZCode が MPX68K の GUI (起動後の画面・キー入力) を操作した事実は記録上ない。
実行、ディスク挿入、物理キーボードによるキー入力、画面の観察はすべてユーザーが行い、
ZCode 側は (1) 検証に必要な**改変版 MPX68K のソース変更とビルド**、(2) 検証ディスクの
作成、(3) 手順と合否基準の設計、(4) ユーザーの報告の判定と SSOS-9 側の修正、を担った
(下記の改変版 commit はユーザーの git 名義だが、作成実行者の記録は vault になく断定できない)。

### 実績 (repo / vault 記録)

- **2026-10-12** (ssos-9 process lifecycle): ユーザー確認で `.x`/`.xdf` とも物理キーボードの
  Ctrl-C が動作。macOS 側のキー入れ替えが原因で届かなかった問題を切り分け。
- **2026-11-04** (ssos-68k、案件ノート `sokoide_ssos-68k.md`): FDD 0 に Human68k 3.02 の
  複製、FDD 1 に両 scheduler の `.x` を収録した Human68k 形式 XDF で起動手順を確立。
  `mformat` 新規作成 XDF は Human68k が認識しないため元イメージ形式を保持する規約を記録。
  同セッションで XEiJ の操作 API 接続を試みて `Invalid app` で失敗した記録が残る。
- **2026-11-11** (ssos-9 Phase 1、`tests/README.md`): **「the user ran these tests」** —
  Ctrl 対応検証版 MPX68K で、対話コンソールモジュール (`KEY: 0061` 表示、`~2`/`~1`
  切替後の表示保持、Ctrl-C 中断) とバックグラウンドモジュール (切替・スクロール中も
  `BG 36`〜`BG 59` が連続出力) をユーザーが実行・観察。実行後、接頭辞+特殊キーの
  順序不具合を特定して SSOS-9 を修正し host 回帰試験を実施 (最終バイナリの MPX68K
  再実行は未実施)。
- **2026-11-11** (ssos-9 Phase 2): 更新した Human68k 起動ディスクでエディタ編集中の
  `~2` → `~1` 切替と、戻った後の `hello` と未保存マークの保持を確認
  (FAT16 保存・再読込は MPX68K 実環境では未確認)。

### 改変版検証ビルド (fork リポジトリ)

`/Volumes/IO512GB/repo/oss/MPX68K` に上流へ無いカスタム commit がある:

| commit | 日付 | 内容 |
| --- | --- | --- |
| `2ca64ee` | 2026-11-04 | Add US symbol translation and layout-independent function key remapping (`GameScene.swift` +129 行、`tests/keyboard/test_us_mapping.py` 追加) |
| `1ae38bb` | 2026-11-11 | Fix macOS control key forwarding (`GameScene.swift` +16 行、同テスト更新) |

- ビルド手順書 `README.scott.md` (未追跡) があり、Xcode または
  `xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release …`
  でビルドして `/Applications` にコピーする手順を記載。
- Xcode Archive `2026-11-04 16.17` (検証版の実体)。通常インストール版はユーザーが
  検証版と同じものに入れ替えたと報告 (入れ替え後の通常版での実操作は未確認)。
- 検証ディスク一式が `~/repo/sokoide/ssos-68k/build/mpx68k-test/` にある
  (`human302-ssos.xdf`、`RUNPRE.BAT`、`ssos-*.xdf`)。

### ZCode からの関与の実際 (MPX68K)

**ZCode 側が実行できること**

- fork のソース変更 (キー翻訳・Ctrl 転送のような macOS 入力処理) と、それに伴う
  python キーマッピング試験の追加。
- `xcodebuild` による改変版のビルドと、`.app` の `/Applications` への配置
  (要ユーザー承認)。
- 検証ディスク (Human68k 形式 XDF) の作成 — `mformat` 新規 XDF は Human68k が
  認識しないため、元イメージ形式を保って作成する。
- 検証手順・合否基準の設計と、ユーザーが報告した画面観察の判定、不具合修正。

**できないこと / 制約**

- **キー送信・画面取得の自動経路がない**。XEiJ の制御パイプに相当する外部インタ
  フェースの記録はなく、GUI 操作は全面ユーザー依存。
- 実行中の状態取得・同期は不可能 (観察はユーザーの目視)。
- 実機・物理メディアの検証にはならない。改変版の入力処理自体が本物の
  キーボード配列・実機の代替にはならない (記録にも明記)。
- 通常版 MPX68K での再確認が一部残っている (入れ替え後の実操作が未確認)。

## 共通の注意

- エミュレータ実行による確認は「実 X68000 の検証」ではない。repo の検証境界
  (`AGENTS.md`、各契約文書) に従い、環境・成果物 SHA-256・確認内容を記録して
  区別する。
- QEMU suite (`make test-qemu`) は自動化できる代替経路であり、エミュレータ手動
  確認は QEMU が代行できない IOCS・実 MFP・Human68k 起動・実キー入力の補完に
  位置づける。

## 関連記録

- `tests/README.md` — 2026-11-11 の MPX68K 検証版実行記録と XEiJ 実行記録 2 件
  (hash と境界の記載)。
- `docs/status-and-plan.md` 検証状況 — 同実績の日本語要約。
- `docs/hello-executable-plan.md` — 2026-11-01 の `run hello` ユーザー確認。
- 案件ノート `~/obsidian/31_Projects/sokoide_ssos-68k.md` — 2026-11-04 の MPX68K
  FDD 起動手順と XEiJ 操作 API 失敗 (`Invalid app`) の記録。
- XEiJ 付属 `feature.htm` — 制御パイプ仕様の一次情報。
- `/Volumes/IO512GB/repo/oss/MPX68K` — 改変版の fork (`2ca64ee`、`1ae38bb`)。

## 統合テスト専用の媒体と設定

`tools/xeij-test-profile.py` は元のiniと接続済み媒体を読み、指定した新規ディレクトリに専用ini、媒体の複製、HFS共有先を作る。複製対象は設定の `fdN`、`hdN`、`saN`、`scN`、`seN`、`siN`。読み取り専用指定は保持する。HFSは専用の `hf0` に差し替える。元のXEiJを終了してから、ディスクの全容量分の空き領域を用意して複製する。

```sh
source ~/.elf2x68k
make all
profile_root=$(mktemp -d /tmp/ssos9-xeij.XXXXXXXX)
python3 tools/xeij-test-profile.py "$HOME/XEiJ.ini" ssos/build/export "$profile_root/run"
export SSOS9_XEIJ_INI="$(cd "$profile_root/run" && pwd -P)/XEiJ.ini"
export SSOS9_XEIJ_STAGE="${SSOS9_XEIJ_INI%/*}/hfs"
```

同じJAR・ROMを使って専用iniを `-ini "$SSOS9_XEIJ_INI"` に指定して起動する。Human68kのプロンプトが表示されたら、同じ環境変数でリポジトリルートから `tools/emulator-itest.sh c1` を実行する。再起動ケースには、実際に使用するJARとROMの絶対パスを `SSOS9_XEIJ_JAR` と `SSOS9_XEIJ_CGROM` に設定する。既存の起動手順を使う場合は、第一引数のiniでXEiJを起動する実行ファイルを `SSOS9_XEIJ_LAUNCHER` で指定できる。

複数ケースは `python3 tools/xeij-suite.py a3 h5` で実行する。通常回帰は `make test-emulator-core`、反復回収は `make test-emulator-soak`。専用プロファイルを設定し、`make verify` でアプリ、`make -C tests/target` でconsoleとbackgroundのfixtureを生成した作業ツリーから `make test-emulator-automated` を実行すると、25個のXEiJ自動回帰を選べる。FD1のH2はMPX68Kで実施する手動試験であり、XEiJのID選択肢には含めない。ランナーと搬送ヘルパーのコピー、ケースのログ、送信キー、最後に採取した画面、実行時間、ini・媒体のハッシュを `build/xeij/results/` に保存する。失敗後のケースは未実行として記録する。iniの横に `XEiJ.ini.ssos9.*` が残った場合は、そのプロファイルを停止してからバックアップを確認する。専用環境なら新規複製で次を実行できる。

制御FIFOとクリップボードは同じログイン環境で共有する。XEiJは一度に一台だけ起動し、テスト中の画面コピーを避ける。ランナーはiniで対象PIDを識別し、ケース単位で排他を取る。媒体を変更するケースを独立に再実行するときは新規プロファイルを作成する。XEiJ終了後に複製媒体とログを検査する。

ホスト側のFIFO、排他、コマンド期限、プロファイル複製の回帰は `make test-xeij-transport`。

### Timer Dと例外ベクタの復元

`make build/xeij/mfpstate.x` でHuman68k用の読み取りプローブをビルドする。専用HFSにコピーし、`SSOS9_XEIJ_MFP_CHECK=1 tools/emulator-itest.sh a2` を実行する。起動前の値と各終了後のIERB、IMRB、TCDCRおよびTimer D・TRAP #0・TRAP #13ベクタを比較する。停止中のTimer DはTDDRも比較する。実行中のカウンタ値や割り込み保留ビットは時間で変わるため比較対象から外す。

プローブはSupervisorに移行し、MFPをバイト単位で読み、偶数番地にあるベクタをロングワードで読む。表示の前にUserモードへ復帰する。アドレスとアクセス幅の根拠はx68k-masterの『Inside X68000』要約「03_割り込み」「04_MFP」、復元対象との対応は [mfp_state.s](../ssos/os/kernel/mfp_state.s)。
