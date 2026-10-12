# Task APIの契約

| API | 動作 | 制限 |
| --- | --- | --- |
| `ss_task_create` | taskとstackを準備しIDを返す | IDは1〜`SS_MAX_TASKS`。失敗値は`uint16_t`に符号化された`SS_ERR_*`。 |
| `ss_task_start` | DORMANT taskをREADYにする | taskを一度だけ開始できる。 |
| `ss_task_cancel` | READY/WAIT taskをTERMINATEDにする | 実行中taskは対象にできない。process killはsyscall中なら安全なsyscall境界まで遅延する。 |
| `ss_task_wake` | WAIT taskをready queueへ戻す | WAITでないtaskは`SS_ERR_STATE`。 |
| `ss_task_sleep` | 指定tick数だけtaskをsleepさせる | 最大値は`SS_MAX_SLEEP_TICKS`。 |
| `ss_task_yield` | 次のready taskへ切り替える | ready taskがなければ同じtaskを続ける。 |
| task entry return | taskをTERMINATEDにする | reaperがTCB slotを解放する。 |

custom stack は呼び出し側が所有し、scheduler は解放しません。process の専用 stack は process allocator が所有し、終了 process の reaper 処理で解放します。`SSTaskInfo.stack`は領域の先頭ではなく、4 byte 境界に揃えた最終 word を指します。stack は少なくとも`SS_MIN_TASK_STACK` bytes 必要です。

この API は同一物理アドレス空間内の kernel task 向けです。OS process は別 API で管理し、module image を共有しつつ data/GOT/stack を分離します。外部 module loading と user 向け C wrapper は未実装です。
