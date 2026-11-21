# テストカバレッジレビューと追加計画 (2026-11-14)

更新日: 2026-11-14。エミュレータ統合テスト (A〜G) 完遂後のカバレッジレビューに基づく
追加テスト計画と、XEiJ 実行の高速化計画。実装状況は各表の「状態」列で追跡する。
2026-11-14 実装分: S1〜S4 適用・検証済み (c1 40s→14.7s、b4 20.0s、c3 27.8s、
d2 1:52、a2 1:12)、H1/H3〜H6 実装・実行 PASS、U1〜U3 追加 (host 46/46)、
Q1 追加 (QEMU 18/18)。H2 はMPX68K手動試験でPASS、XEiJ自動化対象外。Q2は2026-11-18に実装しQEMU t15でPASS。

## 1. 現状カバレッジの要約

| 領域 | 検証層 | 評価 |
| --- | --- | --- |
| スケジューラ状態機御・sleep・優先度 | unit + QEMU t01〜t08 | 充分 |
| syscall 契約 (全 20 番号・整列・境界) | QEMU t13 (call_sys 58 件) | 充分 (全番号直接検証) |
| FD 限度・所有権・終了時回収 | unit test_cli_background | 充分 |
| FAT16/Human68k ストレージ・故障注入・fencing | unit storage_integrity + QEMU t16 | 充分 |
| asm/link ゴールデン (GAS/ld/mini-linker) | golden + QEMU t13/t14 + emulator D3/D4 | 三層で充分 |
| editor/mux 論理 | unit + QEMU t15 + emulator D2/E1 | 充分 |
| 実 MFP・実 IOCS・実 HDD・ツールチェーン通し | emulator A〜G 全領域、H2はMPX68K | 自動試験PASS、FD1読出しはMPX68K手動PASS |

設計境界につき計画外: 電断リカバリ、実キーボード・実媒体、実機タイミング精度、
実ディスクの容量上限試行 (82MB 書込みは非現実的、host sparse イメージで代替済み)。

## 2. 追加テスト (ギャップ)

### エミュレータ層 (H 系、`tools/emulator-itest.sh` に追加)

| ID | 内容 | 根拠 | 優先度 | 状態 |
| --- | --- | --- | --- | --- |
| H1 | B5: Human68k側読み取りプローブで起動前後のIERB/IMRB/TCDCR、Timer D・TRAP #0・#13ベクタを直接比較。停止時のみTDDRも比較 | Timer D割込みとtrap vectorをHuman68k復帰後に読む | P1 | **実装・PASS** (`SSOS9_XEIJ_MFP_CHECK=1`でa2を5反復。各終了後に同一状態) |
| H2 | `load 1 CONSOLE.S9M` 実フロッピ経路。XEiJでなくMPX68KのGUIからFD1へ専用画像を挿入し、Human68k C:で `SS9CON.X` 起動後、SSOS-9のFAT12読出し・モジュール実行を確認 | XEiJのini時添付ではディスク変更通知が不確実。MPX68KのFDDメニューによる実挿入を採用 | P1 | **MPX68K手動 PASS (2026-11-18)**。`load 1 console.s9m`成功、`KEY: 0061`、q終了、Human68k復帰。詳細は `tests/README.md` |
| H3 | Ctrl-C のウィンドウ間ルーティング: window 2 で foreground `run counter`、F1 に切替えて Ctrl-C → **window 2 の PID は中断されず** (cli_wait.c はアクティブウィンドウのみ配送)、window 2 に切り替えて Ctrl-C で回収。挙動を固定期待値として文書化 | 実 IOCS での配送先確認 | P2 | **実装・PASS** |
| H4 | sread 境界: (a) edit で空ファイルを作成し sread → 0 バイトでエラーなし、(b) 編集で 300 バイト超のファイルを作り sread → `maximum 256 bytes` 拒否。作成後は exact 名で cleanup | CLI sread の上限の実確認 | P2 | **実装・PASS** (7行×40文字=286バイトで拒否確認) |
| H5 | XDF IPL 起動での `smount 0` + `sread` (Human68k 非経由起動とストレージの直交)。a3 を拡張 | 経路の直交性 | P3 | **実装・PASS** (`h5` でfixture読出し、PID 1終了拒否、ini復元) |
| H6 | ソーク: G1 を 25 サイクルに延長、または D1 を 3 連続実行 | 回帰網羅 | P3 | **実装・PASS** (2026-11-14)。S3 の即時初回チェックが stale な Started 行を拾い古い PID を kill する競合を発見 — wait_new_pid ヘルパ (PID 単調増加待ち) で g1/h6/f1 を修正、リークはスクリプト起因で製品ではないことを確認 |

