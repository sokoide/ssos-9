# SSOS-9: OS-9型マルチプロセス・ローダー実装計画

状態: 実装中。S9M1 validator/registry、private data fixup、合成 SR/PC frame、Process Table、built-in PIC counter の spawn/kill/wait と CLI コマンドを実装。
外部 module loader/packer、GCC C runtime、syscall ABI、VFS/redirect、FAT は未実装。

対象: 16MHz 以上の MC68000 と 12MiB RAM を必須とする X68000、CLI 専用、Timer D による preemptive scheduling。
`.x`と`.xdf`の両方で同じ process/module/syscall コードを使う。

## 1. 到達点と設計上の決定

初版で、同じ PIC モジュールを複数プロセスとして起動し、コードを共有しながら
プロセスごとの変数とスタックを分離する。shell からバックグラウンド起動、一覧、
停止、終了待ちを操作し、A3/A5/SP、カウンタ、参照数を観察できるようにする。

OS-9/68K の共有モジュールと独立データ領域という構成を取り入れる。
OS-9 の既存バイナリやモジュールヘッダ、syscall 番号との互換は初版の対象外。
SSOS-9 独自の小さな形式を使い、OS-9 互換形式への変換・対応は別の拡張とする。
2026-10-12 の設計更新で、C cross compiler への適合を優先し、A6/data bias 案を
A5/private GOT 方式へ変更した。共有 module と process 専用 data という OS-9 型の構成は維持する。

| 項目 | 初版の決定 |
| --- | --- |
| アドレス空間 | 全プロセスが同じ物理RAMを使う。MMUによるメモリ保護なし |
| 実行単位 | 1 process = 1 scheduler task。process内の複数threadは将来対応 |
| 実行モード | 信頼済みモジュールをsupervisor modeで実行 |
| コード | 再配置なしのPIC、同じモジュールのcode/rodataを共有 |
| データ | 各processに初期化data、BSS、引数、stackを割り当てる |
| ABI | A3 = module base、A5 = private GOT base、A6 = Cの通常用途 |
| 外部機能 | モジュールからはSSOS-9のsyscallを使用 |
| ローダー | 組み込みmodule imageから開始し、VFS経由のファイル読み込みへ拡張 |
| I/O | processのfd → open-file object → device/file backend |
| リダイレクト | `>`は作成・切り詰め、`>>`は各write時に末尾へ追記 |

supervisor mode を選ぶのは、現行 context switch が各 task の単一 SP 上にある
レジスタ退避と例外フレームを復元する構造だからである。user mode 化には
process ごとの USP/SSP、kernel stack、trap/interrupt entry の作り直しが必要になる。
初版の分離は所有権と ABI 上の約束であり、誤ったポインタや直接 I/O を防止できない。
ユーザモード移行は syscall 契約を維持したまま別フェーズで行う。

## 2. 現行コードとの差分

| 現行の場所 | 現在の制約 | 必要な変更 |
| --- | --- | --- |
| `ssos/os/kernel/scheduler.c` | TERMINATED taskのslot/stackを再利用しない | cancel、wait解除、別stackからの回収を追加 |
| `ssos/os/kernel/main_task.c` | 初期shellのTCBは`tcb_table`の外 | PID 1のprocess descriptorと明示的に対応付ける |
| `ssos/os/kernel/preemptive/interrupts.s` | 新規taskはCの`entry(arg)`から開始 | PIC process用の初期レジスタフレームを復元する経路を追加 |
| `ssos/os/kernel/context_frame.h` | yield用frameはPC→SRの順 | Timer D/trap用のSR→PC順のframeも定義 |
| `ssos/os/app/cli.c` | 入力行と組み込みコマンドだけを処理 | tokenize、spawn、wait、fd設定、process観察を追加 |
| `ssos/os/kernel/linker.ld` | arenaを使わずSSOSRAMを予約 | 12MiB必須条件に合わせてarena終端を`$C00000`へ固定 |
| `ssos/standalone/main.c` | `.x`終了時にMFPとmodeを復元 | 全childの終了・回収後に既存の復元経路へ戻る |

初期 shell を PID 1 として登録し、kernel heartbeat/idle/reaper は PID を持たない
kernel task として表示上も区別する。PID と既存 task ID を同じ番号と仮定しない。
PID 1 は常駐する kernel shell を包む system process で、初版では PIC image ではない。
`module_id=0`とし、`proc 1`では loader 由来の data/GOT を`n/a`と表示する。
bootstrap TCB の`stack_base == 1`という sentinel を実 stack 境界として表示しない。
実際の root stack 範囲は platform/bootstrap から descriptor へ記録する。
初版は最大 8 process（shell を含む）、scheduler slot は child と kernel task の分を
別途見積もり、少なくとも 10 slot を確保する。kernel task の既存 pool stack と
allocator が確保する process stack を二重に解放しない。
この変更では C の`SS_MAX_TASKS/SS_TASK_STACK`と linker の pool 予約量を同じ build 定義から
生成し、現在の linker にある`8 * 4096`のまま slot 数だけを増やさない。

root と通常 child の priority は既定 8、idle は最低 priority、reaper は既定 8 とする。
reaper を idle と同じ低 priority にして、常に READY な shell に回収を妨げられないようにする。

現行の「ready task がなくなる場合は sleep を拒否する」制限も解消する。
idle task を常設し、全 process が sleep/wait 中でも Timer D を受け付ける。
bootstrap は BSS 初期化、arena、scheduler/process table、PID 1 と idle/reaper、
module registry と syscall vector の準備後に Timer D を有効化する。
現行`ss_set_interrupts()`の設定と CPU 割り込み許可を分離し、未初期化 queue を
ISR へ公開しない。`.x`では最初に supervisor へ移行し、旧 vector/MFP 状態を保存する。

## 3. Module Tableとモジュール形式

### 3.1 Module Table

固定長の table を最初に実装する。最大 8 module、名前は ASCII 1〜31 byte、
大文字小文字を区別し、同名の異なる内容を同時に登録しない。

