# XEiJ で確認すべき統合テスト計画

更新日: 2026-11-13。状態: **A1〜A3・B1〜B4・C1〜C3・D1・E3・F1・F2・G1実装・実行済み
(C4を除きPASS)。C4は実行済み・エミュレータキー経路の制限でFAIL (下記)。
C5・D2〜D4実装済み・未実行**。
`smount` は Human68k 形式の先頭パーティションのみを受け付け、MS-DOS 型 FAT16 は拒否される。
パスの 8.3 名照合は大文字小文字を区別しない (Human68k 側の作成・検索の挙動に合わせている)。

## 1. 目的と位置づけ

unit test と QEMU suite がカバーしない領域を、XEiJ 0.26.10.08 (実 X68000 相当の
MFP/IOCS エミュレーション) で埋める。QEMU の検証境界 (`AGENTS.md`) のうち、
XEiJ で代替確認できるもの:

| QEMU が代行できない領域 | XEiJ で確認できるか |
| --- | --- |
| 実 MFP (Timer D) 割り込みハンドラ | 可 (XEiJ は MC68901 をエミュレート) |
| 実 IOCS コンソール (B_PUTC/B_LOCATE、キー入力) | 可 |
| Human68K 起動 (`.x` の PIC 実行、DOS 復帰) | 可 |
| 実 X68000 挙動 (実機) | **不可** (XEiJ 合格 ≠ 実機合格) |
| 実キーボード・実媒体 | 不可 (ホストキーとファイルイメージで代用) |

既存の自動化資産 (`docs/emulator-remote-operation.md`、`tools/emulator-smoke.sh`
T1〜T5、6判定 PASS 済み) を土台にする。実行環境と制約は
`docs/emulator-remote-operation.md` を参照。

## 2. 前提と環境

