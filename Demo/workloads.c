/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
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

#include "Headers/workloads.h"
#include "Headers/timeline.h"
#include "Headers/timeline_internal.h"

/**
 * @brief FreeRTOS Task Wrapper for both Hard Real-Time and Soft Real-Time task execution.
 * * This task extracts the configuration parameter, executes the deterministic
 * CPU-bound workload, and signals the timeline scheduler upon completion.
 * * @param[in] pvParameters Pointer to the statically allocated scaling factor.
 */
void vGenericTaskWrapper( void *pvParameters )
{
    uint32_t ulScalingFactor;
    TaskHandle_t xCurrentTask;
    const TimelineConfig_t *pxConfig;
    const TimelineTaskConfig_t *pxTaskConfig;

    /* Validate parameter pointer and extract the scaling factor */
    configASSERT( pvParameters != NULL );
    ulScalingFactor = *( ( uint32_t * ) pvParameters );

    /* Dynamically determine task identity from the Kernel */
    pxConfig = pxGetTimelineConfig();
    xCurrentTask = xTaskGetCurrentTaskHandle();
    pxTaskConfig = pxTimeline_FindTaskByHandle( pxConfig, xCurrentTask );

    /* If the task is not in the timeline, halt the system */
    configASSERT( pxTaskConfig != NULL );

    /* Infinite loop for standard FreeRTOS task architecture. */
    for( ;; )
    {
        /* Execute the simulated payload */
        vSimulateEmbeddedWorkload( ulScalingFactor );

        /* Signal completion to the Time-Triggered Scheduler based on type */
        if( pxTaskConfig->xType == HRT_TASK )
        {
            vTimelineMarkHRTComplete();
        }
        else
        {
            vTimelineMarkSRTTaskCompleted();
        }
    }
}

/**
 * @brief Simulates a deterministic CPU-bound workload.
 * * This function performs a series of Multiply-Accumulate (MAC) operations,
 * which are highly representative of Digital Signal Processing (DSP) algorithms
 * or control loops (like PID) common in Cortex-M microcontrollers.
 * The use of the 'volatile' qualifier is mandatory to prevent aggressive
 * compiler optimizations from removing the execution loop entirely.
 * * @param[in] ulWorkloadScalingFactor The number of iterations to perform.
 * This parameter determines the computational time.
 * It must be profiled and calibrated to map
 * iterations to physical time (e.g., CPU ticks or ms).
*/
void vSimulateEmbeddedWorkload( uint32_t ulWorkloadScalingFactor )
{
    volatile uint32_t ulAccumulator = 0UL;
    volatile uint32_t ulMultiplier1 = 13UL;
    volatile uint32_t ulMultiplier2 = 7UL;

    for( uint32_t i = 0UL; i < ulWorkloadScalingFactor; i++ )
    {
        ulAccumulator += ( ulMultiplier1 * ulMultiplier2 );

        /* State mutation to prevent loop unrolling and constant folding optimizations */
        ulMultiplier1++;
        ulMultiplier2--;
    }
}