### unit 層 (`tests/unit/`)

| ID | 内容 | 根拠 | 優先度 | 状態 |
| --- | --- | --- | --- | --- |
| U1 | mux: 0x1d (Ctrl-]) プレフィックス経路、キュー満杯 + prefix 保留の相互作用 (`CAPACITY - (prefix?1:0)` ガード)。c4 で実 IOCS 経路が不能になった分、unit が唯一の守り | test_mux は `~` のみ | P2 | **実装・PASS** (no-op switch・実switch・非 digit push・満杯 push 失敗) |
| U2 | scheduler: trace リングのラップアラウンド (>16 イベントで最古 16 件が oldest-first)。B2 は実機暗黙確認のみ | 決定論的検証欠落 | P2 | **実装・PASS** (create+start 10 ペア→CREATE 8 + START 8) |
| U3 | human68k_volume 陰性 8 ケースの内訳監査 (fat_start=0、clusters<4085、total>partition、media=0xf7 以外等)、抜けがあれば補填 | 現状でも充分の可能性 | P3 | **実装・PASS**: 監査の結果 11 分岐が未カバーと判明し補填 (disk id、table id、partition 名、fat_start=0、root_entries=0、total>partition、media、hidden 不一致、clusters<4085、spc 非べき乗、fat_count=0 + flags=2 の正当ケース)。host 46/46 |

### QEMU 層 (`tests/qemu/pre/`)

| ID | 内容 | 根拠 | 優先度 | 状態 |
| --- | --- | --- | --- | --- |
| Q1 | t13: CONSOLE_SIZE の mux 有効時契約 (`ss_cli_console_mux_enable()` 後も 25 行既定を返す) | 新 syscall の非 mux 側のみ検証済み | P2 | **実装・PASS** |
| Q2 | t15: B_CONSOLを31行に設定し、CONSOLE_SIZEと実editorのstatus/command行を検査 | 高さ可変時のlayout回帰 | P3 | **実装・PASS** (QEMU t15、80×31、status row 29 / command row 30) |

## 3. XEiJ 実行の高速化 (S 系、`tools/emulator-itest.sh`)

現行の代表的な所要: c1 ≈ 40 秒、d2 ≈ 2 分 (ゲスト reset 約 40 秒含む)、e1 ≈ 4〜5 分。
無駄の正体は (1) type_cmd ごとの不要な frontmost、(2) 画面コピーの固定 1.2 秒待ち、
(3) wait 系の固定 3 秒ポーリング、(4) 冗長な固定 sleep・dir。

| ID | 内容 | 予想効果 | 状態 |
| --- | --- | --- | --- |
| S1 | type_cmd から `frontmost` 削除 [実装済み] (制御パイプ書込みはフォーカス不要。Option-Shift-C のみ focus が必要で screen() が既に確保する) | コマンド 1 回あたり約 0.2〜0.4 秒 × 全コマンド | **実装・確認済み** |
| S2 | screen() のクリップボード取得をアダプティブ化 [実装済み]: 0.4 秒後に取得し、マーカーが残っていれば 0.3 秒間隔で再試 (上限は従来どおり) | 1 取得あたり約 0.6〜0.8 秒 × 全取得 | **実装・確認済み** |
| S3 | wait_screen/wait_line/wait_row をアダプティブ間隔に [実装済み]: 即初回チェック → 0.5 → 1 → 2 秒の指数バックオフ、期限は SECONDS 基準 | 成功待ち 1 回あたり最大約 2.5 秒短縮 | **実装・確認済み** |
| S4 | 冗長 sleep・dir 削減 [実装済み]: boot_ssos 末尾の sleep 1 削除、guest_reset の sleep 3→2、h68_prep の mkdir 未発生時の二度目の dir 省略、代表箇所の固定 sleep → wait 置換 | 1 テストあたり数秒〜十数秒 | **実装・確認済み** |
| S5 | キー送信間隔は 0.1 秒を維持 (typekey 衝突回避の実証済み下限。0 秒間隔で化けることを本セッションで再確認済み) — 明文化のみ | — | 採用決定 |

検証手順: S1〜S4 適用後に c1・b4・c3・d2 を実行して全 PASS と所要時間の短縮を確認
する (d2 は typing・ストレージ・reset・cleanup の全経路を含む代表)。失敗時は個別に
原因を切り分け、安定性を優先して段階的に戻す。

