# SSOS-9 の設計

SSOS-9 は、共有コードとプロセス専用データ、プリエンプション、syscall、資源回収を
CLI から観察するための教育用 OS である。OS-9 型の module/process 分離を採用する。
既存 OS-9 バイナリ・ヘッダ・syscall との互換性は提供しない。

この文書は全体の責務と変更方針を定める。実装済み機能は [README](../README.md)、
将来の wire format・I/O・実装順は [ローダー計画](multiprocess-loader-plan.md)を参照する。
実装との不一致を見つけた場合は実装を確認し、現行契約と将来案を分けて更新する。

## 1. 教材として維持する範囲

対象は 16MHz 以上の MC68000、12MiB の連続 main RAM、単一 CPU、CLI 専用。
Timer D による preemptive scheduler を共通に使い、`.x` と `.xdf` の差を起動・終了と
arena の確保方法に閉じ込める。GUI、別 scheduler、汎用 framework の追加を前提にしない。

初期 module は信頼済みコードとして supervisor mode で動く。
private data はプロセスごとに割り当てるが、同じ物理アドレス空間にあり、アクセス保護はない。
不正 module や bus/address error からプロセス単位で復帰する保証もない。
user mode 化には USP/SSP と kernel stack の設計が必要であり、独立した後続課題とする。

一つの拡張には「学ぶ概念」「CLI での観察」「失敗時の挙動」「検証方法」を付ける。
最初に共有 PIC と回収を学び、次に C module、fd と RAM 上の redirect、最後に disk を扱う。
FAT や C の出力関数を、前段の学習を始めるための必須条件にしない。

## 2. 責務と依存方向

```text
CLI / 将来の userlib
    -> process API / trap entry・dispatch
        -> process -> module registry -> image validator
        -> process -> scheduler
        -> process / module -> system arena -> arena

.x launcher / XDF bootstrap -> 共通 kernel 初期化
Timer D entry -> scheduler / wakeups
```

| 境界 | 所有するもの | 変更時の規則 |
| --- | --- | --- |
| `app/cli.c`, `app/cli_wait.c`, `app/cli_console.c` | 入力・表示・foreground 待ち | table の実体を編集せず API と snapshot を使う |
| `kernel/syscall*`, `include/ssos9/syscall.h` | register ABI、request layout、引数の検査 | CLI 解析を持ち込まず process API へ渡す |
| `process/` | PID/PPID、private block、実行 module ref、終了値 | scheduler task ID と PID を区別する |
| `kernel/scheduler*`, `kernel/preemptive/` | TCB、ready/sleep queue、context、kernel task pool | module 名・shell 構文・VFS path を解釈しない |
| `module/module*` | 検証済み image の登録、handle、参照数、private fixup | file の取得と image の検証を分ける |
| `module/image*` | S9M1 の範囲・CRC・fixup 検査 | file I/O、scheduler、IOCS に依存させない |
| `mem/arena*`, `mem/system_arena*` | first-fit free list と platform arena | process/module の中身を解釈しない |
| `boot/`, `standalone/`, kernel の起動 assembly | platform 起動・vector/MFP の保存と復元 | process/module の別実装を作らない |
| 将来の `io/` | fd、open-file object、node、backend | module loader は backend 固有形式を知らない |

既存の C 関数と header を境界として使う。差し替える必要のない部分に
vtable、service container、汎用 plugin 機構を追加しない。
共有 source は `ssos/common-sources.mk`、platform と stack pool の設定は `ssos/config.mk` に集約する。

## 3. 状態と所有権

process は 1 scheduler task に対応する。背景の組み込みは module/GOT を持たない C entry の子 process で、引数コピーと専用 stack を所有する。PID 1 の shell は bootstrap TCB を使い、
heartbeat/idle/reaper は PID を持たない kernel task である。
`SS_PROCESS_READY` は生存する process を表す現行の lifecycle 状態であり、
CPU 上で実行中か、sleep 中かを示す scheduler 状態とは区別する。
計画上の RUNNING/WAIT 等を、そのまま現行 enum の値として読まない。

```text
FREE -> INITIALIZING -> READY -> REAPING -> ZOMBIE -> FREE
          |                                              ^
          +--------------- 準備失敗・rollback -----------+
```