| field | 意味 |
| --- | --- |
| `module_id` | 内部handle。再利用時のgenerationを含む |
| `name` | 検証・コピー済みのmodule名 |
| `image_base`, `image_size` | 共有するimage全体の所在とサイズ |
| `code_base`, `code_size`, `entry` | 実行領域とentry address |
| `init_offset`, `init_size`, `bss_size` | private dataの初期化情報 |
| `got_offset`, `got_size`, `fixups` | private data内のGOTと、spawn時のpointer初期化情報 |
| `stack_size` | 各processのstack要求量 |
| `ref_count` | processと明示的linkが保持する参照の合計 |
| `storage` | 組み込みimage、またはallocator所有image |
| `state` | LOADING / READY / UNLOADING |

code を読み込んだだけでは process を作らない。table の登録が image を保持し、
`ref_count == 0`でも明示的な`unload`まで残す。自動 cache/eviction は初版に入れない。
`unload`は参照数 0 の動的 module だけを解放する。組み込み module の物理領域は
kernel image が所有するため解放しない。

### 3.2 SSOS-9 module image v1（`.s9m`）

wire format を明示し、C struct をそのままファイルへ書かない。
全整数は big endian、header は 80 byte、予約領域は 0 とする。

| offset | size | field |
| --- | --- | --- |
| `$00` | 4 | magic `S9M1` |
| `$04` | 2 | ABI version = 1 |
| `$06` | 2 | header size = 80 |
| `$08` | 4 | flags: PIC、REENTRANT。v1では両方必須 |
| `$0C` | 4 | image size |
| `$10` | 4 | code/rodata offset |
| `$14` | 4 | code/rodata size |
| `$18` | 4 | entry offset（image先頭から） |
| `$1C` | 4 | initialized data offset |
| `$20` | 4 | initialized data size |
| `$24` | 4 | BSS size |
| `$28` | 4 | stack size |
| `$2C` | 4 | name offset |
| `$30` | 2 | name size（終端NULを含まない） |
| `$32` | 2 | reserved |
| `$34` | 4 | CRC32。計算時はこのfieldを0とする |
| `$38` | 4 | GOT offset（private data先頭から） |
| `$3C` | 4 | GOT size |
| `$40` | 4 | fixup table offset（image先頭から） |
| `$44` | 4 | fixup count |
| `$48` | 8 | reserved |

CRC は標準 IEEE CRC32 で image 全体を対象にする。反転 polynomial `$EDB88320`、
初期値/final XOR は`$FFFFFFFF`とする。破損検出用であり認証ではない。
OS-9 の CRC 形式とは別仕様である。

loader は次を検証し、失敗時は table へ公開しない。

- magic/version/flags/予約領域が既知であること。
- image の実際の長さと宣言長が一致し、header を含む全領域が範囲内にあること。
- `offset + size`の overflow を防ぎ、非空の各領域が互いに重ならないこと。
- code/entry が偶数 address で、entry が code 領域内にあること。
- 名前が有効な ASCII 名で、埋め込み NUL や path separator がないこと。
- data + BSS が 64KiB 以下、stack が 4KiB 以上かつ 4 byte 倍数であること。
- v1 の code/rodata が 32KiB 以下であること。PC 相対参照の各変位は build 時にも検証する。
- resource 上限、CRC、同名登録の一致を確認すること。
- GOT が initialized data 内にあり、各 GOT displacement が signed16bit の範囲内にあること。
- fixup table の全 record、target、referenced offset と重複 target を検証すること。

コードはロード・spawn 時に書き換えない。GOT と pointer を含む initialized data は
process へコピーし、その private copy だけに fixup を適用する。可変変数へのアクセスは
`A5 → private GOT slot → private data`で行う。ASM も同じ ABI を使う。
PIC/reentrant flags だけで機械語の安全性は証明できないため、build 時の ELF relocation
検査と逆アセンブル確認を必須にする。GCC の`-fPIC`だけでこの data 分離が完成するとは扱わない。

### 3.3 load/link/spawnの分離

```c
int32_t load_module(const char *path);      /* register/find image; module_id */
int32_t link_module(const char *name);      /* acquire one ref; module_id */
int32_t unlink_module(uint32_t module_id);  /* release caller-owned ref */
int32_t unload_module(const char *name);    /* require ref_count == 0 */
int32_t spawn_process(const SSUserSpawn *request); /* child PID */
```

上記はユーザ向け wrapper の名前。kernel 内は`ss_module_*`、`ss_process_*`とする。
`load_module()`は既に同じ image が登録されていれば再利用するが、ref を増やさない。
`link_module()`は呼出 process の link table へ記録し、exit 時の自動 unlink 対象にする。
v1 の link は lifetime handle の取得までとし、共有 subroutine 呼び出し ABI は後続で定義する。
`spawn_process()`は実行用 ref を独立して 1 つ取得し、caller の link ref を消費しない。

初版の`spawn counter`は登録済み module 名を解決する。登録がなければ
`/rom/mod/counter.s9m`を load して再試行する。`load /path/file.s9m`は明示的な path 指定。
最初は組み込み image を ROM filesystem から読み、後から同じ API で FAT の file を読む。
I/O 中は LOADING 状態の予約 entry だけを持ち、IRQ 禁止区間で file 読み込みを行わない。
同時 load は同名の予約を検出して WAIT へ入り、公開完了または失敗通知を待つ。

### 3.4 Cross CompilerによるCプログラムとA5/GOT ABI

C 対応を初版の実装フェーズに含める。標準の m68k GCC が対応する A5/GOT 方式を
採用し、OS-9/68K の A6 bias 方式を GCC へ強制しない。A5 は単なる data 領域の先頭ではなく、
process 専用の Global Offset Table を指す。GOT には変数・定数・関数への pointer を置く。
共有 code から同じ GOT slot を参照しても、process ごとに A5 と変数の pointer が異なるため、
通常の C global/static 変数を独立させられる。A6 は frame pointer に利用できる。

