# XEiJ で確認すべき統合テスト計画

更新日: 2026-11-21。状態: **全領域 (A1〜A3・B1〜B4・C1〜C6・D1〜D4・E1〜E3・F1・F2・G1・H1・H3〜H6)
実装済み。合格は A〜G・H1・H3〜H6 の自動領域と H2 の MPX68K 試験 (2026-11-18 実績)。
2026-11-21 追加・実行済み: C6 (BS/←/DEL の実キー配信 PASS。typekey に名前の無い ←/DEL は host key event
(osascript key code) で届き、IOCS 0x3B00/0x3D00/0x3700 を実測。shell 行編集も同日修正でこれらを消費)、
H1 プローブ拡張 (IERA/IMRA/TACR/VR + Timer A/KEY/CRTC ベクタ、TADR は動作カウンタのため除外。拡張 15 項目で 5 反復 PASS)、
c1 拡張 (uptime/modules/clear/version)。H2 ランナー (`tools/mpx68k-h2.sh`) は実 MPX68K で 11/11 PASS。
追加テストと高速化の詳細は docs/test-coverage-and-speedup-plan.md**。
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
  更新を確認し、出力の確認はポーリングで行う。`_` も瞬間 typekey で落ちるため
  yen と同様に保持送信する。長いタイプ列はフル速度で尾を失うため 0.05 秒/文字
  と定期 settle で送る。yen 保持はオートリピート競合で稀に 2 文字になるため、
  Human68k 側の typed path は `cd test` + 相対名でバックスラッシュ自体を
  排除している (SSOS-9 内は `test/...` のスラッシュ形式)。
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
| B2 | trace にスケジューライベント | `run counter` 中に `trace` | `trace` の画面に見える末尾で10件以上の有効なイベントと切替を確認し、プロンプトへの復帰を確認 (64件の出力ではヘッダは画面外へ流れる) | P0 | ○ |
| B3 | 実割り込みによる preemptive 動作 | `run counter` (foreground) 中に画面を 2 回取得 | カウンタが進み、かつプロンプト入力が可能 (shell が止まっていない) | P0 | ○ |
| B4 | Ctrl-C の実キー経路 | busy foreground (`run counter`) に対し `presskey ctrl` → `typekey c` → `releasekey ctrl` | kill され `wait` で回収、`mem` が baseline に復帰 | P1 | ○ |
| B5 | 終了時 MFP 状態復帰 | A2起動前と各終了後にHuman68k読み取りプローブを実行 | IERB/IMRB/TCDCR、Timer D・TRAP #0・#13ベクタが一致。停止時のTDDRも一致 | P1 | ○ (`SSOS9_XEIJ_MFP_CHECK=1`) |

### C. 実 IOCS コンソール・キー入力
Ç
| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| C1 | `help` 全量表示 | `help` → 画面取得 | 画面テキストが host 期待値 (`cli.c` のコマンド表) と一致 | P1 | ○ |
| C2 | 25 行を超える出力の再描画 | `ps` を反復 or 長出力で 25 行超 → 画面取得 | 物理スクロールせず行が折り返わる。既知の右下セル制限 (空白のまま) も確認 | P0 | ○ (右下セルは目視) |
| C3 | mux 画面切替と保持 (F1〜F3) | `mux` → F2 で `help`、F3 で `ps`、F1 に戻る。各画面をコピー | 画面ごとの出力が保持され、戻った画面が復元される (t15 の実機相当) | P0 | ○ |
| C4 | F1/F2/F3 による mux 画面選択と出力保持 | 画面 2 で `mem`、画面 3 で `ps`、F2/F1へ戻る | 選択した画面に切り替わり、画面2の出力が保持される | P1 | ○ |
| C5 | 対話コンソールモジュール (CONSOLE_KEY) | `tests/target` の CONSOLE.S9M を `A:\TEST` に配置し、Human68k 形式 HDD をマウント → module 起動 → 各種キー送信 | `KEY: xxxx` 表示がキーごとに一致 (ASCII は下位 8 bit、特殊キーは scan<<8)。`q` で終了 | P2 | △ (モジュール配置) |
| C6 | BS/←/→/DEL 行編集の実キー配信とエコー | `helq`+BS+`p` (→ help)、`versioz`+BS×2+`on` (→ version)、`clear` で前の応答を消去し、`version`+←×2+→+`z`+DEL+BS+`n` (→ version。←/→/DEL は osascript key code 123/124/117 の実キーイベント、IOCS 0x3B00/0x3D00/0x3700) | 各完成コマンドが実行される。さらにmux有効時に80列目と次行の先頭を左右キーでまたぎ、DELとBSの後で両セルが消え、編集後のコマンドが実行される。制御パイプ (`typekey`/`presskey`) に ←/DEL の名前は無いが実キーイベントは届く (2026-11-21 実測。当初「DEL 配信不能」と結論したのは pipe 名の局限で、キー自体は存在する) | P2 | **実行済み PASS** (2026-11-21、修正後ビルド) |

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

