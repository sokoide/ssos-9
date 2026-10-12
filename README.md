# SSOS-9

X68000 向けの最小 CLI 専用 OS です。`ssos-68k`の起動経路と Timer D 割り込みによるプリエンプティブ・スケジューラを基にしています。対応するスケジューラは preemptive のみです。

## ビルドとインストール

```sh
source ~/.elf2x68k
make
make install
```

`make install` は `ssos9.x` と `ssos9.xdf` を生成し、`~/tmp` にコピーします。生成物は Git 管理対象外です。

- `ssos9.x`: Human68K から起動する CLI
- `ssos9.xdf`: IPL から起動する XDF イメージ

## CLI

- `help`: コマンド一覧
- `uptime`: カーネル worker の起床回数
- `debug`: tick、task stack 残量、MFP Timer D 設定を表示
- `trace`: 直近 16 件の task create/start/sleep/wake/switch/exit を表示
- `clear`: IOCS テキスト画面を消去
- `version`: OS 名を表示
- `exit`: `.x` は Human68K へ戻る。`.xdf` は初期プロセスの終了を拒否する

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

実装済みなのは単一 CLI プロセスとカーネル worker task です。`.xdf` で親プロセスまたは最小 PID へ戻る動作には、プロセス表・プロセス生成・切替が必要で、未実装です。

将来の OS-9 型ローダーに必要な Module Table、物理メモリアロケータ、Process Table、PIC/A5 GOT ABI、`load_module()` / `link_module()` / `spawn_process()` はまだありません。worker task は独立プロセスではありません。

[マルチプロセス・ローダー実装計画](docs/multiprocess-loader-plan.md)に、共有 PIC、process lifecycle、background 起動、syscall、将来の FAT と stdout の`>`/`>>` redirect の設計と実装順序をまとめています。

## 検証の範囲

クロスビルド成功は、コンパイル・リンク・XDF 生成の確認です。Timer D/MFP 割り込み、IOCS コンソール、Human68K 上での起動、実機・エミュレータ動作は別途確認が必要です。
