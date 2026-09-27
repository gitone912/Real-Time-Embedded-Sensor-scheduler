#!/usr/bin/env python3
import json
from pathlib import Path

def c_ms_to_ticks(ms: int) -> str:
    return f"pdMS_TO_TICKS({int(ms)})"

def sanitize(name: str) -> str:
    out = []
    for ch in name:
        out.append(ch if ch.isalnum() else "_")
    return "".join(out)

def main():
    script_dir = Path(__file__).resolve().parent
    demo_dir = script_dir.parent  # Demo/
    cfg_path = demo_dir / "config" / "tasks.json"

    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    h_path = demo_dir / "Headers/tasks_generated.h"
    c_path = demo_dir / "tasks_generated.c"

    # --- generate header ---
    h = []
    h.append("#ifndef TASKS_GENERATED_H")
    h.append("#define TASKS_GENERATED_H")
    h.append("")
    h.append('#include "Headers/timeline.h"')
    h.append("")
    h.append("extern TimelineTaskConfig_t xTaskConfig[];")
    h.append("extern const uint32_t ulNumTasks;")
    h.append("")
    h.append("#endif")
    h.append("")
    h_path.write_text("\n".join(h), encoding="utf-8")

    # --- generate C file ---
    c = []
    c.append('#include "Headers/tasks_generated.h"')
    c.append('#include "Headers/HRT_task.h"')
    c.append('#include "Headers/SRT_task.h"')
    c.append("")

    # Wrappers/task entry points (devono esistere nel tuo codice C)
    c.append("void vTaskControl(void *pvParameters);")
    c.append("void vTaskCrypto(void *pvParameters);")
    c.append("")

    # 1) Parametri statici generati
    #    Se workload manca: task_hr -> CONTROL, task_srt -> CRYPTO
    for t in cfg["tasks"]:
        name = t["name"]
        safe = sanitize(name)
        code = t.get("code", "task_hr")
        workload = (t.get("workload") or ("CONTROL" if code == "task_hr" else "CRYPTO")).upper()
        params = t.get("params", {}) or {}

        if workload == "CONTROL":
            iters = int(params.get("iterations", 500))
            inner = int(params.get("innerLoop", 20))
            budget_ms = int(params.get("budgetMs", 0))
            budget = "0" if budget_ms == 0 else c_ms_to_ticks(budget_ms)

            c.append(f"static const SimWorkControlParams_t xParams_{safe} = {{")
            c.append(f"    .ulIterations = {iters}u,")
            c.append(f"    .ulInnerLoop = {inner}u,")
            c.append(f"    .xBudgetTicks = {budget},")
            c.append("};")
            c.append("")

        elif workload == "CRYPTO":
            payload = int(params.get("payloadBytes", 512))
            rounds = int(params.get("rounds", 80))
            budget_ms = int(params.get("budgetMs", 0))
            budget = "0" if budget_ms == 0 else c_ms_to_ticks(budget_ms)

            c.append(f"static const SimWorkCryptoParams_t xParams_{safe} = {{")
            c.append(f"    .ulPayloadBytes = {payload}u,")
            c.append(f"    .ulRounds = {rounds}u,")
            c.append(f"    .xBudgetTicks = {budget},")
            c.append("};")
            c.append("")
        else:
            # workload sconosciuto -> niente params
            pass

    # 2) Task table
    c.append("TimelineTaskConfig_t xTaskConfig[] = {")
    for t in cfg["tasks"]:
        name = t["name"]
        typ = t["type"]
        start = int(t.get("start", 0))
        end = int(t.get("end", 0))
        subframe = int(t.get("subframe", 0))
        safe = sanitize(name)

        code = t.get("code", "task_hr")
        workload = (t.get("workload") or ("CONTROL" if code == "task_hr" else "CRYPTO")).upper()

        # entry point in base al workload (puoi anche basarti su code, ma così è più flessibile)
        if workload == "CONTROL":
            fn = "vTaskControl"
            params_ptr = f"&xParams_{safe}"
        elif workload == "CRYPTO":
            fn = "vTaskCrypto"
            params_ptr = f"&xParams_{safe}"
        else:
            fn = "vTaskControl" if code == "task_hr" else "vTaskCrypto"
            params_ptr = "NULL"

        xType = "HRT_TASK" if typ == "HRT" else "SRT_TASK"

        c.append("    {")
        c.append(f'        .pcName = "{name}",')
        c.append(f"        .pvTaskFunction = {fn},")
        c.append(f"        .xType = {xType},")
        c.append(f"        .ulStart_time = {start},")
        c.append(f"        .ulEnd_time = {end},")
        c.append(f"        .ulSubframe_id = {subframe},")
        c.append("        .xTaskHandle = NULL,")
        c.append(f"        .pvTaskParams = {params_ptr},")
        c.append("    },")
    c.append("};")
    c.append("")
    c.append("const uint32_t ulNumTasks = (uint32_t)(sizeof(xTaskConfig) / sizeof(xTaskConfig[0]));")
    c.append("")
    c_path.write_text("\n".join(c), encoding="utf-8")

    print(f"Generated: {c_path} and {h_path}")

if __name__ == "__main__":
    main()