| compile方式 | 生成される参照 | 本計画での判断 |
| --- | --- | --- |
| `-fpic` / `-fPIC`のみ | PCからGOTを求めてA5へ設定 | codeとdataの相対配置を前提にするため、process専用GOTへ切り替える用途には不足 |
| `-mpcrel` | codeだけでなくglobal変数もPC相対 | codeを共有すると可変globalも同じ場所を指すため、標準のprocess ABIには採用しない |
| `-fPIC -msep-data` | 呼出環境が設定したA5からGOT参照 | 本計画の標準。loaderがprivate GOTを構成する |
| A6固定の独自GCC改造 | OS-9風のdata直接参照 | compiler保守が必要なので初版では行わない |

#### 3.4.1 ローカルtoolchainで確認したこと

2026-10-12 時点の`m68k-xelf-gcc (elf2x68k) 13.4.0`で、小さな C コードを
`-m68000 -O2 -fPIC -msep-data -ffixed-a3`で compile し、ELF link と逆アセンブルを確認した。
`unsigned counter`の increment は次の形になった。

```asm
move.l counter@GOT(%a5), %a0
move.l (%a0), %d0
addq.l #1, %d0
move.l %d0, (%a0)
```

通常の`-fpic`では関数内に`lea (_GLOBAL_OFFSET_TABLE_@GOTPC,%pc),%a5`が入る一方、
`-msep-data`ではその初期化がなく、process entry で与えた A5 を使うことを確認した。
GOT を使いながら A6（assembler では`%fp`）で stack frame を作る関数も compile できた。
これは code 生成と小さな ELF の link 確認であり、SSOS-9 上の C module 実行は未確認。
link 試験の`ss_write`は 0 を返す仮 stub で解決しており、実際の syscall 呼出しを検証したものではない。

#### 3.4.2 Module buildと最小runtime

```sh
source ~/.elf2x68k
m68k-xelf-gcc -m68000 -msoft-float -O2 \
  -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
  -fPIC -msep-data -ffixed-a3 -fno-jump-tables \
  -I ssos/include -c counter.c -o counter.o

# 以下のmodule.ld/start.o/syscall.o/mkmodule.pyは実装する成果物。
m68k-xelf-ld --emit-relocs --no-relax -T module.ld \
  start.o counter.o syscall.o -o counter.elf
python3 tools/mkmodule.py counter.elf --name counter -o counter.s9m
```

Human68K 用`.x`へ変換する通常の link 経路を使わず、`.o → ELF → .s9m`の専用 pipeline にする。
int/long/pointer は 32bit、固定長 field は`stdint.h`の型を使い、`-mshort`を使わない。
A5 を別用途に使う inline ASM は ABI 違反。A6 には`-ffixed-a6`を付けない。
`-mpcrel`もこの profile には混ぜない。

start stub は D0=argc/A0=argv を C の stack 引数に変換し、PIC な`main(argc, argv)`を呼ぶ。
main が戻ったら status を`exit()` wrapper へ渡す。syscall wrapper は C の stack 引数から
request を作り、D0/D1 で`trap #0`を実行し、C ABI の callee-saved register を守る。
kernel 側も trap/Timer D で A5 を保存・復元する。

初版 runtime は write/read/sleep/getpid 等の wrapper、`write_all`、小さな string/memory/
整数出力関数までとする。Human68K の libc、IOCS library、通常の crt0 はリンクしない。
compiler が要求した`memcpy`や libgcc helper も同じ PIC/sep-data ABI で実装・build・検査する。
未検証 helper を自動で既存 archive から取り込まない。浮動小数点、TLS、constructor、
C++ exception、dynamic linking、標準 stdio/malloc は初版の対応範囲から外す。
未知の undefined symbol や未対応 section/relocation を build error にする。

#### 3.4.3 Linkerとpackerの責任

link 時の仮想配置は code/rodata と private data を別領域にする。
例えば code VMA=0、data VMA=`$10000`として link し、packer はこの仮想 address を
実 RAM address として保持せず、image offset または private data offset へ変換する。
code/rodata の相対配置と padding は維持し、packer が個別 section を詰め直して
link 済み PC 相対変位を壊さないようにする。

private initialized data には`.got.plt`の予約部分、`.got`、`.data*`を収め、BSS を続ける。
最初の linker script では`.got.plt`を`.got`より前に置き、A5 が指す
`_GLOBAL_OFFSET_TABLE_`と最終 GOT displacement の対応を検査する。
ローカルの小さな link 実験では順番を逆にすると、この symbol と GOT 参照の起点が
一致しなかったため、section 名や通常の symbol 値だけを信用しない。
PLT による遅延解決は実装せず、関数は module 自身の code または同梱 syscall wrapper へ解決する。

build 時に GOT displacement、PC 相対 call/branch は解決し、code 内の実 RAM 絶対 address への
relocation は拒否する。private GOT の各 pointer と、`const char *label = message`等の
data pointer だけを spawn 時の fixup 対象として記録する。

fixup record は big endian の 12 byte、`target_offset / kind / value_offset`の 3 field とする。
target は private initialized data 内の 4 byte 整列 slot に限定し、重複 target を拒否する。
`CODE32=1`は`image_base + value_offset`、`DATA32=2`は`data_base + value_offset`を
その slot へ書く。CODE32 は共有 code/rodata、DATA32 は private data/BSS の範囲を指す。
NULL は初期値 0 のままとする。元の仮想 pointer は template から取り除き、0 と fixup record に置く。
GOT の dynamic linker 用予約 slot は 0 にし、resolver を持ち込まない。

**`--emit-relocs`の出力だけではGOT初期化を網羅できない。** ローカル実験では
`.rela.data`に label の`R_68K_32`だけが残り、linker が生成した GOT pointer は列挙されなかった。
packer は`.rela.text`の`R_68K_GOT16O`と symbol/addend、最終 GOT displacement から slot を
対応付け、GOT 用 fixup も生成する。pointer らしい整数の全探索による再配置は行わない。
他の relocation 形式は、意味と target を実装・検証するまで拒否する。

#### 3.4.4 spawnと確認用C module

