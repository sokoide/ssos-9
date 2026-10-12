# SSOS-9

共有 PIC コードとプロセス専用データ、割り込み、syscall、資源回収を CLI で観察する、X68000 向けの教育用 OS です。OS-9 型の module/process 分離を参考にしています。既存 OS-9 バイナリや syscall との互換性はありません。`ssos-68k`の起動経路と Timer D 割り込みによるプリエンプティブ・スケジューラを基にしています。対応するスケジューラは preemptive のみです。
動作条件は 16MHz 以上の MC68000 と 12MiB の連続 main RAM です。

## ビルドとインストール

```sh
source ~/.elf2x68k
make all
make install
```

`make install` は `ssos9.x` と `ssos9.xdf` を生成し、`~/tmp` にコピーします。生成物は Git 管理対象外です。

- `ssos9.x`: Human68K から起動する CLI
- `ssos9.xdf`: IPL から起動する XDF イメージ

## CLI

- `help`: コマンド一覧
- `uptime`: カーネル worker の起床回数
- `run counter`: 内蔵 PIC counter を foreground 起動。Ctrl-C/Break で終了要求を出し、回収後に prompt へ戻る
- `run counter &` / `run counter&`: 内蔵 PIC counter をバックグラウンド起動
- `ps`: PID、親 PID、状態、共有 module、private data/GOT のアドレスを表示
- `modules`: module ID、名前、共有 image/code/entry アドレス、image サイズ、参照数を表示
- `mem`: arena の空き byte 数と最大の連続空き block を表示
- `kill PID`: 子 process を停止
- `wait PID`: 子 process の回収と終了状態を待つ。Ctrl-C/Break は待機だけを取り消し、子は実行を続ける
- prompt での Ctrl-C/Break: 入力中の行を破棄して prompt を再表示
- `debug`: tick、task stack 残量、MFP Timer D 設定を表示
- `trace`: 直近 16 件の task create/start/sleep/wake/switch/exit を表示
- `clear`: IOCS テキスト画面を消去
- `version`: OS 名を表示
- `exit`: `.x` は Human68K へ戻る。`.xdf` は初期プロセスの終了を拒否する

末尾の `&` は全コマンドで共通に解析し、直前の空白は不要です。
`run counter&` は module process を起動し、`help&`、`ps&`、`modules&` などの
組み込みコマンドは独立した子 process で実行します。親 CLI は PID を表示して戻ります。
`exit&` はその子だけを終了します。`wait PID&` は親 CLI の子を引き継がないため、
親が起動した PID を回収できず終了値 127 を返します。背景の子も `wait PID` で回収します。
IOCS は短い software lock で直列化しますが、背景出力後の入力行再描画は未対応です。
行途中の `&`、`&&`、`&` だけの入力は構文エラーです。

## 学習の進め方

まず [全体設計](docs/design.md)で task/process/module の違いを確認します。
現行 module は supervisor mode の同一物理アドレス空間で動き、private data の分離は
メモリ保護を意味しません。

```text
modules
mem
run counter &
run counter &
ps
modules
mem
```

同じ MODULE ID を持つ 2 process の DATA/GOT/STACK が異なることと、`modules` の
共有 CODE/ENTRY が一組で REFS が 2 になることを観察します。`ps` が返す READY は
process の生存状態であり、scheduler の実行中/sleep の区別ではありません。
表示された各 PID について `kill PID`、`wait PID` を実行し、REFS が 0、`mem` の値が
起動前の値へ戻ることを確認します。kill の受付、reaper の資源回収、wait の PID slot 回収は
別の段階です。終了した子を wait しないと process table の slot が残ります。
内蔵 counter は値をコンソールへ出力しません。まずアドレスと参照数を観察する教材です。

| 学ぶ概念 | 読む実装 | 自動検証 |
| --- | --- | --- |
| queue、sleep、idle、task 回収 | `ssos/os/kernel/scheduler.c` | native と QEMU T01〜T08 |
| first-fit、分割/結合 | `ssos/os/mem/arena.c` | QEMU T09 |
| image の検証、共有 ref、private fixup | `ssos/os/module/` | QEMU T10〜T11 |
| SR/PC frame、A3/A5、共有 code と private data | `ssos/os/kernel/context_frame.h`, `ssos/os/process/` | QEMU T12〜T13 |
| trap、kill/wait、孤児、Ctrl-C | `ssos/os/kernel/syscall*`, `ssos/os/app/cli_wait.c` | QEMU T13 |

