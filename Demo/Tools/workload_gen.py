#!/usr/bin/env python3
import json
import argparse
from pathlib import Path

FREERTOS_LICENSE_HEADER = """/*
 * FreeRTOS Kernel <PROJECT 1 EXTENSION>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */"""

def sanitize(name: str) -> str:
    out = []
    for ch in name:
        out.append(ch if ch.isalnum() else "_")
    return "".join(out)

def main():
    parser = argparse.ArgumentParser(description="Generate timeline task configuration from JSON")
    parser.add_argument(
        "--input",
        default=None,
        help="Path to input JSON file (default: config/tasks.json relative to Demo/)"
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    demo_dir = script_dir.parent

    if args.input:
        cfg_path = Path(args.input)
        if not cfg_path.is_absolute():
            cfg_path = demo_dir / args.input
    else:
        exit("Error: No input JSON file specified. Use --input <path> or place config/tasks.json in the Demo/ directory.")

    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    # --- LETTURA DEI PARAMETRI DI FRAME DAL JSON ---
    major_frame = int(cfg.get("major_frame", 100))
    subframe = int(cfg.get("subframe", 10))

    h_path = demo_dir / "Headers" / "tasks_generated.h"
    c_path = demo_dir / "tasks_generated.c"

    # --- Generate Header ---
    h = []
    h.append(FREERTOS_LICENSE_HEADER)
    h.append("")
    h.append("/**")
    h.append(" * @file tasks_generated.h")
    h.append(" * @brief Auto-generated timeline task configurations.")
    h.append(" */")
    h.append("#ifndef TASKS_GENERATED_H")
    h.append("#define TASKS_GENERATED_H")
    h.append("")
    h.append('#include "timeline.h"')
    h.append('#include "workloads.h"')
    h.append("")
    h.append("extern TimelineTaskConfig_t xTaskConfig[];")
    h.append("extern const uint32_t ulNumTasks;")
    h.append("extern const uint32_t ulMajorFrameTicks;")
    h.append("extern const uint32_t ulSubFrameTicks;")
    h.append("")
    h.append("#endif /* TASKS_GENERATED_H */")
    h.append("")

    h_path.parent.mkdir(parents=True, exist_ok=True)
    h_path.write_text("\n".join(h), encoding="utf-8")

    # --- Generate C File ---
    c = []
    c.append(FREERTOS_LICENSE_HEADER)
    c.append("")
    c.append("/**")
    c.append(" * @file tasks_generated.c")
    c.append(" * @brief Auto-generated timeline task instances.")
    c.append(" */")
    c.append('#include "Headers/tasks_generated.h"')
    c.append("")

    # --- ESPORTAZIONE DELLE VARIABILI GLOBALI ---
    c.append("/* Global Timeline Configuration */")
    c.append(f"const uint32_t ulMajorFrameTicks = {major_frame}U;")
    c.append(f"const uint32_t ulSubFrameTicks = {subframe}U;")
    c.append("")

    unique_functions = set()
    for t in cfg.get("tasks", []):
        func_name = t.get("function", "vGenericTimelineTask")
        unique_functions.add(func_name)

    c.append("/* External Task Function Prototypes. */")
    for func in sorted(unique_functions):
        c.append(f"extern void {func}( void *pvParameters );")
    c.append("")

    c.append("/* Static Parameter Allocations. */")
    for t in cfg.get("tasks", []):
        name = t.get("name", "Unknown")
        safe_name = sanitize(name)
        workload = t.get("workload_factor", 1000)
        stack = int(t.get("stack", 128)) 

        c.append(f"static const uint32_t ulParam_{safe_name} = {workload}UL;")
        c.append(f"static StaticTask_t xTCB_{safe_name};")
        c.append(f"static StackType_t xStack_{safe_name}[ {stack} ];")
    c.append("")

    c.append("/* Timeline Scheduler Configuration Array. */")
    c.append("TimelineTaskConfig_t xTaskConfig[] = {")
    for t in cfg.get("tasks", []):
        name = t.get("name", "Unknown")
        safe_name = sanitize(name)
        typ = t.get("type", "SRT")
        func = t.get("function", "vGenericTimelineTask")

        start = int(t.get("start", 0))
        end = int(t.get("end", 0))

        stack = int(t.get("stack", 128))
        max_delay = int(t.get("max_delay", 0))
        min_delay = int(t.get("min_delay", 0))

        xType = "HRT_TASK" if typ == "HRT" else "SRT_TASK"

        c.append("    {")
        c.append(f'        .pcName = "{name}",')
        c.append(f"        .pvTaskFunction = {func},")
        c.append(f"        .xType = {xType},")
        c.append(f"        .xStackDepth = {stack},")
        c.append(f"        .pxTaskBuffer = &xTCB_{safe_name},")
        c.append(f"        .pxStackBuffer = xStack_{safe_name},")
        c.append(f"        .xTaskHandle = NULL,")
        c.append(f"        .pvTaskParams = (const void *)&ulParam_{safe_name},")
        c.append(f"        .ulStart_time = {start}U,")
        c.append(f"        .ulEnd_time = {end}U,")
        c.append(f"        .ulSubframe_id = 0U,")
        c.append(f"        .ulMaxDelay = {max_delay}U,")
        c.append(f"        .ulMinDelay = {min_delay}U")
        c.append("    },")
    c.append("};")
    c.append("")
    c.append("const uint32_t ulNumTasks = ( uint32_t )( sizeof( xTaskConfig ) / sizeof( xTaskConfig[ 0 ] ) );")
    c.append("")

    c_path.write_text("\n".join(c), encoding="utf-8")
    print(f"Generated successfully:\n  -> {c_path}\n  -> {h_path}")

if __name__ == "__main__":
    main()