## 4. 実装順序

1. S1〜S4 + 検証 (c1, b4, c3, d2)
2. H1, H3, H4 (既存関数への小追加) + d2/d3/c1 での回帰
3. U1, U2 (`make test`)、Q1 (`make test-qemu`)
4. H2 (MPX68KでFD1をGUI挿入する手動確認。XEiJのini添付では代替しない) — PASS
5. H5, H6, U3, Q2 — 実装・検証済み

ケースの実装・個別PASSは完了。反復測定・比較ハーネス (`tools/xeij-benchmark.py`) と host 回帰テストを追加済み。30回／10回の実測と、短回帰20%・フル30%の時間短縮およびp95判定は、統計的なリリース受け入れ測定として未実施。

## 5. 反復ベンチマークの実行

専用XEiJプロファイルを起動し、実際に使うJARとCGROMを環境変数で固定する。再起動を含むため、JAR/CGROMの絶対パスは必須。各runは結果JSONとログを保存し、失敗runがあれば比較を拒否する。既存runを上書きしないため、baseline/candidateや再測定ごとに異なる結果ディレクトリを指定する。

```sh
export SSOS9_XEIJ_JAR=/absolute/path/XEiJ.jar
export SSOS9_XEIJ_CGROM=/absolute/path/CGROM.dat
make test-emulator-benchmark-short EMULATOR_BENCHMARK_DIR=build/xeij/benchmarks/candidate
make test-emulator-benchmark-full EMULATOR_BENCHMARK_DIR=build/xeij/benchmarks/candidate
```

基準版の計測では上記2コマンドの `candidate` を `baseline` に置き換え、候補版と別の結果ディレクトリに保存する。

基準版・候補版を同じホスト、同じ元媒体、同じXEiJ JAR/ROM、同じケース集合で測る。shortは30回、fullは10回。比較ではsuite時間中央値の短縮がそれぞれ20%/30%以上、suite時間p95が悪化しないこと、session reuse部分のp95が悪化しないことを合格条件とし、再起動ケース時間は別集計する。

```sh
BASELINE_SHORT=build/xeij/benchmarks/baseline/short/candidate/benchmark.json \
CANDIDATE_SHORT=build/xeij/benchmarks/candidate/short/candidate/benchmark.json \
make test-emulator-benchmark-compare-short
BASELINE_FULL=build/xeij/benchmarks/baseline/full/candidate/benchmark.json \
CANDIDATE_FULL=build/xeij/benchmarks/candidate/full/candidate/benchmark.json \
make test-emulator-benchmark-compare-full
```

結果は自動判定だけで完了扱いにせず、測定条件・結果JSON・比較JSONを証跡として確認する。

## 6. 第二回カバレッジレビュー (2026-11-21)

第一回レビュー後の残ギャップ照合 (CLI 全機能×itest 実行、standalone のハード
状態保存復帰×検証項目、ストレージ機能表面×3層) に基づく追加。集計は「未検証
件数」でなく対象ごとに整理する (既通過経路・比較項目の不足・手順の自動化不足
を区別する)。

