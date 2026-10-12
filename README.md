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
- `clear`: IOCS テキスト画面を消去
- `version`: OS 名を表示
- `exit`: `.x` は Human68K へ戻る。`.xdf` は初期プロセスの終了を拒否する

## 現在の実装範囲

実装済みなのは単一 CLI プロセスとカーネル worker task です。`.xdf` で親プロセスまたは最小 PID へ戻る動作には、プロセス表・プロセス生成・切替が必要で、未実装です。

将来の OS-9 型ローダーに必要な Module Table、物理メモリアロケータ、Process Table、PIC/A6 ABI、`load_module()` / `link_module()` / `spawn_process()` はまだありません。worker task は独立プロセスではありません。

## 検証の範囲

クロスビルド成功は、コンパイル・リンク・XDF 生成の確認です。Timer D/MFP 割り込み、IOCS コンソール、Human68K 上での起動、実機・エミュレータ動作は別途確認が必要です。