### H. カバレッジレビュー追加 (2026-11-14、docs/test-coverage-and-speedup-plan.md 由来)

| ID | 概要 | 手順 | 合格基準 | 状態 |
| --- | --- | --- | --- | --- |
| H1 | B5: Timer D・例外ベクタの終了時復元を直接比較 | `SSOS9_XEIJ_MFP_CHECK=1`でa2を実行し、Human68kプローブを起動前と各終了後に実行 | 2026-11-21拡張後: IERB/IMRB/TCDCR/TDDR (停止時) + IERA/IMRA/TACR/VR + Timer D・TRAP #0・#13・Timer A・KEY・CRTC($138/$13C) ベクタ一致。TADR は Timer A 動作カウンタのため比較対象外。受入targetはプローブを必須化し、単独実行は `make test-emulator-h1` | **実行済み PASS (旧項目 2026-11-18、拡張 15 項目で 5 反復 2026-11-21)** |
| H2 | `load 1` 実フロッピ経路 | MPX68KでFD1に専用2HD画像を挿入し、Human68kのC:から起動・読出し。SSOS-9で `load 1 console.s9m` → `run console` | モジュールが `KEY: 0061` を表示し、qで終了、Human68kへ復帰。2026-11-21 から `tools/mpx68k-h2.sh` (`make test-mpx68k-h2`) が3クラスタ `load 1 edit.s9m` と load 負例を含む11ゲートを自動判定 (実 MPX68K で 11/11 PASS) | **MPX68K 自動 PASS (2026-11-21)**。XEiJ ini添付はディスク変更通知を再現できず不安定なため自動化対象外。手順とハッシュは `tests/README.md` |
| H3 | Ctrl-C のウィンドウ間ルーティング | window 2 で foreground `run counter` → F1 → Ctrl-C → F2 確認 → F2 で Ctrl-C | 隠れたウィンドウの foreground は中断されず、自ウィンドウの Ctrl-C でのみ中断される (cli_wait.c はアクティブウィンドウのみ配送) | 実行済み PASS |
| H4 | sread 境界 | edit で空ファイルと 286 バイトファイルを作成し `sread` | 空ファイルは 0 バイトでエラーなし、286 バイトは `maximum 256 bytes` で拒否 | 実行済み PASS |
| H5 | XDF IPL 起動 + ストレージ | A:\TEST に fixture を配置後、ini `fd0=ssos9.xdf` + 再起動で XDF 起動 → `smount 0` → `sread` → `exit` 拒否 | fixture が読める、PID 1 exit が拒否される、ini 復元後 Human68k 復帰 | 実行済み PASS (2026-11-14、1:08) |
| H6 | 25 サイクル spawn/kill/wait ソーク | G1 を 25 サイクルに延長 | arena が baseline に完全復帰 | 実行済み PASS (2026-11-14)。S3 高速化の即時初回チェックが前サイクルの Started 行に一致して古い PID を kill する競合が発覚 → wait_new_pid (単調増加 PID 待ち) で g1/h6/f1 を修正 |

## 4. スクリプト運用

- A2: `tools/emulator-itest.sh a2`。各回の画面とMFP表示を確認し、
  Human68k復帰後に `dir a:` を実行する。A: の一覧に `<dir>` が必要。
  `SSOS9_XEIJ_MFP_CHECK=1`を付けた場合は `mfpstate.x` で起動前と終了後の状態を直接比較する。結果は下記記録参照。
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
  trace は直近 64 件までであり、その switch が hello 自身の実行中だったとは限らない。
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
- C2: `tools/emulator-itest.sh c2`。`ps` ×9 で31行目まで出力し、内部ウィンドウ
  再描画によりプロンプトが最終コンソール行に残ることとエラーなしを確認する。右下セルが
  書き込まれないことの確認はテキストコピーでは区別できないため手動確認のまま。
- C3/C4: `tools/emulator-itest.sh c3|c4`。mux の選択はF1/F2/F3を使い、各画面の
  出力保持・復元を確認する。C4は画面2の `mem` と
  画面3の `ps` を実行し、F2/F1へ戻ったときの画面選択と出力保持を検証する。
