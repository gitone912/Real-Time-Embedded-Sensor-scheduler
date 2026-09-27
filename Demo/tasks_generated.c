/*
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
 */

/**
 * @file tasks_generated.c
 * @brief Auto-generated timeline task instances.
 */
#include "Headers/tasks_generated.h"

/* Global Timeline Configuration */
const uint32_t ulMajorFrameTicks = 200U;
const uint32_t ulSubFrameTicks = 20U;

/* External Task Function Prototypes. */
extern void vGenericTaskWrapper( void *pvParameters );
extern void vSensorProcessingTask( void *pvParameters );
extern void vSensorTask( void *pvParameters );

/* Static Parameter Allocations. */
static const uint32_t ulParam_HT1 = 5000UL;
static StaticTask_t xTCB_HT1;
static StackType_t xStack_HT1[ 128 ];
static const uint32_t ulParam_HT2 = 800UL;
static StaticTask_t xTCB_HT2;
static StackType_t xStack_HT2[ 128 ];
static const uint32_t ulParam_HT3 = 6000UL;
static StaticTask_t xTCB_HT3;
static StackType_t xStack_HT3[ 128 ];
static const uint32_t ulParam_ST1 = 8000UL;
static StaticTask_t xTCB_ST1;
static StackType_t xStack_ST1[ 512 ];
static const uint32_t ulParam_ST2 = 16900UL;
static StaticTask_t xTCB_ST2;
static StackType_t xStack_ST2[ 512 ];
static const uint32_t ulParam_ST3 = 16900UL;
static StaticTask_t xTCB_ST3;
static StackType_t xStack_ST3[ 512 ];

/* Timeline Scheduler Configuration Array. */
TimelineTaskConfig_t xTaskConfig[] = {
    {
        .pcName = "HT1",
        .pvTaskFunction = vGenericTaskWrapper,
        .xType = HRT_TASK,
        .xStackDepth = 128,
        .pxTaskBuffer = &xTCB_HT1,
        .pxStackBuffer = xStack_HT1,
        .xTaskHandle = NULL,
        .pvTaskParams = (const void *)&ulParam_HT1,
        .ulStart_time = 12U,
        .ulEnd_time = 17U,
        .ulSubframe_id = 0U,
        .ulMaxDelay = 0U,
        .ulMinDelay = 0U
    },
    {
        .pcName = "HT2",
        .pvTaskFunction = vGenericTaskWrapper,
        .xType = HRT_TASK,
        .xStackDepth = 128,
        .pxTaskBuffer = &xTCB_HT2,
        .pxStackBuffer = xStack_HT2,
        .xTaskHandle = NULL,
        .pvTaskParams = (const void *)&ulParam_HT2,
        .ulStart_time = 23U,
        .ulEnd_time = 30U,
        .ulSubframe_id = 0U,
        .ulMaxDelay = 0U,
        .ulMinDelay = 0U
    },
    {
        .pcName = "HT3",
        .pvTaskFunction = vGenericTaskWrapper,
        .xType = HRT_TASK,
        .xStackDepth = 128,
        .pxTaskBuffer = &xTCB_HT3,
        .pxStackBuffer = xStack_HT3,
        .xTaskHandle = NULL,
        .pvTaskParams = (const void *)&ulParam_HT3,
        .ulStart_time = 37U,
        .ulEnd_time = 40U,
        .ulSubframe_id = 0U,
        .ulMaxDelay = 0U,
        .ulMinDelay = 0U
    },
    {
        .pcName = "ST1",
        .pvTaskFunction = vSensorTask,
        .xType = SRT_TASK,
        .xStackDepth = 512,
        .pxTaskBuffer = &xTCB_ST1,
        .pxStackBuffer = xStack_ST1,
        .xTaskHandle = NULL,
        .pvTaskParams = (const void *)&ulParam_ST1,
        .ulStart_time = 0U,
        .ulEnd_time = 0U,
        .ulSubframe_id = 0U,
        .ulMaxDelay = 0U,
        .ulMinDelay = 0U
    },
    {
        .pcName = "ST2",
        .pvTaskFunction = vSensorProcessingTask,
        .xType = SRT_TASK,
        .xStackDepth = 512,
        .pxTaskBuffer = &xTCB_ST2,
        .pxStackBuffer = xStack_ST2,
        .xTaskHandle = NULL,
        .pvTaskParams = (const void *)&ulParam_ST2,
        .ulStart_time = 0U,
        .ulEnd_time = 0U,
        .ulSubframe_id = 0U,
        .ulMaxDelay = 0U,
        .ulMinDelay = 0U
    },
    {
        .pcName = "ST3",
        .pvTaskFunction = vGenericTaskWrapper,
        .xType = SRT_TASK,
        .xStackDepth = 512,
        .pxTaskBuffer = &xTCB_ST3,
        .pxStackBuffer = xStack_ST3,
        .xTaskHandle = NULL,
        .pvTaskParams = (const void *)&ulParam_ST3,
        .ulStart_time = 0U,
        .ulEnd_time = 0U,
        .ulSubframe_id = 0U,
        .ulMaxDelay = 0U,
        .ulMinDelay = 0U
    },
};

const uint32_t ulNumTasks = ( uint32_t )( sizeof( xTaskConfig ) / sizeof( xTaskConfig[ 0 ] ) );