spawn は initialized data/GOT の copy、BSS clear、全 fixup の適用後に
`A5 = data_base + got_offset`を初期 frame へ設定する。準備中の GOT を公開しない。
同じ共有 module の GOT[counter]は各 process の counter を指し、GOT[message]/GOT[ss_write]は
同じ共有 rodata/code を指す。code を copy/patch しないまま複数 process を実行する。

```c
#include <stdint.h>
#include <ssos9/user.h>

static volatile uint32_t counter;
static const char message[] = "counter\r\n";
static const char *label = message;

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ss_write(1, label, sizeof(message) - 1);
    for (unsigned i = 0; i < 10; i++) {
        counter++;
        ss_print_u32(counter);
        ss_sleep(20);
    }
    return 0;
}
```

これは追加する user header/runtime 向けの例で、現在の repo ではまだ build できない。
C の counter を 2process で動かし、counter と label の格納先、A5、stack が別であること、
label が指す message と関数 pointer が共通であることを確認する。
code と data を異なる仮想配置・実 load 位置で検査し、PC 相対範囲超過、未解決 symbol、
未知 relocation、不正 fixup は停止する。ASM 版と C 版を同時に起動して syscall ABI も照合する。

## 4. Physical Memory Allocator

最小構成は 4 byte 単位の first-fit allocator と隣接 free block の結合とする。
コードの移動、compaction、swap は行わない。長寿命 module と短寿命 process が
同じ arena を使うため、総 free bytes に加えて largest free block を表示する。

| 起動形式 | allocatorが管理するRAM |
| --- | --- |
| `.x` | Human68Kから与えられたprogram領域内の専用256KiB arena（BSSとして確保） |
| `.xdf` | `__ssosram_start`から`__ssosram_end = $C00000`まで。kernel task stack poolの後ろ |

動作条件として 12MiB の連続した main RAM（`$000000–$BFFFFF`）を要求し、RAM 検出は行わない。
`.xdf`の arena 上限は linker/config で`$C00000`へ固定するため、実 RAM が条件に満たない機種では
起動しない。`.x`は Human68K が割り当てた program 領域内の BSS だけを使う。VRAM、I/O、ROM、
拡張 device の領域は arena に含めない。

process ごとに次の連続 block を割り当てる。

```text
low address
  private GOT + initialized data <-- data_base / A5 = data_base + got_offset
  zeroed BSS
  padding to 4-byte boundary
  copied argv pointers/strings
  lower stack canary
  private descending stack      <-- initial SP at aligned high end
high address

A5 -> GOT[counter] -> private counter
A5 -> GOT[message] -> shared rodata
A5 -> GOT[ss_write] -> shared syscall wrapper code
```

64KiB は初版の GOT を含む data/BSS の上限であり、毎 process に 64KiB を予約する意味ではない。
stack/引数はその上限とは別に計上する。argv 内のポインタはコピー先を指すよう再構成する。
canary は破壊検出用であり、保護領域ではない。全 size 加算・alignment・allocation を
overflow 検査し、spawn 失敗時は所有権を逆順に巻き戻す。

allocation/free list の更新だけを短い IRQ 禁止区間で行う。data コピー、BSS clear、
canary 埋め込み、CRC 計算は IRQ 許可状態で行い、準備中の block を他 process へ公開しない。

## 5. Process Table、context、終了とkill

### 5.1 Descriptorと状態

| field | 意味 |
| --- | --- |
| `pid`, `ppid` | 起動中に再利用しない単調増加32bit field。成功値は1〜`INT32_MAX`、0はkernel/reserved |
| `task` | 対応TCBへのpointer。PID 1は既存bootstrap TCBへ対応付ける |
| `module_id` | 主moduleの実行用ref |
| `allocation`, `data_base`, `got_base`, `stack_low/high` | process専用block、A5の起点、stack境界 |
| `saved_context` | TCBのcontext経由でD0–D7/A0–A6/SR/PC/SPを保持 |
| `state`, `wait_reason` | INITIALIZING / READY / RUNNING / WAIT / DYING / ZOMBIE |
| `exit_status`, `kill_pending` | 終了理由と取消要求 |
| `in_kernel` | syscall/IOCSの実行中かどうか |
| `fds[8]`, `links[]` | processが所有するfdと明示的module ref |
| `wait_target`, `wait_result` | 子process終了待ち |
| `parent_console` | foregroundを返す先のPID |

RUNNING は scheduler の current task から導出する。TCB は scheduler の queue/state を
所有し、descriptor は resource と wait 理由を所有する。状態変換を 1 つの関数へ集約し、
descriptor と TCB で矛盾する独立 state 更新を行わない。PID overflow 時は spawn を拒否する。

spawn は module の実行 ref 取得、descriptor/TCB 予約、fd ref 取得、private block 確保、
引数/data 初期化、frame 作成を順に行う。最後の短い IRQ 禁止区間で parent との関係、
foreground ownership、READY queue への公開を一括して確定する。
失敗時は READY へ公開せず、取得した resource だけを逆順に返す。割り当て済み PID は
失敗しても再利用しない。公開前の process へ ps/kill が触れないようにする。

### 5.2 起動frameとA5

PIC process を C の`entry(arg)`として呼ばない。専用の start frame を作り、
既存の`resume_interrupted`に対応する`movem`/`rte`で開始する。
kernel task の既存 C entry 経路は別の種別として維持する。

- PC = module entry、SR = `$2000`（supervisor、IPL mask 0）。
- A3 = image base、A5 = private GOT base、A7 = private stack。A6 初期値は 0。
- D0 = argc、A0 = copied argv。その他の初期 register は 0。
- frame は D0–D7/A0–A6 の 60 byte に SR 2 byte、PC 4 byte を続けた 66 byte。
- stack top は 4 byte 整列。frame の先頭は偶数整列でよい。
- PC→SR の順である現在の yield frame とは別 struct にして offset assertion を入れる。
- process の`resume_type=interrupted`として作り、既存「context==stack_base」sentinel
  による C entry 起動と取り違えない。