- C5: `tools/emulator-itest.sh c5`。`tests/target/build/console.s9m` を `A:\TEST` の
  `CONSOLE.S9M` として配置し (既存なら再配置しない)、`smount 0` 後 `run
  test/console` で実キー入力が `KEY: xxxx` と一致することを確認する。
  モジュールは最初のキーまで `KEY:` を描画しないため、キー送信で駆動する。
  typekey はシフトを送れないため入力はすべて小文字として届く。
  実行済み (2026-11-13、a/z/5/F5/q を確認)。
- D2: `tools/emulator-itest.sh d2`。`apps/edit/build/edit.s9m` を `EDIT.S9M` として
  配置し、`run test/edit test/<新規名>` で新規作成 (`n`) → 挿入 (`i` … ESC) →
  `:wq` 保存 → 再オープンで内容一致 → `:q!` を確認、リセット後 Human68k の
  `type` でも確認する。`Written` 表示は保存から退出までの一瞬のため、
  `:wq` の成功は終了ステータス 0 で判定する。実行済み (2026-11-13)。
- D3: `tools/emulator-itest.sh d3`。`tests/asm/link` の 3 ソースを一意名で
  `A:\TEST` に置き `run test/asm` ×3、リセット後 `.O` を F: 経由で取得し
  host `asm-host` の出力とバイト比較する。実行済み (2026-11-13、3 個とも一致)。
- D4: `tools/emulator-itest.sh d4`。同一手順で `.O` を作り `run test/ld` で
  一意名 `progxxxx.s9m` にリンク、`run test/<prog>` の FAT 経由フォールバック
  起動で `LNK` と status 27 を確認、生成物を host `asm-host`+`link-host` の
  golden とバイト比較する。ld はモジュール名をイメージに埋め込むため、golden の
  出力名はゲストが typekey で入力できる小文字に合わせる。モジュール画像
  (`EDIT/ASM/LD/CONSOLE.S9M`) は `A:\TEST` 内の初回配置を再利用し、ビルド更新後の
  再配置は手作業で行う。実行済み (2026-11-13、golden バイト一致)。
- E1: `tools/emulator-itest.sh e1`。mux の F1 で edit により簡略ソース 3 本を
  作成 (`n` → `i` → タイプ → ESC → `:wq`)、F2 で `run test/asm` ×3 +
  `run test/ld`、F3 で `run test/<prog>` のフォールバック起動 (`lnk`、status 27)
  を行う。ディスクフォールバックの `run` は終了後も登録イメージを残すため、
  arena 比較前に `unload` で edit/asm/ld/生成プログラムを解放する。
  cleanup は exact 名の `del` (確認プロンプトなし) で行い、`dir` 再取得を
  ポーリングして消えたことを確認する。実行済み (2026-11-13、PASS、
  arena baseline `253908/253908` に復帰)。
- E2: `tools/emulator-itest.sh e2`。`tests/asm/link/badmain.s` (未定義シンボル
  参照) を `A:\TEST` に配置して target 上で asm し、`run test/ld` が
  `Undefined symbol missing` と status 1 で止まること、生成ファイルが未作成で
  あることを Human68k 側で確認する。`h68_absent` は単発照合が stale 画面と
  競合するため `dir` 再取得をポーリングする。
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
- C6: `tools/emulator-itest.sh c6`。BS は `key bs` (typekey 経由)、←/→/DEL は
  `host_keycode` (osascript の実キーイベント。mac key code 123=←, 124=→, 117=DEL) で
  送り、削除・挿入を含めてコマンドを完成させ、結果 (help/version 表示) で配信と
  行編集を判定する。制御パイプには ←/DEL の key name が無い (typekey/presskey
  `del` は届かない) ためこの分担であり、IOCS 値は 0x3B00/0x3D00/0x3700 を実測済み。
- H2 (MPX68K): `make test-mpx68k-h2` (`tools/mpx68k-h2.sh`)。実績手順は
  `tests/README.md` の「MPX68K FD1 smoke test」を参照。
- H3: `tools/emulator-itest.sh h3`。mux の window 2 で foreground 実行中に F1 で
  Ctrl-C → 隠れたウィンドウの foreground は生存、F2 に切替えて Ctrl-C で中断。
- H4: `tools/emulator-itest.sh h4`。edit で空ファイル (即 `:wq`) と 7行×40文字の
  ファイルを作り、`sread` の 0 バイト成功と 256 バイト超拒否を確認、cleanup する。
- H5: `tools/emulator-itest.sh h5`。Human68k 側で fixture を配置してから ini
  `fd0=ssos9.xdf` + 再起動 (a3 と同じバックアップ/復元) で XDF 起動し、`smount 0` +
  `sread` + PID 1 exit 拒否を確認、ini 復元後に fixture を削除する。
- H6: `tools/emulator-itest.sh h6`。G1 を 25 サイクルに延長したソーク。

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
