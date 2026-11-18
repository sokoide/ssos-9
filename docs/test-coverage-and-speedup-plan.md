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
