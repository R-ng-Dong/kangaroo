/**
 * @file task_stacks.h
 * @brief Task stack sizes, each stated next to the peak it was measured against.
 *
 * WHY THESE LIVE TOGETHER: a stack size is only safe relative to an OBSERVED peak, and the
 * peak is a property of a workload, not of the code. Scattering the numbers as bare
 * `#define FOO_STACK 4096` loses the evidence, so the next person either leaves 3 KB of
 * waste in place or shaves it blind. Both are bad here: the heap is the binding constraint
 * on this C3 (mbedTLS needs 16749 CONTIGUOUS bytes for a TLS handshake, and a long-lived
 * task stack sits in the middle of the heap fragmenting it), while a stack overflow on a
 * sealed unit with no serial header is unrecoverable without soldering.
 *
 * So: size and measured peak in one place, with a host test asserting the margin never
 * silently erodes.
 *
 * PEAKS: measured on an ESP32-C3 mesh hub (2 boots) via FreeRTOS high-water marks.
 * A high-water mark is the worst case SEEN, not
 * the worst case POSSIBLE, which is why the margins below are deliberately generous rather
 * than shaved to the peak.
 *
 * BEFORE SHRINKING FURTHER: re-measure on the actual target under load (DAO writes, cache
 * flush, timer callbacks firing smart/schedule evaluation) and update both the size and the
 * peak here. Do not tighten a margin on the strength of a quiet boot.
 *
 * SECOND DATA POINT -- measured on an ESP32-C3 4-gang switch at the
 * heaviest moment available: provisioned, MQTT/TLS up, immediately after an OTA download.
 * ESP-IDF's high-water mark is in BYTES (vanilla FreeRTOS returns words), so these are the
 * smallest free space ever seen:
 *
 *     task         size   free   USED
 *     timer        4096   3808    288
 *     cache_flush  2048   1760    288
 *     dao_task     3072   2028   1044
 *     core_work    6144   4312   1832
 *     sys_evt      2816    796   2020   (IDF's, not ours -- least headroom of all)
 *
 * The sizes below are NOT tightened to these figures, deliberately, for two reasons:
 *
 *   1. `timer` used only 288 B here because this unit has NO automations configured, so no
 *      smart-trigger or schedule evaluation ever ran inside a timer callback -- which is
 *      exactly the load that matters for that task, and exactly what crash-looped a board
 *      before. 288 B is not its worst case.
 *   2. The two boards disagree (the mesh hub showed core_work far busier). A size
 *      that fits one workload is not evidence about another.
 *
 * What these DO establish: the 2026-10-09 reduction (timer 6144->4096, dao_task 4096->3072,
 * cache_flush 4096->2048) starves nothing, and it moved the heap's largest contiguous block
 * from 32768 to 40960 -- which is the number that decides whether a TLS handshake fits.
 */

#ifndef IOT_ESP32_TASK_STACKS_H
#define IOT_ESP32_TASK_STACKS_H

/** Every task must keep at least this much headroom above its measured peak.
 *  A stack overflow here costs a soldering job, so the margin is worth the heap. */
#define IOT_STACK_MIN_MARGIN 1024

/* ── timer ────────────────────────────────────────────────────────────────────
 * Runs IOT_TimerInsertCb callbacks, which carry project logic (smart triggers,
 * schedule evaluation). Deliberately the most generous margin: a heavy timer callback
 * has crash-looped a board before. */
#define IOT_STACK_TIMER 4096
#define IOT_STACK_TIMER_PEAK 2364 /* measured, ESP32-C3 */

/* ── dao_task ─────────────────────────────────────────────────────────────────
 * Serialises NVS work. Peak includes an encrypted-NVS write path. */
#define IOT_STACK_DAO_TASK 3072
#define IOT_STACK_DAO_TASK_PEAK 1272 /* measured, ESP32-C3 */

/* ── cache_flush ──────────────────────────────────────────────────────────────
 * Write-back flush; hands the actual write to dao_task, hence the very low peak. */
#define IOT_STACK_CACHE_FLUSH 2048
#define IOT_STACK_CACHE_FLUSH_PEAK 504 /* measured, ESP32-C3 */

/* ── console ────────────────────────────────────────────────────────────────
 * Reserved for an optional local console task. The shipped platform does not create it. */
#define IOT_STACK_CONSOLE 3584
#define IOT_STACK_CONSOLE_PEAK 2300 /* UART path incl. ECDSA verify */

/* ── NOT shrinkable ───────────────────────────────────────────────────────────
 * core_work used 4080 of 6144 -- only 2064 free, and it is the busiest task in the
 * firmware (message dispatch). Memory says plainly: do NOT shrink. Recorded here so the
 * host test can assert nobody does.
 *
 * It is also why "run the console on the core task to save its stack" does not work: the
 * console's measured peak is ~2.3 KB including ECDSA verify, which does not fit in 2064 B
 * of headroom, and growing core_work spends exactly the heap that idea was meant to save. */
#define IOT_STACK_CORE_WORK 6144
#define IOT_STACK_CORE_WORK_PEAK 4080 /* measured, ESP32-C3 */

/* IDF's own sys_evt had only 368 B free (2448 of 2304+512). Not ours to size, but recorded
 * so nobody "optimises" CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE downward. */
#define IOT_STACK_SYS_EVT_FREE_OBSERVED 368

#endif /* IOT_ESP32_TASK_STACKS_H */