- registry が共有 image を保持する。spawn は実行用 ref を取得し、private data/GOT/argv/stack を作る。
- spawn の準備失敗は取得済み資源を返し、開始していない task を残さない。
- kill の受付と資源回収完了は別である。syscall 中の kill は安全な境界まで遅延する。
- reaper は別 stack 上で task と private block、module ref を回収する。
- ZOMBIE には終了値が残る。親の `wait` が descriptor を解放し、process slot を再利用可能にする。
- 親の先行終了では子を PID 1 へ移す。現行 shell は終了値を自動回収する cache を持たず、`wait` が必要。

詳細は [実行時の所有権](runtime-ownership.md)と [Task API](task-api-contract.md)を正本にする。
将来の fd と I/O request にも所有者・取消時の扱い・最終解放地点を定義してから追加する。

## 4. 割り込みと診断

queue、table、free list の更新は短い `ss_irq_save` / `ss_irq_restore` 区間で行う。
ネストでは保存した SR を逆順に復元する。コピー、CRC、文字列表示、disk 待ちを
長い IRQ 禁止区間に入れない。現在の syscall guard は process ごとの境界管理であり、
将来の全 kernel I/O の再入可能性を保証するものではない。

CLI は snapshot を取得してから表示する。IOCS は console adapter の software lock で直列化し、背景組み込みの取消は lock 解放後の command 境界まで遅延する。module 列挙は参照数を変えず、snapshot 内の
image pointer は表示用アドレスとして使う。後で bytes を読む必要がある呼出し側は
module ref を取得する。一覧の各行は個別の観測で、一覧全体が同一 tick の値とは限らない。
`mem` の free bytes と largest free は同じ IRQ 区間で取得する。

MFP と trap の境界は `kernel/preemptive/interrupts.s`、`kernel/mfp_state.s`、
`kernel/ctrl_c.s`、`kernel/syscall_entry.s` と
[context switch 契約](context-switch-contract.md)で確認する。
低レベル資料は `x68k-master/resources/04_MFP.md` と `43_Trap.md`
（開発用スキルの資料。ビルド依存にはしない）。
Timer D 以外の source 状態を保ち、`.x` は child 回収後に保存した環境へ戻る。
`.xdf` は PID 1 の終了を拒否する。

## 5. ABI の変更方針

現行 ABI の正本は `ssos/include/ssos9/syscall.h` と dispatch の実装である。
S9M1 の image version と syscall ABI version は別の契約であり、混同しない。
未実装番号は予約であって機能の提供ではなく、dispatch は `SS9_ERR_NOSYS` を返す。

同じ syscall 番号・version の request を別 layout に読み替えない。
現行 `SS9SpawnRequest` は 12 byte、引数文字列は合計 512 byte が上限。
計画の size/version/fd/flags 付き 40 byte request は互換性のある置換ではない。
導入時には別番号の拡張 call で旧 call を保持するか、ABI version を更新して旧 module を
実行前に拒否する仕組みを作る。どちらを採るかは実装フェーズで確定し、
header、wrapper、packer、dispatch、互換性試験を同じ変更で揃える。
error 値と公開 state 値の変更にも同じ規則を適用する。

wire format を host struct の丸ごと出力で作らない。サイズ・offset・整列・予約値を検証する。
未知の section/relocation/helper を拒否し、C runtime に Human68K libc を混ぜない。

## 6. 変更の完了条件

| 変更 | 必要な確認 |
| --- | --- |
| 文書 | 現行/予定の区別、相対リンク、Markdown、実装との照合 |
| queue・資源・ABI | 関連する native/QEMU 試験、失敗・取消・再利用のケース |
| module build/runtime | relocation 検査、複数 load 配置、2 process の data 分離 |
| platform・MMIO・コンソール | クロスビルドに加え X68000 実機または対応 emulator の手動確認 |

基本の自動検証は `source ~/.elf2x68k` 後の `make test`、`make test-qemu`、`make all`。
全体検証には `make verify` を使う。QEMU は production C と context-switch port を実行するが、
同期 trap による Timer D 代替であり、実 MFP 非同期割り込みを確認しない。

手動確認の記録には日付、機種/emulator と version、CPU/RAM、起動形式、成果物 hash、
操作、期待値、結果を含める。特に `.x` の起動/終了復元、`.xdf` の起動/PID 1 exit 拒否、
物理 Ctrl-C、syscall のない計算中の preemption を確認する。
リリース名だけで検証済みとせず、自動検証と platform 確認の結果を別々に記録する。
