# SSOS-9 受入テスト手順

更新日: 2026-11-21

この文書は、SSOS-9 の host/unit、QEMU、XEiJ、MPX68K 受入テストを一つの実行順にまとめたものです。実行結果はこの文書の末尾の記録票へ記入してください。実装済みテストの個別説明は本文に含め、実行スクリプトの正確なコマンドや詳細なデバッグ情報が必要な場合だけ関連文書を参照します。

リリース候補の通常実行には [README の Release Test](../README.md#release-test) の3ターゲットを使います。以下の手動準備コマンドは個別確認にも使えます。

## 1. 受入範囲と判定

受入対象は現在実装されている CLI-only SSOS-9 です。サポート対象は MC68000 16 MHz 以上、連続したメインRAM 12 MiB です。一般的なCランタイム、リダイレクト、未実装機能は受入対象に含めません。

受入は次の全ゲートを記録して判定します。

| ゲート | 必須実行 | 合格条件 |
| --- | --- | --- |
| G1: Host/unit・cross-build | `make verify` | 終了コード0。host/unit、assembler/linker golden、QEMU、ビルドと `verify-check` がすべて成功 |
| G2: XEiJ自動統合 | 26ケース全件 | すべて `pass`、終了コード0。a2はH1プローブ有効。設定と元媒体の実行前後ハッシュが一致 |
| G3: XEiJ目視 | C2右下セル | 画面表示を見て結果を記録。自動ログだけで代替しない |
| G4: MPX68Kフロッピー | `make test-mpx68k-h2` | 11ゲートすべてPASS。複数クラスタ読出し、負例、Human68k復帰、媒体整合性を確認 |
| G5: 性能比較 | baseline/candidate 各30 short・10 full | short中央値20%以上、full中央値30%以上短縮。suite p95とsession reuse p95が悪化しない |

G1〜G4が機能受入、G5はXEiJ統合テストの高速化を受け入れるゲートです。G5の比較用baselineがない場合、テストケースのPASSは報告できますが、統計的な高速化目標の達成とは報告しません。現在の計画資料でも30/10回の比較計測は未実施です。

XEiJ/MPX68Kの結果は物理X68000の実機合格を意味しません。実機ハードウェアでの最終確認は別ゲートです。XEiJでは本物のキーボード・フロッピーディスク媒体の動作も証明できません。

エミュレータの分担: G2・G3・G5はXEiJ、G4はMPX68K (MonitorSocketフォークビルド) で実施する。XEiJはテキスト画面コピーによる正確なアサーションと専用プロファイルによる媒体ハッシュ証跡を備える一方、実行中のFD差し替えは不安定。MPX68KはFD挿入とモニタソケット (`PAUSE` + `READ`/`REGS`/`TRACE`) による外部操作が可能だが、テキスト画面の読取経路がなくキー注入も前面化必須のため、26ケーススイートの置き換え先ではない。白箱検証は現行ゲート外の拡張候補で、導入する場合は別途計画する (操作方法は `docs/emulator-remote-operation.md`)。

## 2. 実行前の準備

### 2.1 作業ツリーとtoolchain

リポジトリルートで実行します。toolchainを設定し、作業ツリーの状態と対象revisionを記録します。

```sh
cd /Users/scott/repo/sokoide/ssos-9
git rev-parse HEAD
git status --short
source ~/.elf2x68k
```

作業ツリーに意図しない差分がある場合は、その状態も記録してください。`make verify` はcross-buildを行うため、`m68k-xelf-*` が利用可能である必要があります。

### 2.2 XEiJ環境

- XEiJ、Human68k、MC68000設定、RAM設定を記録します。CPU/RAMは上記最低要件を満たしてください。
- XEiJのJAR、CGROMの絶対パスを記録します。A3/H5の再起動ケースや性能測定では必要です。
- XEiJの操作に必要なmacOSアクセシビリティ権限と、画面コピー用の `pbcopy` / `pbpaste` を確認します。
- テストFIFOは `${TMPDIR}/XEiJControl` です。XEiJはテスト中1台だけ起動してください。
- 媒体書き込みを含む全自動試験では、必ず以下の使い捨てプロファイルを使います。元のini・ディスクイメージで直接試験しないでください。

プロファイルの作成は、元のXEiJを終了し、容量の大きい接続ディスクも複製できる空き領域を確保してから行います。ヘルパーは元のiniで参照されるFD/HD/SASI/SCSI媒体をprofile内へコピーし、HFS共有を使い捨てディレクトリへ向けます。

```sh
source ~/.elf2x68k
make all
profile_root=$(mktemp -d /tmp/ssos9-xeij.XXXXXXXX)
python3 tools/xeij-test-profile.py "$HOME/XEiJ.ini" ssos/build/export "$profile_root/run"
export SSOS9_XEIJ_INI="$(cd "$profile_root/run" && pwd -P)/XEiJ.ini"
export SSOS9_XEIJ_STAGE="${SSOS9_XEIJ_INI%/*}/hfs"
export SSOS9_XEIJ_JAR=/absolute/path/XEiJ.jar
export SSOS9_XEIJ_CGROM=/absolute/path/CGROM.dat
```

`SSOS9_XEIJ_JAR` と `SSOS9_XEIJ_CGROM` は実際のファイルの絶対パスに置き換えてください。続いてターゲット用テストfixtureをビルドします。

```sh
make -C tests/target
make build/xeij/mfpstate.x
cp build/xeij/mfpstate.x "$SSOS9_XEIJ_STAGE/"
```

作成されたiniを明示してXEiJを起動します。使用中の起動方法に合わせて `-ini "$SSOS9_XEIJ_INI"` を指定し、Human68kのプロンプトが出るまで待ってください。専用プロファイルと同じiniで起動したことを確認します。XEiJの通常操作はこのテスト中に行わないでください。

## 3. G1 — host/unit、golden、QEMU、cross-build

次を実行します。

```sh
source ~/.elf2x68k
make verify
```

この一つのtargetが次を実行します。

- cross-buildとXDF生成
- host/nativeテスト、Human68k volume/storage、FAT12、mux、editor、assembler/linker core
- assembler encoding goldenとlinker golden
- QEMU t01〜t17
- 変更差分に対する `verify-check`

Host/unitの対象はscheduler/context契約、native task/process/background builtins、FAT12/Human68k volumeとstorage integrity、mux入力経路、editor buffer/application、assembler parse/encode/object、linker wrap/CRCとhost coreです。assembler goldenは `m68k-xelf-as` とのencoding比較、linker goldenは独立mini-linkerおよび対応profileでGNU ldとの比較を行います。

QEMU t01〜t17の対象は次のとおりです。

| Test | 確認内容 |
| --- | --- |
| t01 | round-robin scheduling |
| t02 | context switch時のregister保存 |
| t03 | sleep/wakeup |
| t04 | main task登録 |
| t05 | Timer D cadence |
| t06 | idle task sleep |
| t07 | task終了・slot reap |
| t08 | waiting task cancel |
| t09 | arena split/coalesce |
| t10 | S9M1 image範囲・CRC検証 |
| t11 | module registry/reference/unload |
| t12 | interrupted process start frame |
| t13 | syscall契約、実assembler module起動、S9O1出力比較 |
| t14 | 実asm/ld module、linked program起動、FAT fallbackと負例 |
| t15 | editor/muxからasm→ld→runの統合フロー |
| t16 | Human68k volume/storageのsynthetic fixture |
| t17 | CLIのHuman68k SCSI mount・write・read |

QEMU側もproduction MFP interrupt handlerを実行するものではありません。

合格条件は、最後まで終了しexit code 0であることです。QEMUはproduction kernelのscheduler/arena/module/process codeとTimer D context-switch pathのQEMU portを検証します。production MFP割り込みハンドラ、IOCSコンソール、実X68000動作は検証しません。

失敗時は最初の失敗コマンドとログを保存し、後続ゲートの結果と混ぜずに報告します。

## 4. G2 — XEiJ全自動統合テスト

### 4.1 実行

2.2の使い捨てprofileでXEiJがHuman68kプロンプトにあることを確認し、リポジトリルートから実行します。

```sh
make test-emulator-automated
```

このtargetはH1を有効にして全26ケースを順番に実行します。H1だけを実行するときは、同じ準備の後に `make test-emulator-h1` を使います。結果は `build/xeij/results/` 以下に保存されます。ケースのログ、送信キー、画面記録、環境・成果物・元媒体・複製媒体のハッシュ、実行時間を確認してください。最初の失敗で後続は未実行になります。失敗のまま同じ変更媒体で再実行せず、新しい使い捨てprofileを作成してください。

合格条件:

1. コマンド終了コードが0。
2. `result.json` の全26ケースが `pass` で、`not_run` / `environment_error` / `fail` がない。
3. a2の `mfp_check` が `true` で、ログに基準値と5回の `A2 restored` がある。
4. `XEiJ.ini` と元媒体の実行前後ハッシュが一致。
5. 意図しないファイル変更が元環境にない。

26ケースと期待結果:

| ID | 確認内容 | 合格条件 |
| --- | --- | --- |
| a2 | Human68k `.x` 起動・終了を反復、MFP状態を比較 | 5回ともOS起動、終了メッセージ、Human68k復帰。Timer D mask、MFP状態、Timer D/TRAP #0/#13 vectorが基準値に復帰し、異常終了表示なし |
| a3 | XDF IPL起動とPID 1保護 | XDFからCLI起動し、`exit` が拒否されCLIが存続。ini復元を確認 |
| b1 | Timer D tick周期 | 約2.5秒でtick差分が500±100 |
| b2 | scheduler trace | `run counter` 中のtrace末尾で有効なscheduler eventと切替を確認しCLIへ復帰 |
| b3 | 割り込みpreemption | counterが進み、CLI入力に応答する |
| b4 | Ctrl-C実キー経路 | foregroundを中断・回収し、arena使用量がbaselineへ戻る |
| c1 | help出力 | 画面のコマンド一覧がhost期待値と一致 |
| c2 | 25行を超えるコンソール出力 | 折返し後も表示が崩れず、画面行が保持される。右下セルの目視はG3で実施 |
| c3 | F1/F2/F3 mux保持 | 各windowを切り替えて戻るとwindowごとの表示が保持される |
| c4 | F1/F2/F3 window選択 | ファンクションキーでwindowを選択でき、出力が保持される |
| c5 | console moduleのIOCS入力 | `CONSOLE.S9M` 起動後、キー入力が期待する `KEY: xxxx` として表示される |
| c6 | CLI行編集 | BS・左右・DELで短い入力を編集し、muxの折り返し前後のセルと実行結果を確認 |
| d1 | Human68k HDD create/read/replace | `A:\TEST` 内だけで作成・読取・置換し、期待内容を再読取できる |
| d2 | editor保存と再読込 | `A:\TEST` の新規ファイルを保存し、再度開いて内容が一致 |
| d3 | on-target assembler | 実asm moduleの出力objectがhost生成物とbyte一致 |
| d4 | linkerとrun fallback | linker出力がgoldenと一致し、未登録moduleのFAT fallback実行と終了状態を確認 |
| e1 | 3-window edit→asm→ld→run | editorで作成したsourceを複数asm、ld、runまで通し、期待出力・終了・reapを確認 |
| e2 | linker負例 | undefined symbolを拒否し、出力imageを作らない |
| e3 | builtin hello PIC起動 | `Hello` とstatus 0、shell復帰、trace eventを確認 |
| f1 | background builtin | `help&`, `ps&`, `mem&` が処理・回収され、出力とarenaが正常 |
| f2 | background出力中mux切替 | window切替中もbackground module出力が継続し、元windowに保持 |
| g1 | spawn/kill/wait反復 | 5サイクル後arenaがbaselineへ復帰 |
| h3 | window単位Ctrl-C | 非active windowのforegroundは中断されず、active windowからのCtrl-Cで中断 |
| h4 | sread境界 | 空ファイルは0 byteとして成功し、256 byte超は上限エラーで拒否 |
| h5 | XDF IPLからstorage読取 | fixtureを読め、PID 1のexitを拒否し、iniを復元 |
| h6 | 25回spawn/kill/wait soak | 25サイクル後arenaがbaselineへ完全復帰 |

ケースを個別に調べる場合は `make test-emulator-list` でIDを確認し、使い捨てprofileで次のように選択実行できます。

```sh
EMULATOR_TESTS="b1 c1" make test-emulator
```

ただしこの通常targetは `--require-profile` を指定しません。媒体に触れるケースを実行する場合は全自動targetを使い、profileの妥当性を検証させてください。

### 4.2 C2右下セルの目視 (G3)

C2のテキスト取得では画面右下セルが欠落するため、自動判定だけでは確認できません。使い捨てprofileのHuman68kから `F:\SSOS9` を起動し、CLIで `ps` を9回程度実行して最下行まで出力させ、XEiJ画面上で最終行・右端セルと再描画を確認します。右下セルが現行仕様どおり空白であること、他の行が誤って消去・スクロールしないことを記録してください。画面を写真またはスクリーンショットで保存できる場合は結果に添付します。

## 5. G4 — MPX68K FD1統合テスト (H2)

MonitorSocket付きMPX68KでHuman68kをFD0から起動し、`A:` のプロンプトを表示します。同じSSOS-9ビルドで次を実行します。

```sh
source ~/.elf2x68k
make all
make -C tests/target
make test-mpx68k-h2
```

11ゲートすべてのPASS、元・専用XDFのSHA-256前後一致、Human68kへの復帰を確認します。MPX68Kのバージョン、ゲート結果、成果物ハッシュ、画面証跡の保存先を記録します。前提と手順は [tests/README.md](../tests/README.md) を参照してください。

## 6. G5 — XEiJテストランナー高速化の性能受入

2.2と同じ使い捨てprofileでcandidate計測を行います。shortは30回、core 15ケース、fullは10回、全26ケースです。各runは成功した場合だけ次へ進みます。

```sh
make test-emulator-benchmark-short EMULATOR_BENCHMARK_DIR=build/xeij/benchmarks/candidate
make test-emulator-benchmark-full EMULATOR_BENCHMARK_DIR=build/xeij/benchmarks/candidate
```

比較には、同じホスト、XEiJ JAR/CGROM、OS/module artifacts、元媒体、同じケースIDのbaseline `benchmark.json` が別途必要です。高速化前ランナー（例: `0805ce6`）は通常の `$HOME/XEiJ.ini` と `~/tmp` を直接操作し、使い捨てprofileを検証しません。そのままbaselineとして実行しないでください。安全に実行できる互換baselineが用意されるまではcandidateの所要時間だけを記録し、G5を未判定にします。

baseline reportがある場合は次で比較します。比較ツールは30/10回、ケース一覧、環境identity、一貫した成功状態を検証します。

```sh
BASELINE_SHORT=build/xeij/benchmarks/baseline/short/candidate/benchmark.json \
CANDIDATE_SHORT=build/xeij/benchmarks/candidate/short/candidate/benchmark.json \
make test-emulator-benchmark-compare-short
BASELINE_FULL=build/xeij/benchmarks/baseline/full/candidate/benchmark.json \
CANDIDATE_FULL=build/xeij/benchmarks/candidate/full/candidate/benchmark.json \
make test-emulator-benchmark-compare-full
```

比較結果の合格条件は、short suite中央値20%以上短縮、full suite中央値30%以上短縮、suite p95非悪化、session reuse p95非悪化です。比較JSONとbenchmark.jsonを保存し、CPU負荷やホスト環境の差がないか確認します。ケースPASSだけでは高速化の達成としません。

## 7. 実行結果の記録票

この節をコピーして実測値を記入してください。

```text
Revision:
Working tree changes:
Date/time and timezone:
Host model / macOS:
Toolchain version:
XEiJ version / JAR SHA-256:
CGROM SHA-256:
Human68k version:
CPU / RAM setting:
Original ini SHA-256 before/after:
Original media SHA-256 before/after:
Disposable profile path:

G1 make verify: PASS / FAIL
  Exit code:
  Log path:
  Failed target (if any):
G2 XEiJ 26-case suite: PASS / FAIL
  Exit code:
  result.json path:
  Pass / fail / environment_error / not_run counts:
  Modified disposable files:
G3 C2 visual check: PASS / FAIL
  Screen evidence path / observation:
G4 MPX68K H2: PASS / FAIL / NOT RUN
  MPX68K version:
  console.xdf SHA-256 before/after:
  Observed KEY output / exit status / Human68k return:
G5 benchmark: PASS / FAIL / NOT RUN
  Baseline short/full report paths:
  Candidate short/full report paths:
  Comparison JSON paths:
  Median reduction short/full:
  Suite p95 baseline/candidate:
  Session reuse p95 baseline/candidate:

Physical X68000 tested: YES / NO
Notes / deviations:
```

## 8. 関連する詳細資料

- `tests/README.md`: host/QEMUの対象範囲、手動fixture作成、MPX68K H2の過去実績
- `docs/emulator-integration-test-plan.md`: XEiJ各ケースの内部手順、個別の判定実装
- `docs/emulator-remote-operation.md`: XEiJ制御FIFO、プロファイル、画面コピーとトラブルシューティング
- `docs/test-coverage-and-speedup-plan.md`: 追加テストの背景と高速化計画の測定基準
- `AGENTS.md`: 製品範囲と検証境界
