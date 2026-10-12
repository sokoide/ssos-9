# 実行時の所有権

SSOS-9 の最小構成は Supervisor mode の preemptive scheduler、Timer D 割り込み、IOCS を使う CLI です。

| 対象 | 所有者・規則 |
| --- | --- |
| ready/sleep queueとtask状態 | schedulerが管理する。変更中は割り込みレベル7を保つ。 |
| task stack | 作成時にpoolから確保する。終了taskのstackとIDは再利用しない。 |
| Timer DとMFP状態 | kernelが初期化・保存・復元する。割り込みhandlerは短く保つ。 |
| CLI画面・入力 | main taskだけがIOCSを呼ぶ。 |

`ss_irq_save`と`ss_irq_restore`は Supervisor mode 専用です。ネストする場合は各呼び出しの保存値を逆順で復元してください。task entry が return すると task は TERMINATED になります。join、cancel、stack 回収、PID 再利用は未実装です。

現在の task scheduler は将来の OS-9 型 process loader とは別の機能です。独立したアドレス空間や module table はありません。
