# コンテキスト切替の契約

正本は `ssos/os/kernel/scheduler_abi.h`（TCB 配置）、`context_frame.h`（例外 frame）、`scheduler.c`（queue と task 状態）です。preemptive 版だけをビルドします。

Timer D 割り込みでは MC68000 が SR と PC を stack へ積み、`interrupts.s`が d0–d7/a0–a6 を保存して scheduler を呼びます。scheduler は選択した task の保存 SP を復元し、`rte`で再開します。明示 yield は PC→SR と register を手動で積み、`resume_type` により SR/PC を手動復元して再開します。Timer D の SR→PC frame と混同しないでください。QEMU 用 port はこの経路を模擬しますが、実 MFP 割り込みの代替ではありません。

`SSTask`と assembly の offset は C の static assert で照合します。通常の割り込み frame は SR・PC を含む 6 bytes です。bus/address error の特殊 frame を復帰できる設計ではありません。

| 境界 | production | QEMUで確認する範囲 | 実機・Human68Kで別途確認 |
| --- | --- | --- | --- |
| task作成・開始・終了 | scheduler.c | ready queueとtask状態遷移 | 実RAM上のstack配置 |
| preemption/resume | MFP Timer D、`rte` | scheduler Cと同期port | 非同期割り込み、Timer D周期 |
| 起動 | boot loader、entry、premain | QEMU kernel/静的stack pool | X68000起動とHuman68K環境 |
| CLI入出力 | IOCS | 対象外 | キー入力と画面出力 |

`tools/check-context.py`は production と QEMU port の命令列を確認します。これは実行時の割り込み安全性や MMIO 挙動を証明しません。
