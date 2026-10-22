# 実行時の所有権

SSOS-9 の最小構成は Supervisor mode の preemptive scheduler、Timer D 割り込み、IOCS を使う CLI です。

| 対象 | 所有者・規則 |
| --- | --- |
| ready/sleep queueとtask状態 | schedulerが管理する。変更中は割り込みレベル7を保つ。 |
| task stack | kernel taskは固定pool、processはprocess専用blockから確保する。reaperが終了後に解放する。 |
| Timer DとMFP状態 | kernelが初期化・保存・復元する。割り込みhandlerは短く保つ。 |
| CLI画面・入力 | `app/cli_console.c` が IOCS 呼出しを直列化する。背景の組み込み子 process は command 全体で取消 guard を保持し、lock 所有中に回収されない。キーボードは親 CLI だけが読む。 |
| module 診断 | registry が READY entry の snapshot をコピーする。列挙は ref を増やさず、image pointer は表示用。bytes を読むには別途 ref を取得する。 |
| arena 診断 | CLI の `mem` は free bytes と largest free を同じ IRQ 禁止区間で取得し、IRQ 復元後に表示する。 |

`ss_irq_save`と`ss_irq_restore`は Supervisor mode 専用です。ネストする場合は各呼び出しの保存値を逆順で復元してください。task entry が return すると task は TERMINATED になります。終了 task の TCB は reaper が別 task の stack から解放します。`ss_task_release_terminated` は IRQ 禁止区間で TERMINATED を REAPING に変更して回収権を確保し、呼出し元の IRQ 状態に戻して process hook を実行した後、TCB を解放します。回収中の slot は再利用・再回収できません。process のメモリ/module 参照は ZOMBIE 化時に解放し、親の`wait`後に PID slot を再利用します。syscall 実行中の kill は syscall 境界まで遅延し、sleep/wait 中なら起床させます。

process は同一 Supervisor 物理アドレス空間を共有します。Module Table の PIC code は共有し、data/GOT/stack は process ごとに確保します。MMU による保護はありません。外部`.s9m` loader、GCC 用 module runtime、file descriptor と redirect は未実装です。

背景の組み込みコマンドは `ss_process_spawn_kernel` で起動する信頼済み C entry の子 process です。
module ref/GOT は持たず、argument copy と private stack を arena で所有します。
entry の return は通常の task 終了経路へ進み、reaper が block を回収し、親の wait が PID slot を回収します。
`exit` は子の終了だけに作用し、子の `wait` は自身の直接の子だけを対象にします。
console の software lock 待ちでは IRQ を復元して sleep します。新しい呼出し元を追加する場合も、
取消 guard の内側から利用し、lock 所有中の強制取消を避けてください。

## syscall のメモリ検査

module process の request・入力は自身の private block または参照保持中の module image、
出力は private block に全体が収まる必要があります。kernel-entry 子 process は private block
だけを使用できます。構造体・pointer 配列は 4 byte 整列を要求し、文字列は上限内の NUL を検査して
kernel stack へコピーします。`spawn` の引数文字列は合計 512 bytes（NUL を含む）までです。
PID 1 は信頼済みで、必須環境の連続 12 MiB RAM のうち `[0x1000, 0xc00000)` を使用できます。
この PID 1 の検査は所有権を制限せず、低位 vector/IOCS work、VRAM、I/O、ROM を除外するものです。
範囲外・末尾超過・未終端文字列は `SS9_ERR_PARAM` で拒否します。
Supervisor mode の直接 load/store は制限できず、例外からの process 単位の復旧も未実装です。
