# Timeline Scheduler — FreeRTOS Time-Triggered Real-Time Kernel Extension

A deterministic, Time-Triggered Scheduler (TTS) built on top of FreeRTOS, targeting ARM Cortex-M microcontrollers (emulated via QEMU). The system enforces strict temporal isolation between Hard Real-Time (HRT) and Soft Real-Time (SRT) tasks within a cyclic Major Frame architecture, with full observability through a tracing subsystem and an automated Python test suite.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Project Structure](#project-structure)
4. [Scheduling Model](#scheduling-model)
5. [Core Components](#core-components)
6. [Task Configuration](#task-configuration)
7. [Tracing & Observability](#tracing--observability)
8. [Test Suite](#test-suite)
9. [Build & Run](#build--run)
10. [Configuration Reference](#configuration-reference)
11. [Known Limitations & Design Decisions](#known-limitations--design-decisions)

---

## Overview

This project extends the FreeRTOS kernel with a **Time-Triggered Architecture (TTA)** 
scheduler. Unlike standard priority-based schedulers, the TTA paradigm pre-assigns
every hard task to a fixed time slot within a repeating **Major Frame**. This eliminates
non-deterministic preemption and provides provable temporal guarantees 
— a requirement in safety-critical embedded domains (avionics, automotive, industrial control).

Key properties:

- **Hard Real-Time (HRT)** tasks run in pre-allocated, 
- non-overlapping sub-frame slots. Missing a deadline is detected, the task is immediately killed and info logged.
- **Soft Real-Time (SRT)** tasks execute in remaining slack time using a FIFO policy, and are preempted the instant an HRT task's slot begins.
- All memory is **statically allocated** — no heap usage at runtime.
- A **circular trace buffer** captures scheduling events with cycle-accurate timestamps.
- A **Python test runner** orchestrates build, QEMU emulation, log parsing, and automated pass/fail checks.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     Major Frame (N ticks)               │
│  ┌──────────┬──────────┬──────────┬────────────────────┐│
│  │  HRT: T1 │  HRT: T2 │  HRT: T3 │   Slack (SRT)      ││
│  │  [0..4]  │  [5..10] │  [13..14]│                    ││
│  └──────────┴──────────┴──────────┴────────────────────┘│
│         Sub-Frame 0        Sub-Frame 1      ...         │
└─────────────────────────────────────────────────────────┘
```

The scheduler is driven entirely by the **SysTick interrupt** 
(via FreeRTOS's `xPortSysTickHandler`). On every tick, the Timeline Tick Hook:

1. Enforces HRT deadlines (suspends overrunning tasks).
2. Activates HRT tasks whose start time matches the current tick.
3. Reclaims slack time for SRT task execution.
4. Resets all state at the Major Frame boundary for deterministic cyclic repetition.

---

## Project Structure

```
.
├── main.c                   # Entry point: UART init, scheduler setup, static task creation
├── timeline.c               # Frontend API: configuration binding, scheduler initialization
├── timeline_internal.c      # Backend engine: tick hook, schedule hook, task state machines
├── tasks_generated.c        # Auto-generated task configuration array (from JSON via Python tooling)
├── trace.c                  # Lock-free circular trace buffer (ISR-safe)
├── trace_dumper.c           # Highest-priority task that drains and formats trace events over UART
├── workloads.c              # Generic task wrapper + CPU-bound workload simulation (MAC loop)
├── startup.c                # Bare-metal vector table, Reset/HardFault handlers
├── uart.c                   # Minimal UART driver for QEMU MPS2+ AN385 target
│
├── Headers/                 # Header files for all above modules
│
├── integrated_test.py       # Main Python test runner (CLI entry point)
├── suite/                   # JSON test configuration files
│   └── *.json               # One file per test scenario
│
└── Tools/                   # Python test infrastructure modules
    ├── build.py             # Docker/native build and QEMU execution
    ├── suite.py             # Test config discovery and parsing
    ├── log_parser.py        # UART log parsing → structured metrics
    ├── checks.py            # Pass/fail check definitions
    ├── metrics_view.py      # Console metrics pretty-printer
    ├── reporting.py         # JSON/HTML report generation
    ├── visualization.py     # matplotlib chart generation (optional)
    ├── models.py            # Data models: TestMeta, TestMetrics, TestResult, CheckResult
    └── ui.py                # Console UI helpers (banners, prompts, colour output)
```

---

## Scheduling Model

### Major Frame & Sub-Frames

The schedule is defined by two parameters:

| Parameter            | Description                                     |
|----------------------|-------------------------------------------------|
| `ulMajorFrameTicks`  | Total duration of one repeating schedule cycle  |
| `ulSubFrameTicks`    | Duration of each sub-frame partition            |

The Major Frame is divided into equal sub-frames. Each HRT task must fit entirely within one sub-frame — it cannot cross a sub-frame boundary.
This constraint is validated statically at startup before the scheduler starts by the `xTimelineInternal_InitAndValidate()` function. The sub-frame ID for each task is computed as:

```
ulSubFrame_id = task.ulStart_time / ulSubFrameTicks
``` 

### HRT Task Slot Constraints

Each HRT task is assigned a `[ulStart_time, ulEnd_time)` interval (in ticks). The scheduler enforces at initialization:

- **Validity**: `ulStart_time < ulEnd_time`
- **No overlap** between any two HRT tasks (guaranteed by chronological sorting + sequential end-time check)
- **Sub-frame confinement**: `ulEnd_time ≤ (subframe_id + 1) * ulSubFrameTicks`
- **Major frame confinement**: `ulEnd_time ≤ ulMajorFrameTicks`

Any violation causes the system to halt with a diagnostic UART message before the first tick fires.

### HRT Execution Flow

```
SysTick ISR
    │
    ├─ [tick == task.ulStart_time] ──► Resume HRT task (bypasses FreeRTOS priority)
    │                                  Preempt any running SRT task
    │                                  Log: HRT START
    │
    ├─ [tick == task.ulEnd_time]   ──► Suspend HRT task if still running
    │                                  Log: DEADLINE MISS
    │
    └─ [task calls vTimelineMarkHRTComplete()] ──► Task voluntarily self-suspends
                                                   Log: HRT COMPLETE
                                                   Attempt SRT slack reclaim
```

### SRT Execution & Slack Reclaiming

SRT tasks are held in a FIFO queue built from their declaration order 
in `tasks_generated.c`. They execute only when no HRT task is active and they can 
cross sub-frame boundaries freely. The tick hook attempts to reclaim slack time for 
SRT execution whenever an HRT task completes early or when no HRT tasks are active.

| Event    | Trigger                                           | Log            |
|----------|---------------------------------------------------|----------------|
| Start    | SRT task receives CPU for the first time          | `SRT START`    |
| Preempt  | HRT slot begins while SRT is running              | `SRT PREEMPT`  |
| Resume   | HRT slot ends / completes early                   | `SRT RESUME`   |
| Complete | Task calls `vTimelineMarkSRTTaskCompleted()`       | `SRT COMPLETE` |
| Killed   | Task still running at Major Frame boundary        | `SRT KILLED`   |

A preempted SRT task is suspended (not destroyed) and takes priority over new queue entries when the CPU becomes available again.

### Schedule Hook

`xTimelineScheduleHook()` overrides the FreeRTOS task selection mechanism, bypassing the standard ready-list priority resolution entirely. It returns:

1. The active HRT task handle (if not suspended), **or**
2. The active SRT task handle (if not suspended), **or**
3. `NULL` → yields to the FreeRTOS idle task.

This achieves true time-triggered determinism independent of FreeRTOS priority assignments.

### Major Frame Reset

At `tick == ulMajorFrameTicks - 1` the engine:

1. Logs `DEADLINE MISS` for any HRT task still active.
2. Terminates and logs any running SRT task (`SRT KILLED`).
3. Rewinds the SRT FIFO queue pointer to the list head — O(1), no list rebuild.
4. Calls `vTaskTimelineReset()` on every task to restore its initial stack frame and program counter, ensuring true cyclic determinism across frames.

---

## Core Components

### `timeline.c` — Frontend API

| Function | Description |
|---|---|
| `pxGetTimelineConfig()` | Binds the auto-generated task arrays to the `TimelineConfig_t` struct |
| `vConfigureScheduler()` | Validates config, creates all tasks statically, suspends them, activates the engine |
| `prvTimeline_PrintSubFrames()` | Debug utility: prints sub-frame assignment over UART before first frame |

### `timeline_internal.c` — Scheduler Engine

| Function | Description |
|---|---|
| `xTimelineInternal_InitAndValidate()` | Sorts tasks, runs offline temporal constraint validation |
| `vTimelineInternal_Activate()` | Arms the tick hook and initialises the SRT FIFO queue |
| `xTimelineTickHook()` | Called on every SysTick; drives all scheduling decisions (phases 1–4) |
| `xTimelineScheduleHook()` | Overrides FreeRTOS task selection |
| `vTimelineMarkHRTComplete()` | Called by HRT tasks on voluntary early completion |
| `vTimelineMarkSRTTaskCompleted()` | Called by SRT tasks on completion |
| `vTimelineUpdateJitterStats()` | Updates per-task min/max dispatch latency in cycles |
| `pxTimeline_FindTaskByHandle()` | Maps a `TaskHandle_t` back to its `TimelineTaskConfig_t` |

#### Task Sorting Algorithm

At initialization, tasks are reordered using a **stable insertion sort** that enforces:

1. All HRT tasks precede all SRT tasks in the array.
2. HRT tasks are sorted chronologically by `ulStart_time`.
3. The relative declaration order of SRT tasks is preserved (FIFO guarantee).

The sort runs once, offline, before the scheduler starts — its O(N²) complexity has no runtime impact.

### `trace.c` — Trace Buffer

A lock-free, ISR-safe **circular ring buffer** of 8 192 `xTraceEvent_t` entries. Events are silently discarded on overflow rather than blocking — this preserves HRT timing guarantees at the cost of potential telemetry loss under high saturation. Writes use `portSET_INTERRUPT_MASK_FROM_ISR()` for safe concurrent access from both task and ISR contexts.

Each event stores:

| Field                    | Description                                            |
|--------------------------|--------------------------------------------------------|
| `ulTimestamp`            | FreeRTOS system tick counter at logging time           |
| `ulMajorFrameTimestamp`  | Tick offset within the current major frame             |
| `ucEventID`              | Event type enum value                                  |
| `ulData`                 | Payload: task handle, cycle count, or idle tick count  |

### `trace_dumper.c` — Trace Consumer

A dedicated FreeRTOS task for dumping trace events over UART.
It is crated in the main.c .
It drains the trace buffer in a polling loop, yielding with `vTaskDelay(1 ms)`.
In any case also the trace dumper is considered a soft real-time task, as it runs 
in the idle time between hard real-time tasks, and it is not allowed to preempt them.
It formats events into human-readable strings and outputs them over UART.

Kernel overhead is computed per frame as a percentage of the total cycle budget using 64-bit arithmetic to prevent overflow:

```
overhead% = (ulOverheadCycles × 10 000) / (ulMajorFrameTicks × CYCLES_PER_TICK)
```

The integer and fractional parts are separated for fixed-point formatted output (e.g., `0.17%`).

### `workloads.c` — Task Payload

`vGenericTaskWrapper` is the universal task function used by all configured tasks. It:

1. Reads `ulScalingFactor` from its static parameter pointer.
2. Calls `vSimulateEmbeddedWorkload(ulScalingFactor)` — a volatile Multiply-Accumulate loop representative of DSP / PID control workloads. The `volatile` qualifier prevents the compiler from optimising the loop away.
3. Reports completion to the timeline scheduler via `vTimelineMarkHRTComplete()` or `vTimelineMarkSRTTaskCompleted()` depending on task type.

### `startup.c` — Bare-Metal Bootstrap

Provides the ARM Cortex-M **vector table** (placed in `.isr_vector` section), mapping:

- `Reset_Handler` → calls `main()` directly.
- `HardFault_Handler` → captures registers from the fault stack frame and prints them over UART via `prvGetRegistersFromStack()`.
- FreeRTOS interrupt handlers: `vPortSVCHandler`, `xPortPendSVHandler`, `xPortSysTickHandler`.

### `uart.c` — UART Driver

Minimal polling UART driver targeting the **ARM MPS2+ AN385** QEMU model (UART0 at a fixed MMIO address). Initializes at 16× baud divisor and provides `UART_printf()` for null-terminated string output. No interrupts, no buffering.

---

## Task Configuration

Tasks are defined in `tasks_generated.c`, intended to be produced automatically by the Python tooling from a JSON schedule definition. Each entry uses `TimelineTaskConfig_t`:

```c
typedef struct {
    const char            *pcName;           // Task name string
    TaskFunction_t         pvTaskFunction;   // Task function pointer
    TaskType_t             xType;            // HRT_TASK or SRT_TASK
    uint32_t               xStackDepth;      // Stack size in words
    StaticTask_t          *pxTaskBuffer;     // Pointer to static TCB buffer
    StackType_t           *pxStackBuffer;    // Pointer to static stack buffer
    TaskHandle_t           xTaskHandle;      // Populated at runtime by vConfigureScheduler()
    const void            *pvTaskParams;     // Pointer to workload scaling factor
    uint32_t               ulStart_time;     // Slot start tick (HRT only)
    uint32_t               ulEnd_time;       // Slot end tick   (HRT only)
    uint32_t               ulSubframe_id;    // Computed at init from start_time / SubFrameTicks
    uint32_t               ulMaxDelay;       // Max observed dispatch jitter (cycles)
    uint32_t               ulMinDelay;       // Min observed dispatch jitter (cycles)
    ListItem_t             xSRTListItem;     // FreeRTOS list node for the SRT FIFO queue
} TimelineTaskConfig_t;
```

### Example Task Set

Major frame: **30 ticks** — Sub-frame: **5 ticks**

| Name | Type | Start | End | Workload Iterations |
|------|------|-------|-----|---------------------|
| HT1  | HRT  | 0     | 4   | 2 500               |
| HT2  | HRT  | 5     | 10  | 5 000               |
| HT3  | HRT  | 13    | 14  | 50                  |
| HT4  | HRT  | 15    | 17  | 220                 |
| HT5  | HRT  | 18    | 20  | 400                 |
| HT6  | HRT  | 20    | 24  | 4 000               |
| ST1  | SRT  | —     | —   | 5 000               |
| ST2  | SRT  | —     | —   | 7 000               |

> **Note:** HT6 starts exactly at tick 20, which is the start of sub-frame [20..24]. The sub-frame confinement check (`ulEnd_time > xSlotEnd`) correctly assigns it to this sub-frame. Designers should be aware that tasks starting exactly on a sub-frame boundary belong to the *new* sub-frame.

---

## Tracing & Observability

### UART Log Format

Each event is emitted as a tab-separated line:

```
[ <frame_tick>  <sys_tick> ] <EVENT>:   <task_name> ( SubFrame: <N> )   [extra fields]
```

### Sample Output

```
--- DEBUG: Sorted Task Array ---
[ 0 ] Name: HT1    Type: HRT   Start: 0
[ 1 ] Name: HT2    Type: HRT   Start: 5
...
[ 7 ] Name: ST2    Type: SRT   Start: 0
--------------------------------

[ 00000  00001 ] HRT START:    HT1 ( SubFrame: 0 )   Deadline @00004   delay max:14 cycles   delay min: 14 cycles
[ 00004  00005 ] HRT COMPLETE: HT1 ( SubFrame: 0 )
[ 00005  00006 ] HRT START:    HT2 ( SubFrame: 1 )   Deadline @00010   delay max:0 cycles    delay min: 0 cycles
[ 00010  00011 ] DEADLINE MISS!    HT2
[ 00010  00011 ] SRT START:    ST1 ( SubFrame: 2 )
[ 00013  00014 ] SRT PREEMPT:  ST1 ( SubFrame: 2 )
[ 00014  00015 ] SRT RESUME:   ST1 ( SubFrame: 2 )
[ 00018  00019 ] SRT COMPLETE: ST1 ( SubFrame: 3 )
[ 00029  00030 ] CPU STATS: Idle Ticks = 2
[ 00029  00030 ] KERNEL OVERHEAD: 1480 Cycles (0.20%)

[ 00029  00030 ] MAJOR FRAME
```

### Event Reference

| Event ID               | Name               | Description                                        |
|------------------------|--------------------|----------------------------------------------------|
| `eTraceHrtStart`       | `HRT START`        | Tick hook activates an HRT task                    |
| `eTraceHrtComplete`    | `HRT COMPLETE`     | Task self-reports early completion                 |
| `eTraceDeadlineMiss`   | `DEADLINE MISS`    | HRT task active at its `ulEnd_time`                |
| `eTraceSrtStart`       | `SRT START`        | New SRT task gets CPU for first time               |
| `eTraceSrtPreempt`     | `SRT PREEMPT`      | SRT suspended for incoming HRT                     |
| `eTraceSrtResume`      | `SRT RESUME`       | Previously preempted SRT resumes                   |
| `eTraceSrtComplete`    | `SRT COMPLETE`     | SRT task reports completion                        |
| `eTraceSrtTerminated`  | `SRT KILLED`       | SRT task reset at frame boundary                   |
| `eTraceMajorFrame`     | `MAJOR FRAME`      | Frame boundary reached                             |
| `eTraceIdleStats`      | `CPU STATS`        | Idle tick count for the frame                      |
| `eTraceOverheadStats`  | `KERNEL OVERHEAD`  | Scheduler cycle cost for the frame                 |
| `eTraceJitterViolation`| `JITTER VIOLATION` | Dispatch latency exceeded threshold                |

---
The output is also redirected to a `uart.log` file with the followings commands for post-mortem
analysis by the Python test suite.
```
qemu_start:
mkdir -p $(LOG_DIR)
qemu-system-arm \
-machine $(MACHINE) \
-cpu $(CPU) \
-kernel $(ELF) \
-monitor none \
-nographic \
-semihosting \
-serial stdio \  → redirects UART output to the console
-icount shift=6,align=off,sleep=on \
| tee $(LOG_DIR)/uart.log  → duplicates console output to uart.log 
```


## Test Suite

The test infrastructure is a Python 3 application centred on `integrated_test.py` and the `Tools/` package.

### Module Responsibilities

| Module             | Responsibility                                                                               |
|--------------------|----------------------------------------------------------------------------------------------|
| `build.py`         | Invokes `make` (via Docker or natively) and runs QEMU, capturing UART output to `uart.log`  |
| `suite.py`         | Discovers `*.json` files in the suite directory, parses `TestMeta`, reads timing config      |
| `log_parser.py`    | Reads `uart.log` line by line, extracts `TestMetrics` (frame count, miss count, jitter, etc.)|
| `checks.py`        | Evaluates a list of `CheckResult` entries (pass/fail) against `TestMeta` + `TestMetrics`    |
| `metrics_view.py`  | Pretty-prints `TestMetrics` to the console                                                   |
| `reporting.py`     | Serialises `TestResult` objects to JSON files; builds suite summary                         |
| `visualization.py` | Generates timeline and jitter charts using `matplotlib` (optional dependency)                |
| `models.py`        | Pure data classes: `TestMeta`, `TestMetrics`, `TestResult`, `CheckResult`                    |
| `ui.py`            | Console helpers: `banner()`, `header()`, `section()`, `ok()`, `fail()`, `info()`, `pause()` |

### CLI Reference

```bash
# Run the full suite (interactive, via Docker)
python3 integrated_test.py

# Run the full suite in CI mode on native Linux
python3 integrated_test.py --linux --no-pause

# Run a single test by its JSON id field
python3 integrated_test.py --config baseline

# Analyze an existing uart.log without building or running
python3 integrated_test.py --action analyze

# Build only (no QEMU, no analysis)
python3 integrated_test.py --action build

# Run QEMU only (firmware must already be built)
python3 integrated_test.py --action test

# Clean build artefacts
python3 integrated_test.py --action clean

# List all available test configurations
python3 integrated_test.py --list

# Skip clean step between tests
python3 integrated_test.py --no-clean

# Disable chart generation (e.g. on headless CI)
python3 integrated_test.py --no-charts

# Disable report file saving
python3 integrated_test.py --no-save-report

# Point to a custom suite directory
python3 integrated_test.py --suite-dir /path/to/my/suite
```

### Test Configuration JSON Schema

```json
{
  "id": "baseline",
  "name": "Baseline Schedule",
  "description": "Default task set — zero deadline misses expected.",
  "major_frame_ms": 100,
  "minor_frame_ms": 10
}
```

The `id` field is used for `--config` selection and for naming report files.

### Execution Flow per Test

```
IntegratedTestRunner.run_single(config_path)
    │
    ├─ 1. clean()          →  make clean
    ├─ 2. build(config)    →  make (injects task config)
    ├─ 3. run_qemu()       →  qemu-system-arm → logs/uart.log
    ├─ 4. analyze(meta)
    │       ├─ log_parser.parse_log()    → TestMetrics
    │       ├─ metrics_view.print()      → console output
    │       ├─ checks.run_checks()       → [CheckResult, ...]
    │       └─ visualization.generate() → charts/ (if enabled)
    └─ 5. reporting.save_result()        → test_reports/<ts>/
```

### Report Layout

```
test_reports/
└── 20240315_143022/
    ├── baseline.json
    ├── deadline_stress.json
    ├── suite_summary.json
    └── charts/
        ├── 20240315_143022_Baseline Schedule_timeline.png
        └── 20240315_143022_Baseline Schedule_jitter.png
```

---

## Build & Run

### Prerequisites

**ARM cross-compiler:**
```bash
sudo apt install gcc-arm-none-eabi
```

**QEMU with ARM system emulation:**
```bash
sudo apt install qemu-system-arm
```

**Python 3.9+ (charts are optional):**
```bash
pip install matplotlib numpy
```

Alternatively, use the **Docker path** (default when `--linux` is not passed). `Tools/build.py` manages a container with all dependencies pre-installed — no local toolchain required.

### Manual Build

```bash
cd Demo
make clean
make
```

### Manual QEMU Execution

```bash
qemu-system-arm \
  -machine mps2-an385 \
  -cpu cortex-m3 \
  -kernel build/RTOSDemo.axf \
  -nographic \
  -serial file:../logs/uart.log \
  -semihosting-config enable=on,target=native
```

### Automated via Test Runner

```bash
# Full suite, native Linux, CI mode
python3 integrated_test.py --linux --no-pause

# Single scenario
python3 integrated_test.py --linux --no-pause --config baseline
```

---

## Configuration Reference

### `TimelineConfig_t`

```c
typedef struct {
    uint32_t              ulMajorFrameTicks;  // Total ticks per cycle
    uint32_t              ulSubFrameTicks;    // Ticks per sub-frame
    uint32_t              ulNumTasks;         // Total task count
    TimelineTaskConfig_t *pxTasks;            // Pointer to task array
} TimelineConfig_t;
```

### Key FreeRTOS `FreeRTOSConfig.h` Parameters

| Parameter                          | Typical Value | Notes                                                  |
|------------------------------------|---------------|--------------------------------------------------------|
| `configTICK_RATE_HZ`               | 1 000         | 1 ms per tick                                          |
| `configCPU_CLOCK_HZ`               | 25 000 000    | 25 MHz (QEMU MPS2+ AN385)                              |
| `configSUPPORT_STATIC_ALLOCATION`  | 1             | **Required** — no dynamic heap used                    |
| `configUSE_TICK_HOOK`              | 1             | Required for `xTimelineTickHook()`                     |
| `configMAX_PRIORITIES`             | 8             | Trace Dumper runs at `configMAX_PRIORITIES - 1`        |
| `configMINIMAL_STACK_SIZE`         | 128           | Stack size in words for the Idle task                  |

---

## Known Limitations & Design Decisions

**Static allocation only.** All tasks, stacks, and TCBs are statically allocated. This is intentional: it eliminates heap fragmentation, bounds initialization time, and makes worst-case memory usage statically provable. `vApplicationGetIdleTaskMemory()` is implemented accordingly.

**Trace buffer overflow is silent.** The 8 192-entry ring buffer discards new events when full rather than blocking. This preserves HRT timing guarantees at the cost of potential telemetry loss under very high event rates (many short tasks, high tick rate).

**Jitter measurement via SysTick counter.** The ARM SysTick counter at `0xE000E018` counts down from `CYCLES_PER_TICK - 1` to 0 each tick. The delta calculation handles counter wrap-around explicitly:

```c
if (ulEnd <= ulStart)
    ulDelta = ulStart - ulEnd;
else
    ulDelta = ulStart + (CYCLES_PER_TICK - 1 - ulEnd) + 1;
```

**Custom kernel extensions required.** The project relies on non-standard FreeRTOS functions: `xTaskIsSuspendedFromISR()`, `vTaskTimelineSuspend()`, `vTaskTimelineSuspendFromISR()`, `vTaskTimelineResume()`, `vTaskTimelineResumeFromISR()`, and `vTaskTimelineReset()`. These must be patched into the FreeRTOS kernel sources before building.

**SRT FIFO via list pointer rewind.** The SRT queue reuses the FreeRTOS `List_t` structure. At each frame reset, instead of destroying and rebuilding the list, the internal `pxIndex` pointer is simply rewound to the list's anchor element — O(1) reset at the cost of tight coupling to FreeRTOS list internals.

**`vTaskTimelineReset()` semantics.** This custom function reinitialises a task's stack frame and resets its program counter to the task function entry point, providing true cyclic reset semantics without tearing down and recreating the task (which would require dynamic allocation and re-registration with the scheduler).

---

## License

Based on the **FreeRTOS Kernel**, licensed under the **MIT License**.  
Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.

See individual source files for the full license text and copyright notices.