entry からの通常`rts`を ABI に含めない。module は`exit(status)` syscall で終了する。
C runtime は main の戻り値を exit へ渡す PIC start stub を用意する。
kernel C と syscall entry は process の A5 を必ず保存し、return 時に復元する。
A6 は C の frame pointer 等に使える通常の callee-saved register とし、ABI で予約しない。
module build では A3 を予約する。GCC の msep-data/PIC code generation が A5 を GOT 用に扱う。

### 5.3 安全なkillと回収

`kill PID`は TERM handler を呼ぶ機能ではなく、終了を要求する最小の強制 kill とする。
register された signal handler、STOP/CONT、process group は後続機能。

1. PID を検証し、shell root/idle/reaper/kernel task への kill を拒否する。
2. 対象が module 実行中なら次の Timer D 境界で DYING へ移し、復帰させない。
3. syscall/IOCS/共有 resource 更新中なら`kill_pending`を立て、安全な return 境界まで待つ。
4. WAIT 中なら sleep/child/I/O 待ち queue から外す。進行中 I/O の所有権は完了まで保持する。
5. 別 task の stack 上で reaper が fd/link を close、private block を free、task slot を解放する。
6. exit status だけを ZOMBIE descriptor に残し、親の`wait`で回収する。

回収時は descriptor の task/allocation pointer を NULL へ戻す。ZOMBIE の表示には
コピー済み module 名と終了時 snapshot だけを使い、解放済み stack/module を参照しない。

現在実行している SP、共有 module code、未完了 I/O の buffer を Timer D ISR 内で free しない。
ISR は状態変更と deferred cleanup 通知までを行う。`kill`の戻り値 0 は要求の受付であり、
回収完了は`wait PID`で確認する。kernel 内で停止した process を安全に強制回収する
保証はないため、kernel syscall には無期限 busy loop を作らない。

exit では子を PID 1 へ reparent する。PID 1 は自身の background child と孤児の exit status を
通知後に回収する。明示的な`wait PID`用に shell が小さな終了 status cache を保持する。
cache を越えた古い PID の wait は`ECHILD`を返す。foreground child の status は wait 完了まで保持。

### 5.4 `.x` / `.xdf`のexit

- 通常 child の exit はその process だけを終了し、待っている親へ status を返す。
- foreground child の終了時は console ownership を生存している親へ返す。
  親がなければ最小の生存 PID（root PID 1）へ戻す。
- background child の exit で shell の console ownership を奪わない。
- `.xdf`の初期 PID 1 は exit できない旨を表示する。kill 対象にもできない。
- `.x`の root shell で exit すると全 child へ kill 要求を出し、cleanup と I/O 完了を待つ。
  その後に syscall vector、MFP、supervisor mode を復元し、Human68K へ終了 status を返す。
- 将来の子 shell も foreground ownership と parent PID を使う。親への復帰は任意の PC へ
  jump する操作ではなく、親の wait 解除と scheduler への再登録として実装する。

## 6. Syscall ABI v1

### 6.1 trap、引数、結果

`trap #0`（vector table address `$80`）を SSOS-9 の入口として予約する。
起動時に旧 vector を保存し、`.x`の終了時に復元する。既存 QEMU port が同じ trap を
Timer D の代役として使っているため、process test では Timer D 代役を別 trap へ移す。
IOCS `trap #15`や Human68K の既存 call 機構は横取りしない。

```text
D0.l = syscall number
D1.l = pointer to the call's fixed-layout request, or 0 for no arguments
return D0.l = nonnegative result, or negative SS_E* error
```

D0 以外の register は preserve する。CCR に成功/失敗を依存させない。
request は 32bit field 中心の専用 wire layout とし、host の pointer 幅や C enum 幅を使わない。
version を syscall で取得し、未知の番号は`ENOSYS`、予約 field 非 0 は`EINVAL`。
wrapper は process の stack 上に request を構成する。

kernel は request/文字列/argv を検証して必要な情報をコピーする。read/write buffer は
process の private block（read/write 可能）または module image（write の source だけ）に
範囲制限する。raw pointer がこの範囲にあることは確認できるが、MMU による保護ではない。
request の入れ子 pointer、size 上限、文字列終端、整数 overflow も検証する。
非同期 I/O では buffer の所有権を保持するか kernel buffer へコピーする。

| number | wrapper | requestと結果 |
| --- | --- | --- |
| `$00` | `abi_version()` | 引数なし、1を返す |
| `$01` | `getpid()` | 引数なし、PIDを返す |
| `$02` | `getppid()` | 引数なし、PPIDを返す |
| `$03` | `spawn_process(req)` | name/path、argc/argv、fd map、flags。child PID |
| `$04` | `exit(status)` | status。成功時は戻らない |
| `$05` | `wait(pid, result, flags)` | 自分のchildのみ。pid=0でany child、WNOHANG可 |
| `$06` | `kill(pid)` | 要求受付0、対象なしはESRCH |
| `$07` | `sleep(ticks)` | Timer D tick単位。0はyield |
| `$08` | `proc_info(pid, out)` | state、parent、module、data/GOT/A5/stack、ticks |
| `$09` | `proc_list(cursor, out, capacity)` | 全descriptorの固定長snapshot。8件まで |
| `$10` | `load_module(path)` | 登録済みまたは新規module handle |
| `$11` | `link_module(name)` | 呼出processにrefを追加 |
| `$12` | `unlink_module(handle)` | 自分のlink refを解除 |
| `$13` | `module_info(handle, out)` | size、entry、ref_count、image address |
| `$14` | `unload_module(name)` | refなしの動的moduleを解除 |
| `$20` | `open(path, flags)` | 最小の空きfdを返す |
| `$21` | `close(fd)` | processのfd refを解除 |
| `$22` | `read(fd, buf, count)` | 読み込みbyte数。0はEOF |
| `$23` | `write(fd, buf, count)` | 書き込みbyte数。partial write可 |
| `$24` | `seek(fd, offset, whence)` | signed32bit displacement、結果は非負31bit位置。TTYはESPIPE |
| `$25` | `dup2(oldfd, newfd)` | fdを同じopen-file objectへ対応付ける |
| `$26` | `fstat(fd, out)` | type、size、open flags。固定長layout |
| `$27` | `fsync(fd)` | backendの書込みをflush。対応前はENOSYS |

