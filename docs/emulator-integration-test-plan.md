# XEiJ で確認すべき統合テスト計画

更新日: 2026-11-12。状態: **A1〜A3・B1実装・実行済み、その他は計画**。

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
- 成果物: `make install` の `~/tmp/ssos9.x` / `ssos9.xdf`。SHA-256 を記録。
- ゲスト構成 (現状): Human68k 3.02 フロッピー起動、`~/tmp` = F: (HFS)。
- 追加構成が必要なテスト:
  - **D 系 (FAT16)**: 32 MiB FAT16 イメージをホストで作成し
    `~/XEiJ.ini` に `scsi1=<パス>` を設定のうえ XEiJ を再起動 (1 回の手構成)。
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
| C5 | 対話コンソールモジュール (CONSOLE_KEY) | `tests/target` の CONSOLE.S9M を FAT16 へ配置 → `smount` → `load`/`run console` → 各種キー送信 | `KEY: xxxx` 表示がキーごとに一致 (ASCII は下位 8 bit、特殊キーは scan<<8)。`q` で終了 | P2 | △ (モジュール配置) |

### D. SCSI FAT16 実 IOCS 読み書き (前提: `scsi1=` 構成)

| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| D1 | smount/swrite/sread/sreplace | `smount 1` → `swrite XTEST.TXT hello` → `sread` → `sreplace XTEST.TXT world` → `sread` → `exit` → ホストで `mtype` と `fsck.fat -n` | 画面に各コマンドの成功出力。停止後 `mtype` = `world`、fsck エラーなし (2026-11-11 手動実績の全自動化) | P0 | △ (冒頭構成) |
| D2 | エディタの保存・再読込 | `run edit NEW.S` (FAT16 上) → テキスト入力 → `:wq` → 再度 `run edit NEW.S` | 再読込内容が入力と一致。停止後 `mtype` でも一致 | P1 | △ |
| D3 | `asm` の実 IOCS on-target | `START.S`/`MAIN.S`/`OBJ.S` をホストからイメージへ mtools で配置 → `run asm` ×3 → `exit` → `.O` を mtools で吸い出し host 期待値とバイト比較 | 3 個の `.O` が host ビルドとバイト一致 (t13 の実 IOCS 版) | P1 | △ |
| D4 | `ld` + `run` フォールバック通し | D3 の `.O` から `run ld PROG.S9M …` → `run PROG` | 画面に `Wrote PROG.S9M`、`LNK` 出力、exit status 表示。停止後イメージ内の `PROG.S9M` が host golden (180 byte) と一致。registry 未登録からの FAT16 フォールバック経路を通る | P1 | △ |

### E. 開発ツールチェーン通し (Phase 4 完了条件の実機相当)

| ID | 概要 | 手順 | 合格基準 | 優先度 | 自動化 |
| --- | --- | --- | --- | --- | --- |
| E1 | mux 3 画面で edit→asm→ld→run | F1: `run edit START.S` をパイプ入力で作成・`:wq`。F2: `asm`×3+`ld`。F3: `run PROG` | t15 と同等の結果 (exit status・`LNK`・回収・arena 復帰) を実 IOCS/実キー経路で得る | P1 | △ |
| E2 | リンカ負例 | `run ld BAD.S9M BADMAIN.O` | 画面に `Undefined symbol missing`。`BAD.S9M` が未作成 (`sread` 失敗) | P2 | △ |
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
  プロンプト存続確認 → XEiJ 停止 → `fd0=` 削除 (イジェクト) → 再起動 →
  Human68k `A:\>` 復帰で環境を必ず元に戻す (失敗経路でも復帰は実行する)。

- `tools/emulator-smoke.sh` のヘルパ (`key` / `type_cmd` / `screen` / `check`) を
  共通化し、`tools/emulator-itest.sh <area>` の形で領域ごとに実行できるようにする。
- 待ち時間は画面コピー前に一定の settle を置き、失敗時は取得画面をそのまま
  ログへ出力する (失敗診断は画面テキストが全て)。
- ホスト側検証 (mtype/fsck.fat/mtools/バイト比較) は XEiJ 停止後に python で
  行い、合否をスクリプトの最後に集計する。

## 5. XEiJ でも確認できないもの

- 実機のタイミング精度 (サイクル精度・バス挙動)。XEiJ の負荷率設定に依存する
  測定は B1 のみで、許容を±20% としている。
- 物理キーボード・実媒体 (フロッピー/HDD の実故障・遅延)。
- `.xdf` IPL ブートはディスク挿入とリセットに手操作が入るため半自動 (A3)。
- 上位機種 (030/060、増設 RAM) での動作。対象は MC68000 + 12 MiB 固定。

## 6. 実行順序の提案

1. **P0 一式** (A1, A2, B1〜B3, C2, C3, D1): 実 MFP・実 IOCS・実ストレージの
   核心を最初に固める。D1 だけ冒頭に `scsi1=` 構成が要る。
2. **P1** (A3, B4, C1, C4, D2〜D4, E1, E3): ツールチェーン通しまで拡張。
3. **P2** (C5, E2, F1, F2, G1): 回帰網羅。

各実行後に `tests/README.md` へ環境・hash・結果を記録し、実機確認とは区別して
「XEiJ 実機相当確認」として扱う。
