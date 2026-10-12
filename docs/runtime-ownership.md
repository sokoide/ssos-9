# 実行時の所有権

SSOS-9 の最小構成は Supervisor mode の preemptive scheduler、Timer D 割り込み、IOCS を使う CLI です。

| 対象 | 所有者・規則 |
| --- | --- |
| ready/sleep queueとtask状態 | schedulerが管理する。変更中は割り込みレベル7を保つ。 |
| task stack | kernel taskは固定pool、processはprocess専用blockから確保する。reaperが終了後に解放する。 |
| Timer DとMFP状態 | kernelが初期化・保存・復元する。割り込みhandlerは短く保つ。 |
| CLI画面・入力 | main taskだけがIOCSを呼ぶ。 |

`ss_irq_save`と`ss_irq_restore`は Supervisor mode 専用です。ネストする場合は各呼び出しの保存値を逆順で復元してください。task entry が return すると task は TERMINATED になります。終了 task の TCB は reaper が別 task の stack から解放します。process のメモリ/module 参照は ZOMBIE 化時に解放し、親の`wait`後に PID slot を再利用します。syscall 実行中の kill は syscall 境界まで遅延し、sleep/wait 中なら起床させます。

process は同一 Supervisor 物理アドレス空間を共有します。Module Table の PIC code は共有し、data/GOT/stack は process ごとに確保します。MMU による保護はありません。外部`.s9m` loader、GCC 用 module runtime、file descriptor と redirect は未実装です。
