# Task APIの契約

| API | 動作 | 制限 |
| --- | --- | --- |
| `ss_task_create` | taskとstackを準備しIDを返す | IDは1〜`SS_MAX_TASKS`。失敗値は`uint16_t`に符号化された`SS_ERR_*`。 |
| `ss_task_start` | DORMANT taskをREADYにする | taskを一度だけ開始できる。 |
| `ss_task_sleep` | 指定tick数だけtaskをsleepさせる | 最大値は`SS_MAX_SLEEP_TICKS`。 |
| `ss_task_yield` | 次のready taskへ切り替える | ready taskがなければ同じtaskを続ける。 |
| task entry return | taskをTERMINATEDにする | IDとstackは予約されたまま。join・cancel・再利用なし。 |

custom stack は呼び出し側が shutdown まで保持します。`SSTaskInfo.stack`は領域の先頭ではなく、4 byte 境界に揃えた最終 word を指します。stack は少なくとも`SS_MIN_TASK_STACK` bytes 必要です。

この API は同一物理アドレス空間内の kernel task 向けです。process descriptor、module loading/linking、process ごとの data 領域はまだありません。
