# SSOS-9

このディレクトリは X68000 向け最小 CLI OS のクロスビルドを行います。スケジューラは Timer D 割り込みによる preemptive 方式のみです。

## ビルド

リポジトリルートで `source ~/.elf2x68k && make install` を実行すると、`ssos9.x` と `ssos9.xdf` を生成し、`~/tmp` にコピーします。個別にビルドする場合は次の通りです。

```sh
source ~/.elf2x68k
make -C ssos
```

## 構成

- `boot/`: XDF IPL
- `os/`: カーネル、preemptive scheduler、CLI
- `standalone/`: Human68K 起動用 `.x` と終了処理

現在の OS は単一 CLI プロセスとカーネル worker task で構成されます。OS-9 型のモジュールローダーや複数プロセスは未実装です。詳しくはリポジトリルートの README を参照してください。
