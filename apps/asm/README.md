# asm: MC68000 assembler module

`asm` は SSOS-9 上で動作する S9M1 アセンブラ。MC68000 整数命令を扱い、
Phase 4 リンカ向けの S9O1 オブジェクトを FAT16 に生成する。構文、命令、
疑似命令、オブジェクト形式、再配置の契約は [docs/assembler-contract.md](../../docs/assembler-contract.md) を参照。

## 使い方

```text
run asm SRC [DST]
```

`SRC` をアセンブルし、`DST` (省略時は `SRC` の拡張子を `.O` に変えた名前)
へ S9O1 オブジェクトを書く。最初のエラーで `SRC:行: メッセージ` を表示して
終了コード 1 で止まる。

## 構成

- `asm.h`: 共有インターフェース。
- `lex.c` / `parse.c`: 字句解析、構文解析、式評価、ニーモニック表。
- `encode.c`: 命令サイズ確定とエンコード、再配置生成。
- `obj.c`: S9O1 シリアライズ。
- `ctx.c`: コンテキスト、シンボル、エミット、再配置テーブル。
- `assemble.c`: 2 パス駆動。S9M1 版とホスト試験で共用。
- `main.c`: S9M1 CLI。
- `support.c`: `memcpy` と libgcc 相当の 32 bit 乗除算ルーチン。-Os が
  生成する呼び出しを、S9M1 モジュールが持てない絶対再配置を含む libgcc
  オブジェクトに依存せず自前実装で賄う。

モジュールは静的領域を持てないため、作業領域はすべてスタック上
(stack size 65532) に確保する。

## ビルド

クロスツールチェーンを設定した上で、リポジトリ根源から `make all`、または
`make -C apps/asm all`。`build/asm.s9m` と `build/asm.xdf` が生成される。
`-Os` でビルドし、S9M1 モジュールの .text 32768 byte 上限は
`tools/mkmodule.py` が検査する。

## 検証

- ホスト単体試験: `make -C tests test-asm-host` (構文 341 checks、エンコード
  266 checks、オブジェクト出力、既定出力名の 8.3 上限)。`tests/build/asm-host`
  も生成し、ゴールデン試験と QEMU 試験で共用する。
- クロス比較: `make -C tests test-asm-golden`。`tests/asm/cases` の同一ソース
  を `m68k-xelf-as` とアセンブルし text バイト列を比較する (正例 8・負例 7)。
  GAS 側の定義済みシンボル再配置を契約のフィールド規則へ折り畳んで比較する。
  binutils 2.46 の GAS は `$` 16 進定数を未定義シンボル化し、前方参照の
  `.s`/`.b` 分岐を受理しないため、正例は `0x` 表記と後方短分岐のみを使う。
  事前に `source ~/.elf2x68k` が必要。
- QEMU: `make -C tests test-qemu` の t13 で、実 S9M1 `asm` が FAT16 上の
  `PROG.S` をアセンブルし、出力 `PROG.O` をホストの `asm-host` 出力と
  バイト比較する。実機での実行は未実施。