| ID | 内容 | 層 | 状態 (2026-11-21) |
| --- | --- | --- | --- |
| E1 | a2 の MFP 比較を保存・復帰対象の全項目へ拡張: IERA/IMRA/TACR/VR + Timer A/KEY/CRTC($138/$13C) ベクタを `mfpstate.x` と itest 抽出に追加。TADR は Timer A 動作カウンタのため除外 | XEiJ | **実行済み PASS** (2026-11-21、拡張 15 項目で 5 反復・全反復同一 snapshot) |
| M1 | H2 を `tools/mpx68k-h2.sh` (`make test-mpx68k-h2`) にランナー化: 11ゲート、Swift/Vision OCR による判定、EJECTFDD/SHA-256/fsck.fat の後片付け、exit 0/1/2/3 | MPX68K | **実装済み** (オフライン検証: イメージ生成・mtype バイト一致・OCR・UNVERIFIED/FAIL/ENVIRONMENT 経路。live 実行は未実施) |
| M2 | FAT12 マルチクラスタ実IOCS読み: `--module` で edit.s9m (3028 B = 3クラスタ) を配置し `load 1 edit.s9m`。登録時 CRC で全チェーン読取りを証明 (BG.S9M 282 B / console.s9m 351 B は単一クラスタで不適) | MPX68K | **実装済み** (ランナー G5。live 実行は未実施) |
| M3 | load 負例の実経路: `load 1 nosuch.s9m` (file or cluster chain エラー) と媒体なし (EJECTFDD 後、disk or FAT12 エラー)。ドライブ番号範囲外は unit で検査可能なため対象外 | MPX68K | **実装済み** (ランナー G9/G10。live 実行は未実施) |
| Q3 | t15 に mux シェルのライフサイクルを追加: ウィンドウ2で自分の子を `ss_cli_wait_pid` 経由で wait → ウィンドウ2だけ exit → 1・3 が応答継続 → ルート `mux` が zombie 回収+再生成。非対話 wait 分岐は `wait 2&` の unit で既カバーのため対象外 | QEMU | **PASS** (t15、17/17) |
| Q4 | 新 t17: CLI `smount`/`swrite`/`sread` を本番 human68k probe/attach 経路で接続 (t13/t14/t15 は mount 置換で probe バイパス、t16 は関数直叩き)。MS-DOS パーティション拒否の負例付き | QEMU | **PASS** (t17、17/17) |
| U4 | `ss_cli_execute_line` 直下メッセージ 4 ケース: Command too long (96/95 境界含む) / Too many arguments・Syntax error / 空行 / Unknown command の文言と exit status 127/2 | unit | **PASS** (native 55/55、ASan/UBSan でも 55/55) |
| E3 | c1 に `uptime`/`modules`/`clear`/`version` のスポット確認を追加 (help 表の全コマンドに実IOCS経路を1回以上与える) | XEiJ | **実行済み PASS** (2026-11-21。modules の期待値は `counter builtin` 行 — hello は run fallback で registry 画像ではない) |
| E2 | CLI エラー応答は unit (U4)、BS/←/DEL の実キー配信と行編集エコーは opt-in `c6`。実測: `typekey bs` は配信可、←/DEL は pipe に名前が無いが host key event (osascript) で IOCS 0x3B00/0x3D00/0x3700 が届く。当初見込みと異なり 0x7f は実キーでは発生せず DEL は scan 0x37 | unit+XEiJ | **実行済み PASS** (2026-11-21、行編集修正後のビルドで c6 PASS) |
| M4 | Ctrl-] (0x1d) prefix の実キー配信。処理自体は unit (U1) と QEMU t15 の `~` prefix でカバー済み。残る確認点は現行 MPX68K フォークに Ctrl 状態転送が入っているか | (観察) | 未実施 |

表現の修正: XEiJ の SCSI 512 バイトレコード→1024 論理合成経路は「現行構成では
XEiJ が論理 1024 を返すため未観測」と記録し、「原理的に不可能」とは断定しない。

統合時の検証: `zsh -n` 両スクリプト、`make test` (native、asm 266 checks、
link 364 checks + wrap 34)、`make test-qemu` (17/17) すべて成功。live XEiJ 実行は
a2 (MFP_CHECK 5 反復)・c1・c6 が 2026-11-21 PASS (下記行編集修正の前後で実施、
詳細とハッシュは `tests/README.md`)。H2 ランナーの live 実行のみ残件
(MPX68K 未起動のため)。

## 7. CLI 行編集の ←/→/DEL 対応 (2026-11-21、E2 の実機検証から発覚した修正)

ユーザー報告: Human68k プロンプトでは "abc" + ← + DEL で "ab" になるが、SSOS-9
は ← も DEL も受け付けない。実測 (XEiJ + console モジュール、osascript key code
で実キーイベントを送信) により、特殊キーは IOCS から scan<<8 で届くことが確認
できた: ←=0x3B00、→=0x3D00、DEL=0x3700 (mac の delete キーは X68000 BS=0x08 に
マップ)。旧 shell_loop は `0x20-0x7f` の範囲フィルタでこれらを破棄しており、
0x7f 分岐は実キーでは発生しない死に分岐だった。

修正: `ss_cli_edit_key()` (cli.c、cli.h) に cursor モデルの行編集を新設し、
shell_loop はこれに委譲。←/→ はカーソル移動、DEL はカーソル位置の文字を前方削除
(Human68k と同じ "abc"←DEL→"ab")、BS/0x7f はカーソル前方を削除。行途中への挿入・
削除は tail 再描画 + backspace でエコーし、mux バッファの `\b` 処理でも成立する。
QEMU 共通 stub に memmove を追加。

検証: unit 2 ケース追加 (エコーバイト列まで完全一致、57/57)、QEMU t15 は 16bit
キュー化して scan キーを実 shell_loop 経由で実行 (17/17)、XEiJ c6 は host key
event で ←/DEL を含む編集を実キー確認 (PASS、修正後ビルド)。