flags と number は`ssos/include/ssos9/syscall.h`を唯一の定義元にする。
`SSUserSpawn`には ABI version、name/path pointer、argc、argv pointer、
stdin/stdout/stderr source fd、background flag を含める。handle は上位 bit を使わず、
syscall の成功値が負にならない範囲に制限する。
error は`EINVAL/ENOENT/ENOEXEC/ENOMEM/EAGAIN/ESRCH/ECHILD/EBADF/EACCES/EBUSY/
EIO/ENOSPC/ESPIPE/ENOSYS`等を SSOS-9 用の固定値として定義する。

`SSUserSpawn`の v1 request は 40 byte とする。
offset 0/4 は size=40 と version=1、8 は flags、12 は module 名または path の pointer、
16/20 は argc/argv、24/28/32 は stdin/stdout/stderr source fd、36 は reserved=0。
source fd=-1 は既定継承を意味し、background の stdin だけは null へ対応付ける。
明示的な TTY stdin 指定は foreground ownership の検証を通す。
argc 上限 16、copied argument 総量 1024 byte、path は 127 byte までとする。

open flags は READ=`$01`、WRITE=`$02`、CREATE=`$04`、TRUNC=`$08`、APPEND=`$10`。
READ/WRITE の少なくとも一方が必須で、TRUNC/APPEND には WRITE が必要。
seek の whence は SET=0、CUR=1、END=2。64bit file size は将来の ABI version で扱う。

### 6.2 syscall中のpreemptionと待ち

最初は kernel を single CPU の非再入構造にする。syscall 実行中は Timer D の tick 更新と
wakeup 通知は続けるが、実行中 kernel 処理を別 process の kernel 処理で再入させない。
`in_kernel`を scheduler と共有し、Timer D による switch を module 実行時に限定する。
kernel 内の queue 更新は短い IRQ 禁止区間で行う。

blocking read/wait/write は、待ち先と resource 所有権を確定し、IRQ/queue/allocator の
短時間 lock を解放してから明示的に scheduler へ制御を渡す。
file write の直列化用 token は進行中 I/O request に所属させ、disk 待ちの間も保持できる。
この token の解放責任と完了通知先を記録し、kernel 内の再入可能な待ち地点を限定する。
再開時は同じ process の trap frame を使い、結果を書いて
`rte`する。この block 経路は Timer D による非同期 kernel 再入と区別して実装する。
exit/kill pending は syscall から module へ戻る直前に処理する。

初期 shell の IOCS 呼出しも同じ guard を通す。module による直接 IOCS/MMIO は ABI で禁止し、
TTY backend を唯一の IOCS console 利用者にする。ハードウェアアクセスは supervisor mode、
MFP は奇数番地 byte access とし、既存 Timer D の enable/mask/EOI 保全を維持する。

## 7. fd、VFS、将来のFATとリダイレクト

### 7.1 所有権とbackend

```text
process fd[0..7]
    --> refcounted open-file object {node, offset, flags}
        --> shared node {type, size, per-node lock, backend}
            --> tty / null / romfs / ramfs / FAT
```

fd 0/1/2 は stdin/stdout/stderr。番号は process 内でのみ有効。
`dup2`と spawn 時の継承は同じ open-file object を共有し、offset と append flag も共有する。
独立した`open`は別 object を作る。同じ file への独立 open でも node と write lock は共有する。
fd/object/node は別 table で数と ref を管理し、object offset も lock 下で更新する。
同じ file を異なる path 表記で open しても node を重複生成しない。RAM filesystem は
node ID、FAT は mount と directory entry の位置で同一性を判断し、8.3 名の大小文字も
正規化する。path 文字列だけで lock の共有先を決めない。

初版では TTY、`/dev/null`、read-only `/rom/mod`を実装する。
次に小さな RAM filesystem で`>`/`>>`/`cat`と append 競合を検証する。
FAT を待たずに fd 契約と shell parser を確立する。FAT 未実装時でも RAM 上で file への
書き込みと redirect を動かし、存在しない disk mount は`ENOENT`等を返す。
spawn で引き継ぐのは fd 0/1/2 だけとする。parent の指定した source fd を child の標準 fd へ
対応付け、fd 3 以上は初版では継承しない。temporary redirect fd の不要な ref を残さない。

TTY input は foreground owner だけが読める。background process の stdin は既定で
`/dev/null`（read は EOF）、stdout/stderr は TTY を継承する。foreground spawn では
TTY ownership を child へ移し、shell は wait する。`Ctrl-C`は foreground child への
kill 要求として TTY backend が処理する。root shell 自体には送らない。

TTY write は上限付き buffer へコピーして直列化し、単一の drain task が IOCS へ出力する。
buffer 満杯なら WAIT_IO へ入る。shell の入力行を保ち、background 出力後に prompt と
編集中の行を再描画する。独立した複数 write を 1 行へまとめる保証は付けない。

### 7.2 `>`と`>>`の意味

| shell構文 | open flags | 既存file | 存在しないfile |
| --- | --- | --- | --- |
| `command > path` | WRONLY / CREATE / TRUNC | 起動前にsizeを0にする | 空fileを作る |
| `command >> path` | WRONLY / CREATE / APPEND | 内容を保持し、writeごとに末尾へ追記 | 空fileを作る |

この append 契約は Linux の`O_APPEND`と同じく、各 write の直前に EOF 位置を決め、
位置の決定と書き込みを一体として行う。shell が一度だけ`seek(END)`してから write する
実装では、複数 process の append が衝突するので不可。

APPEND を指定した object では`seek`しても次の write は現在の EOF へ追加する。
同一 node の lock を、EOF 決定、容量確保、データ書込み、size/offset 更新まで保持する。
独立 open からの append や通常 write、truncate も同じ node の書込み lock を使う。
disk I/O 中に IRQ を禁止し続けず、競合 process を wait queue へ入れる。