- 自動化ループ (実証済み): 制御パイプ `${TMPDIR}/XEiJControl` に
  `typekey <key>` を **1 キー 1 コマンド・0.1 秒間隔**で送る。ゲストの keywitch
  ドライバのリマップを冒頭 `keywitch -r` で解除。画面は Option-Shift-C
  (osascript keystroke、要アクセシビリティ権限) → `pbpaste`。
  `typekey yen` はゲストに何も届かないため `\` は `presskey yen` → 0.15 秒保持 →
  `releasekey yen` で送る (0.5 秒以上保持するとリピートで複数文字になる)。
  画面コピーは直前の状態を返すことがあるため、マーカー文字列を仕込んで
  更新を確認し、出力の確認はポーリングで行う。
- 成果物: `make install` の `~/tmp/ssos9.x` / `ssos9.xdf`。SHA-256 を記録。
- ゲスト構成: 起動済み Human68k の A: は既接続の HDD、`~/tmp` = F: (HFS)。
  A: と SCSI ID・Human68k パーティションの対応は読み取り専用で確認し、推測しない。
- 追加構成が必要なテスト:
  - **C5・D 系・E1/E2**: 既接続 A: の Human68k 形式 HDD を使用し、
    テスト対象のファイルは `A:\TEST` 内に限る。既存内容を事前確認し、衝突しない
    新しい名前を使用する。既存ファイルは上書き・削除せず、A: のその他のファイル、
    ini、ディスク構成を変更しない。`A:\TEST` の新規作成には親ディレクトリ項目、
    内部ファイルの更新には共通 FAT・割当管理情報の変更が必要。
    Human68k 側で必要な fixture を F: から `A:\TEST` へ配置する際も、同名ファイルを
    上書きしない。Human68k と SSOS-9 の同時書込みを避け、キャッシュの可視性と
    再読込手順を確立してから検証する。
  - **A3 (.xdf IPL)**: `ssos9.xdf` を FDD 0 へ挿入してリセット
    (XEiJ UI 操作または ini `fd0=`、要再起動)。
- 測定の注意: `debug` の tick は Timer D 200 Hz (5 ms) 基準。XEiJ の MPU 負荷率
  設定によって実効速度が変わるため、許容範囲を宽く取る (B1 参照)。
- 記録規約: `tests/README.md` に環境 (XEiJ/Human68k 版数、MC68000、12 MiB)、
  成果物 SHA-256、確認内容と境界を実績として追記する (2026-11-11 の書式に倣う)。

## 3. テストケース

優先度: P0 = QEMU 完全未カバーの核心、P1 = 未カバーだが代替観測が一部ある、
P2 = 既にユーザー手動実績があり自動化で回帰化するもの。
自動化: ○ = 全自動、△ = 冒頭の手構成のみ、× = 都度手操作。

### A. 起動・終了・Human68k 共存

| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| A1 | `.x` 起動 smoke + 復帰後の Human68k 動作 | 既存 smoke (T1〜T4) の T4 後、`dir a:` を送る | ディレクトリ表示が出る (DOS 環境が壊れていない) | P0 | ○ |
| A2 | 起動/終了の繰り返し (MFP 復帰・`$004F` 監視) | `f:ssos9` → `debug` (MFP IERB/IMRB/TCDCR 表示を記録) → `exit` を 5 回反復。毎回復帰後に Human68k コマンド | 5 回とも SSOS-9 起動・`SSOS-9 terminated.`・`A:\>` 復帰・`$004F` 等のエラーなし | P0 | ○ |
| A3 | `.xdf` IPL ブート + PID 1 exit 拒否 | `ssos9.xdf` を FDD 0 に挿入してリセット → IPL → CLI。`exit` を入力 | IPL から CLI が起動、`exit` が拒否されプロンプトに戻る (PID 1 保護) | P1 | △ (ディスク挿入・リセット) |

### B. 実 MFP Timer D (preemptive scheduler の実割り込み駆動)

| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| B1 | tick 計数 = 200 Hz | `debug` → 2.5 秒待つ → `debug`。2 回の `ticks=` 差分 | 差分が 500±100 (実効 200 Hz±20%) | P0 | ○ |
| B2 | trace にスケジューライベント | `run counter` 中に `trace` | `trace` が 16 件のイベント (切替・tick) を表示。イベント種別が空でない | P0 | ○ |
| B3 | 実割り込みによる preemptive 動作 | `run counter` (foreground) 中に画面を 2 回取得 | カウンタが進み、かつプロンプト入力が可能 (shell が止まっていない) | P0 | ○ |
| B4 | Ctrl-C の実キー経路 | busy foreground (`run counter`) に対し `presskey ctrl` → `typekey c` → `releasekey ctrl` | kill され `wait` で回収、`mem` が baseline に復帰 | P1 | ○ |
| B5 | 終了時 MFP 状態復帰 | A2 の `debug` 記録と、終約後の Human68k 動作で確認 | Timer D 関連ビットが起動前状態に戻る (IERB/IMRB の bit4、TCDCR) | P1 | ○ (A2 に内包) |

### C. 実 IOCS コンソール・キー入力

| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| C1 | `help` 全量表示 | `help` → 画面取得 | 画面テキストが host 期待値 (`cli.c` のコマンド表) と一致 | P1 | ○ |
| C2 | 25 行を超える出力の再描画 | `ps` を反復 or 長出力で 25 行超 → 画面取得 | 物理スクロールせず行が折り返わる。既知の右下セル制限 (空白のまま) も確認 | P0 | ○ |
| C3 | mux 画面切替と保持 (F1〜F3) | `mux` → F2 で `help`、F3 で `ps`、F1 に戻る。各画面をコピー | 画面ごとの出力が保持され、戻った画面が復元される (t15 の実機相当) | P0 | ○ |
| C4 | Ctrl-]+数字 による切替 | `presskey ctrl` → `typekey ]` → `releasekey ctrl` → `typekey 2` | 画面 2 に切替わる | P1 | ○ |
| C5 | 対話コンソールモジュール (CONSOLE_KEY) | `tests/target` の CONSOLE.S9M を `A:\TEST` に配置し、Human68k 形式 HDD をマウント → module 起動 → 各種キー送信 | `KEY: xxxx` 表示がキーごとに一致 (ASCII は下位 8 bit、特殊キーは scan<<8)。`q` で終了 | P2 | △ (モジュール配置) |

### D. Human68k 形式 SCSI HDD の実 IOCS 読み書き

`smount ID` は SCSI ターゲットの先頭 Human68k パーティションを読み書き可で
マウントし、`swrite` / `sreplace` / `sread` は `TEST/NAME.EXT` と
`A:\TEST\NAME.EXT` 形式のパスを受け付ける。対象は既接続 A: の `A:\TEST` のみ。
書込み前に読み取り専用で対象ディスクを特定し、識別できなければ書込みを開始しない。

| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| D1 | 作成・読取り・置換 | すでにマウントされている Human68k 形式 HDD を利用し、`A:\TEST` 内の新規名へ `hello` を作成、読取り、`world` に置換して再読取り。Human68k 復帰後にも確認 | 各操作が成功し、双方の読取りで期待値と一致。対象外ファイルに変更なし | P0 | △ |
| D2 | エディタの保存・再読込 | `A:\TEST` 内の新規ソースを edit で作成・保存し、再度開く | 内容が入力と一致し、Human68k 復帰後にも確認できる | P1 | △ |
| D3 | `asm` の実 IOCS on-target | `A:\TEST` に 3 個の入力を配置し、asm ×3 を実行。生成物のバイト列を取得して host 期待値と比較 | 3 個の `.O` が host ビルドとバイト一致。取得方法は Human68k 形式対応の検証手段として実装する | P1 | △ |
| D4 | `ld` + `run` フォールバック通し | `A:\TEST` の D3 出力からリンクし、未登録の実行ファイルを起動 | `Wrote`、`LNK`、終了状態を確認し、生成物を host golden と比較。Human68k 形式の fallback を通る | P1 | △ |

### E. 開発ツールチェーン通し (Phase 4 完了条件の実機相当)

| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| E1 | mux 3 画面で edit→asm→ld→run | `A:\TEST` 内で F1: edit により新規ソース作成・`:wq`、F2: asm ×3 + ld、F3: 生成プログラムを起動 | exit status・`LNK`・回収・arena 復帰を実 IOCS/実キー経路で確認 | P1 | △ |
| E2 | リンカ負例 | `A:\TEST` 内の未定義シンボルを含むオブジェクトをリンク | `Undefined symbol missing` と非 0 終了を確認し、Human68k 側でも出力ファイルが未作成と確認 | P2 | △ |
| E3 | builtin hello の実 PIC 起動 | `run hello` | `Hello` 表示と exit status 0、shell 復帰 (実割り込みで切替えられた証跡として `trace` も) | P1 | ○ |

### F. 背景 builtin と IOCS ロック

| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| F1 | 背景 builtin (kernel-entry 子) | `help&` `ps&` `mem&` を投入 → `wait` | 各出力が混ざらず出力され、回収後 `mem` が baseline 復帰 | P2 | ○ |
| F2 | 背景出力中の画面切替 | `run counter&` 出力中に F2/F1 切替を反復 | 出力が切替後も連続 (ユーザー手動実績 2026-11-11 の自動化) | P2 | ○ |

### G. ストレス・回帰

| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| G1 | spawn/kill/wait 反復と baseline 復帰 | `run counter` → Ctrl-C → `wait` ×5 → `mem` | free_bytes/largest_free が起動直後と一致 | P2 | ○ |

## 4. スクリプト運用

- A2: `tools/emulator-itest.sh a2`。各回の画面とMFP表示を確認し、
  Human68k復帰後に `dir a:` を実行する。A: の一覧に `<dir>` が必要。
  終了前後のMFP値を直接比較するB5は未実装。
- B1: `tools/emulator-itest.sh b1`。SSOS-9 を起動し、`debug` の `ticks=`
  (8 桁大文字 HEX) を 2 回取得。2 回の typing 間隔を実測し、
  `delta ≈ 200 Hz × elapsed` (±20%) を判定する。typing と画面コピーの
  オーバーヘッドは 2 回のサンプルで等しいので相殺される。合格後に `exit` で
  Human68k へ戻る。
- A3: `tools/emulator-itest.sh a3`。XEiJ の Java メニューは AX から操作できない
  ため、FD メニューの代わりに **ini の `fd0=` 割当 + XEiJ アプリ再起動**で
  ディスク挿入/イジェクトを実現する (起動デバイスは STD のため、FD 挿入なら
  FD から、挿入がなければ HDD から起動する)。手順: XEiJ 停止 → `fd0=~/tmp/ssos9.xdf`
  追記 → 再起動 → XDF IPL 起動とバナー確認 → `exit` の PID 1 拒否メッセージ確認 →
  プロンプト存続確認 → XEiJ 停止 → 実行前の ini 全体を復元 → 再起動。
  失敗・HUP・INT・TERM 時も復元を試み、停止や復元に失敗すればバックアップを残す。
  SIGKILL やシステム停止では復元処理は動作しない。起動先は元の設定に依存する。

- E3: `tools/emulator-itest.sh e3`。`run hello` の出力と終了状態 0、
  その後の shell プロンプトを確認し、`trace` の Timer D による
  `switch` (`value` の resume_type が 0) を確認して Human68k に復帰する。
  trace は直近 16 件であり、その switch が hello 自身の実行中だったとは限らない。
- D1: `tools/emulator-itest.sh d1`。実行前に `make install`、実行時点で XEiJ が
  Human68k プロンプトで待機していること。一意名の fixture (内容 `D1FIXTURE`) を
  F: から Human68k 側で `A:\TEST` に copy し (ディレクトリがなければ Human68k 側で
  `mkdir`)、SSOS-9 内で SCSI ID 0〜7 を `smount` して fixture を `sread` できた
  ID だけを A: の対象とする。一致する ID がなければ書込みを行わず失敗する。
  対象 ID 上で一意名に `swrite … hello` → `sread` → `sreplace … world` →
  `sread` を行う。`exit` 後、Human68k のディスクキャッシュ (トラックバッファ) が
  SSOS-9 が書いたディレクトリ項目を隠すため、制御パイプの `reset` でゲストを
  再起動してから `dir a:\test` (新規名の存在と対象外の 8.3 名集合が不変) と
  `type` (`world`、`hello` 残留なし) を確認する。SSOS-9 が作る名前は大文字で
  格納・表示され、Human68k が作る名前は入力時のケースのまま保存されるため、
  行比較は両ケースを受け付ける。検証は画面テキスト経由のため、18.3 名や
  `A:\TEST` 内サブディレクトリの変化は比較できない。
  実行済み (2026-11-13、A: = SCSI ID 0)。

- `tools/emulator-smoke.sh` のヘルパ (`key` / `type_cmd` / `screen` / `check`) を
  共通化し、`tools/emulator-itest.sh <area>` の形で領域ごとに実行できるようにする。
- 待ち時間は画面コピー前に一定の settle を置き、失敗時は取得画面をそのまま
  ログへ出力する (失敗診断は画面テキストが全て)。
- Human68k 形式のファイル比較・整合性確認には、当該形式に対応すると確認した手段を
  使用する。PC 用 mtools / fsck.fat がそのまま使用可能とは仮定しない。
- B2/B3: `tools/emulator-itest.sh b2|b3`。`run counter&` でビルトイン counter
  (画面出力なしの private data カウンタ) を回しつつ `trace` / `debug` / `ps` を
  実行する。counter のカウント値自体は画面から観測できないため、tick 前進・
  イベント種別・シェル応答性をもって preemptive 動作の代替確認とする。
- B4: `tools/emulator-itest.sh b4`。foreground `run counter` に実 Ctrl-C を送り、
  `^C` と `PID n interrupted`、プロンプト復帰、`mem` の baseline 復帰を確認する。
- C1: `tools/emulator-itest.sh c1`。`help` 出力を空白・改行を除去して正規化し
  `cli.c` のコマンド表と突き合わせる (コンソール折返しの位置に依存しない)。
- C2: `tools/emulator-itest.sh c2`。`ps` ×8 で 25 行超の出力を流し、内部ウィンドウ
  再描画によりプロンプトが最終行に残ることとエラーなしを確認する。右下セルが
  書き込まれないことの確認はテキストコピーでは区別できないため手動確認のまま。
- C3/C4: `tools/emulator-itest.sh c3|c4`。F1〜F3 と Ctrl-]+数字 の切替え、各画面の
  出力保持・復元を確認する。C4 は 2026-11-13 に実行したが、この環境
  (XEiJ + Human68k 3.02 + ASK68K フロントプロセッサ) では IOCS が ctrl を文字キーに
  のみエンコードし (ctrl-c=0x03 は B4 で実証)、ctrl+]→0x1d は届かない。
  また shift+caret は ASCII の `~` (0x7e) ではなく JIS 全角チルダの 2 バイト
  (0x81,0x60) として届くため `~` プレフィックスも自動送信できない。両者を試行して
  切替わらない場合は F1 に戻って終了し FAIL する。F キー経由の切替え自体は C3 で
  確認済み。実キーボードでの `~` 切替えは 2026-11-11 の手動実績による。
- C5: `tools/emulator-itest.sh c5`。`tests/target/build/console.s9m` を `A:\TEST` の
  `CONSOLE.S9M` として配置し (既存なら再配置しない)、`smount 0` 後 `run
  test/console` で実キー入力が `KEY: xxxx` と一致することを確認する。
- D2: `tools/emulator-itest.sh d2`。`apps/edit/build/edit.s9m` を `EDIT.S9M` として
  配置し、`run test/edit test/<新規名>` で新規作成 (`n`) → 挿入 (`i` … ESC) →
  `:wq` 保存 → 再オープンで内容一致 → `:q!` を確認、リセット後 Human68k の
  `type` でも確認する。
- D3: `tools/emulator-itest.sh d3`。`tests/asm/link` の 3 ソースを一意名で
  `A:\TEST` に置き `run test/asm` ×3、リセット後 `.O` を F: 経由で取得し
  host `asm-host` の出力とバイト比較する。
- D4: `tools/emulator-itest.sh d4`。同一手順で `.O` を作り `run test/ld` で
  一意名 `PROGxxxx.S9M` にリンク、`run test/<prog>` の FAT 経由フォールバック
  起動で `LNK` と status 27 を確認、生成物を host `asm-host`+`link-host` の
  golden とバイト比較する。モジュール画像 (`EDIT/ASM/LD/CONSOLE.S9M`) は
  `A:\TEST` 内の初回配置を再利用し、ビルド更新後の再配置は手作業で行う。
- F1: `tools/emulator-itest.sh f1`。`help&` `ps&` `mem&` を投入し、3 出力が
  行単位で崩れず揃うこと、`wait PID` で回収し `mem` が baseline に戻ることを
  確認する (`wait` は PID 必須で引数なしには対応しない)。
- F2: `tools/emulator-itest.sh f2`。ビルトイン counter は出力を持たないため、
  2026-11-11 の手動実績と同じ `tests/target/build/BG.S9M` (番号付き record を
  約 30 秒出力) を `A:\TEST` に配置して `run test/bg&` で起動し、F2/F1 切替えを
  3 往復して非表示中も record 番号が前進し続けること、最後まで走り切って
  status 0 で終了することを確認する。
- G1: `tools/emulator-itest.sh g1`。`run counter&` → `kill` → `wait` を 5 サイクル
  反復し、`mem` が起動直後の baseline に一致することを確認する。

## 5. XEiJ でも確認できないもの

- 実機のタイミング精度 (サイクル精度・バス挙動)。XEiJ の負荷率設定に依存する
  測定は B1 のみで、許容を±20% としている。
- 物理キーボード・実媒体 (フロッピー/HDD の実故障・遅延)。
- `.xdf` IPL ブートはディスク挿入とリセットに手操作が入るため半自動 (A3)。
- 上位機種 (030/060、増設 RAM) での動作。対象は MC68000 + 12 MiB 固定。

## 6. 実行順序の提案

1. **P0 一式** (A1, A2, B1〜B3, C2, C3, D1): 実 MFP・実 IOCS・実ストレージの
   核心を最初に固める。D1 は Human68k 形式 HDD 対応の完成後に実施する。
2. **P1** (A3, B4, C1, C4, D2〜D4, E1, E3): ツールチェーン通しまで拡張。
3. **P2** (C5, E2, F1, F2, G1): 回帰網羅。

各実行後に `tests/README.md` へ環境・hash・結果を記録し、実機確認とは区別して
「XEiJ 実機相当確認」として扱う。
