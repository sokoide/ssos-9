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

現在は PID 1 の CLI、heartbeat/idle/reaper の kernel task、S9M1 validator/registry、
private data/GOT/fixup、Process Table と内蔵 PIC counter の複数プロセス実行を実装しています。
`ps` / `modules` / `mem` で private 領域、共有コード、参照数、空き arena を観察できます。
外部 `.s9m` file loader、GCC C module runtime、fd/redirect は未実装です。

機能と学習手順は [ルート README](../README.md)、責務と ABI 方針は
[全体設計](../docs/design.md)、拡張順序は [ローダー計画](../docs/multiprocess-loader-plan.md)を参照してください。