1 回の成功した write で受理した byte 列は他 write と重複した領域へ書かない。
空き容量不足等で partial write が起きる場合、受理済み byte 数を返し、offset/size も
その分だけ更新する。複数 write からなる行の一体性や電源断時の transaction は保証しない。
`write_all()` wrapper は partial write を処理するが、再試行間に他 process の write が入れる。

flags は`APPEND | TRUNC`を同時指定すると`EINVAL`。
CREATE なしの不存在 file は`ENOENT`、write 不可 fd は`EBADF`または`EACCES`。
出力 backend は byte 列をそのまま記録し、stdout/stderr の改行変換は module/TTY 側で扱う。

### 7.3 shellでの適用順序

1. token 列を解析し、module 名と argv、background、redirect path を確定する。
2. module 存在と spawn 引数を検証する。
3. redirect 先を open する。失敗時は child を開始しない。
4. child の fd map を構成して spawn する。shell の fd 1 自体を入れ替えない。
5. shell 側の temporary fd を close する。child は独立した ref を保持する。
6. foreground なら wait、background なら PID を表示して prompt へ戻る。

`>`による作成/切り詰めは open 時に起こるため、後続 spawn が ENOMEM 等で失敗しても
元の file 内容を復元しない。この副作用をコマンド仕様とテストに含める。
process 準備に失敗した場合でも fd ref、module ref、RAM は漏らさない。

### 7.4 FATの拡張点

block device → FAT mount/node/backend → open-file object の順に接続する。
最初の disk 対応は XDF 形式に合う FAT12 と 8.3 file 名、root directory から開始し、
FAT16、subdirectory、長い file 名は別フェーズにする。既存 boot 用 XDF は独自の raw kernel
配置であり、FAT volume と同じ layout だと仮定しない。最初は別 disk image を mount する。

read-only FAT で directory/cluster chain を検証した後、create/truncate/write/append へ進む。
FAT entry、cluster chain、directory entry の size と flush 順序を設計し、loop/bad cluster/
disk full/read-only device/I/O error を処理する。close/exit 時の flush error は log へ記録し、
確実な永続化確認には明示的な`fsync`と結果検査を使う。電源断時の整合性は別の検証項目。

## 8. 教育用shellとデモmodule

| コマンド | 最小の機能 |
| --- | --- |
| `counter --start 0 --step 1 --ticks 20 &` | 共有PIC counterをbackground起動しPIDを表示 |
| `run counter ...` | foreground実行。module名だけの入力も同じ意味 |
| `ps` | PID/PPID/module/state/priority/ticksを表示。kernel taskは別行 |
| `proc PID` | module base/entry、data base/GOT/A5、saved SP/PC、stack境界を表示 |
| `kill PID` | 最小killを要求。`kill -9 PID`を同じ意味のaliasにしてよい |
| `wait PID` | childのcleanup完了と終了statusを待つ |
| `modules` | module名、共有code address、size、ref_countを表示 |
| `load path` / `unload name` | moduleの登録と参照なしmoduleの解放 |
| `mem` | arena使用量、free bytes、largest block、process別所有量を表示 |
| `trace` | PIDを含むspawn/switch/wait/exit/kill/reapイベント |
| `cat path` | RAM filesystem以降でfileをstdoutへ読み出す |
| `exit` | 現在のshellの終了規則に従う |

parser の初版は最大 16 token、入力行 128 byte、末尾`&`、単一の stdout `>`または`>>`、
単純な single/double quote をサポートする。`>>`を`>`より先に認識し、quote 内の記号は
文字列として扱う。空の quote は空引数になる。未閉 quote、複数 redirect、未対応の
`|`/`<`/`2>`/`;`等は構文 error にし、一部だけ解釈して実行しない。
引数にしたい記号は quote する。redirect 先は 1 token で、末尾に`&`を置く。
最初は外部 module への redirect を対象にし、shell 組み込みコマンドへの redirect は
対応するまで明確に error を返す。

```sh
counter --start 0 --step 1 --ticks 20 &
counter --start 1000 --step 7 --ticks 20 &
ps
modules
proc 2
proc 3
kill 2
wait 2
ps

# RAM filesystem導入後。同じfileへの同時appendを確認する。
counter --count 5 > /ram/count.log
counter --tag A --count 5 >> /ram/count.log &
counter --tag B --count 5 >> /ram/count.log &
wait 5
wait 6
cat /ram/count.log
```

例の PID は説明用。実際には spawn で返された PID を使う。shell の変数展開は初版に不要。
counter の可変状態はすべて private data へ置き、A5/GOT 経由で参照し、tag と起点を引数から初期化する。
`counter`は sleep で間隔を空け、`spin --ticks N`は syscall/yield なしの計算区間を設ける。
spin と shell/counter の同時進行で preemption を確認する。無限の kernel 内 spin は作らない。

観察項目は「code/entry/A3 が同一、data/GOT/A5/SP が別、値が独立、片方を kill しても
他方は継続、ref_count が 2→1、最後の exit 後 0」の 6 点とする。
`ps/proc/trace`は IRQ 禁止区間内で小さな snapshot だけ取得し、表示は IRQ 許可状態で行う。
saved PC/SP は最後の退避時点の値であり、実行中 process の瞬間値とは表示しない。

## 9. 実装フェーズと完了条件

順序は上から。idle/reaper 起動、cancel/release、stack pool 設定共有、first-fit arena、
arena platform 初期化、S9M1 validator/固定長 Module Table、private data/fixup、合成 SR/PC frame、
PID 1 と built-in PIC counter の spawn/kill/wait、`ps`/`run`/`kill`/`wait`コマンドまで実装済み。
外部 module loader/packer、GCC C runtime、syscall ABI 以降は未完了。
FAT を待たず、RAM filesystem までで process と redirect を学べる状態にする。