`modules` は各行の snapshot、`mem` は一組の snapshot です。出力中にも process は進行します。
外部 module、C runtime、fd/redirect、FAT の学習は [ローダー計画](docs/multiprocess-loader-plan.md)の
各フェーズで追加します。計画のコマンド例には未実装機能が含まれます。

## デバッグ

`make debug` は `.x` の逆アセンブル、ELF シンボル、link map を
`ssos/standalone/build/` に出力します。Human68K の `.x` はロード時に
配置が変わるため、実行時 PC を ELF 上のアドレスと比較するにはロード基点が
必要です。実行中は `debug` で scheduler/MFP 状態を、`trace` で直近の
task lifecycle と切替履歴を確認できます。

`エラー($004F)` が再発する場合、`$4F` は MFP vector base `$40` に
GPIP7 の channel 番号 `$0F` を加えた割り込み vector です。旧初期化は MFP 全 source を
enable/unmask していたため、Timer D 以外の source も Human68k 側へ流していました。
現行版は既存 mask を維持し Timer D だけを追加します。

### MFP Timer D の設定

プリエンプティブ切替には MC68901 MFP の Timer D を使います。`ss_set_interrupts()` は
割り込みを一時禁止してから元の割り込み状態を保存し、MFP の vector base を `$40` に
設定します。MFP の割り込み vector は base に channel 番号を加えた値です。GPIP7 は
`$40 + $0F = $4F` なので、未処理の GPIP7 割り込みを有効にしないことが重要です。

| MFP register | アドレス | このOSでの扱い |
| --- | --- | --- |
| VR | `$E88017` | vector base `$40` を設定し、既存の EOI mode を保持 |
| IERB | `$E88009` | Timer D enable bit 4 だけを追加 |
| ISRB | `$E88011` | Timer D in-service bit 4 をクリア |
| IMRB | `$E88015` | Timer D mask bit 4 だけを追加 |
| TCDCR | `$E8801D` | Timer C の上位 nibble を保持し、Timer D を停止後に設定 |
| TDDR | `$E88025` | Timer D の data に 100 を設定 |

Timer D は prescaler 200、data 100 で約 200 Hz（5 ms 間隔）に設定します。
割り込みハンドラは 10 tick ごと（約 50 ms）に scheduler 切替を行います。初期化では
Human68K の他の MFP source の enable/mask 状態を変更しません。MFP の割り込み設定と
ハンドラ動作は実機または対応エミュレータでの確認が必要です。

## 現在の実装範囲

現在は CLI と heartbeat/idle/reaper の kernel task、S9M1 validator/Module Table、private data/fixup、Process Table、spawn/kill/wait、内蔵 PIC counter を実装しています。`run counter &`で同一コードを複数 process として起動し、各 process の A5/GOT、data、stack を`ps`で観察できます。`ssos/include/ssos9/syscall.h`に初期`trap #0` ABI を定義し、QEMU で trap 入口、version、PID/親 PID、process 情報/list、child kill/wait を確認します。任意の`.s9m`ファイルを読む loader、GCC C module runtime、user 向け wrapper、file I/O/redirect は未実装です。kernel worker task は独立 process ではありません。

[全体設計](docs/design.md)は責務境界と ABI 変更方針を定めます。[マルチプロセス・ローダー実装計画](docs/multiprocess-loader-plan.md)に、共有 PIC、process lifecycle、background 起動、syscall、将来の FAT と stdout の`>`/`>>` redirect の設計と実装順序をまとめています。

## 検証の範囲

### テストコマンド

- SSOS-9 の機能変更では、基本的に `make test` と `make test-qemu` を変更・実行します。変更箇所に合わせて native と統合テストへケースを追加してください。
- `make test`: host 上の native C unit test。scheduler の純粋な状態遷移を速く検証し、68000 命令や割り込み経路は実行しません。
- `make test-asm`: QEMU で独立した 68000 assembly の基本例を実行します。主に例題と実行環境の確認用で、通常の OS 機能変更では対象にしません。assembly 例題やその実行環境を変更した場合に実行します。OS の scheduler/process 統合は検証しません。
- `make test-qemu`: QEMU `m68000` 上で実際の scheduler/process C code と context-switch assembly を検証します。Timer D の代替は同期 trap のため、実 MFP 非同期割り込みや Human68K/IOCS 実機動作は検証しません。
- `make verify`: native、assembly、QEMU の全テストと `.x`/`.xdf` クロスビルドを実行します。

クロスビルド成功は、コンパイル・リンク・XDF 生成の確認です。Timer D/MFP 割り込み、IOCS コンソール、Human68K 上での起動、実機・エミュレータ動作は別途確認が必要です。