| phase | 実装 | 完了条件 |
| --- | --- | --- |
| 0 | RAM arena境界、idle、task取消/待ち解除/別stack回収、bootstrap PID対応 | 最後の非idle taskがsleep/exitしても停止しない。slotを再利用できる |
| 1 | module header/packer、GOT/fixup、Table、allocator、組み込みcounter image | 壊れたimageを拒否し、load/unloadでRAMが元に戻る |
| 2 | trap ABI、process frame、spawn、exit/wait/kill、guard | 同一counterを2 processで実行し、A5/GOT/stack分離と安全なreapを確認 |
| 2C | C module用build、start stub、最小runtime、C counter | GCCで作ったglobal/BSS/pointer初期値が2 processで独立する |
| 3 | shell parser、background/foreground、ps/proc/modules/mem/trace | background起動中にshellを操作でき、任意childをkill/waitできる |
| 4 | fd/open-file/node、TTY/null/romfs、継承/dup2、RAM filesystem | stdoutをconsole/fileへ同じwrite syscallで出力できる |
| 5 | `>`/`>>`/cat、node lock、partial write/error handling | 同じfileに2 processが追記しても既存内容を上書きしない |
| 6 | FAT12 read-only、別disk mount、fileからmodule load | FAT fileからmoduleを読み、同じloaderで共有実行できる |
| 7 | FAT create/truncate/write/append、flush/fsync | 再mount後もredirect内容とfile size/cluster chainが一致 |
| 後続 | C runtime拡張、子shell、user mode、pipe/shared subroutine module | 前段のABIと所有権を維持して個別に設計・検証 |

phase 0〜3 を最初の multi process release、phase 4〜5 を redirect release とする。
PID 1 での`.xdf` exit 拒否と`.x`終了復元は両 release の必須条件。

### 9.1 追加予定のファイル

```text
ssos/include/ssos9/   syscall.h, module_format.h, user.h
ssos/os/process/     process.c/.h
ssos/os/module/      module.c/.h, image.c
ssos/os/mem/         arena.c/.h
ssos/os/io/          fd.c, vfs.c, tty.c, null.c, romfs.c, ramfs.c
ssos/os/kernel/      syscall.c, syscall_entry.s, process_frame.h
ssos/modules/        counter.s, counter.c, spin.s, module.ld, Makefile
ssos/userlib/        start.s, syscall.s, memory/string/整数出力関数
tools/               mkmodule.py
tests/unit/          allocator/module/process/fd/parserの純粋ロジック
tests/qemu/          process frame/trap/A5/GOT/preemption/reap
```

これらは用途が必要になった phase で追加する。以前削除した GUI、汎用 slab/buddy、
大規模 IPC をそのまま戻さない。`common-sources.mk`で`.x`/`.xdf`の共通 source を登録し、
header の offset assertion を production と QEMU で共有する。`source ~/.elf2x68k`後に
cross toolchain を使い、`make install`は従来どおり`ssos9.x/ssos9.xdf`を`~/tmp`へコピーする。
初期 module blob の組み込みでは boot の読込 sector 数と kernel image 容量制限も確認する。

### 9.2 検証項目

| 層 | 対象 |
| --- | --- |
| host | arena整列/分割/結合、image範囲/overflow/CRC、spawn rollback、ref/fd/queue所有権、parser |
| QEMU | A3/A5保存復元、GOTの共有code/private data参照、CのA6 frame、初期SR→PC frame、trap結果、sleep/wait/kill/reap、slot再利用 |
| C build/packer | GOT slotとsymbolの対応、CODE32/DATA32、pointer初期値、helper/import拒否、GOT/PC変位上限 |
| RAM filesystem | TRUNC、APPEND after seek、独立open同時append、dup offset共有、ENOSPC/partial write |
| X68000 emulator/実機 | 実MFP非同期preemption、IOCS入力/出力、shell操作中kill、`.x`復元、`.xdf`初期process |
| FAT disk image | FAT chain整合、disk full/I/O error、create/append/flush、再mountによる内容検査 |

resource 限界まで spawn→kill→wait を繰り返して、allocator 使用量、module ref、fd ref、
TCB slot が baseline へ戻ることを確認する。wait 前の ZOMBIE と wait 後の free を区別する。
最後の process の exit、親の先行 exit、PID 不存在、root kill 拒否、kernel syscall 中の kill、
sleep/TTY 待ち中の kill も含める。spin は Timer D を模倣する同期 trap だけでなく、実機の
非同期割り込みで確認する。QEMU/host 成功を実 MFP の確認済みとは扱わない。

## 10. 参考資料とSSOS-9での適用範囲

- [OS-9/68K V2.4 Technical Manual](https://www.peripheraltech.com/OS9%20-%2068K%20V2.4%20Technical%20Manual.pdf): Process Creation（2-23〜2-25）、F$Fork（System Calls 1-30〜1-31）。共有 module と process 用 data/stack の構成を参考にした。OS-9 の A6 `$8000` bias ABI は本計画では採用せず、GCC 対応の A5/GOT とする。S9M1、trap 番号、fd API、supervisor 初版も SSOS-9 独自の設計。
- [GCC M680x0 Options](https://gcc.gnu.org/onlinedocs/gcc/M680x0-Options.html): `-msep-data`で text と別配置の data を参照する code を生成できる。GCC 公式 manual は一般的な仕様、section 3.4 の GOT/relocation 記述はローカル GCC 13.4.0 での code/link 観察に基づく。
- [Linux open(2)](https://www.man7.org/linux/man-pages/man2/open.2.html): `O_APPEND`は各 write で EOF へ位置を合わせ、位置更新と write を一体として行う。本計画の`>>`の契約に採用する。FAT の電源断耐性まで同じ保証を与える意味ではない。
- `x68k-master/resources/03_割り込み.md`、`04_MFP.md`、`38_IOCS.md`、`43_Trap.md`: vector、EOI、supervisor 移行、IOCS と trap 利用の確認先。原典は『Inside X68000』第 3・4 章。process 追加時も MFP の既存 source 状態を保持する。
- 現行の[task API](task-api-contract.md)、[context switch契約](context-switch-contract.md)、[runtime所有権](runtime-ownership.md)を実装時に更新する。この計画の将来仕様を現行機能として README へ記載しない。
