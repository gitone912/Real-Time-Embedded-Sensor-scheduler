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

/*
 * Precise Timeline-Driven Scheduler API
 */

#ifndef TIMELINE_H
#define TIMELINE_H

#include "FreeRTOS.h"
#include "task.h"

/*-----------------------------------------------------------*/

/**
 * @brief Categorization of task timing requirements within the time-triggered architecture.
 */
typedef enum
{
    HRT_TASK, /**< Hard Real-Time task, strictly managed by the timeline with defined start and end slots. */
    SRT_TASK  /**< Soft Real-Time task, executed during system idle time utilizing a FIFO policy. */
} TimelineTaskType_t;

/**
 * @brief Configuration structure for a single Time-Triggered task.
 *
 * This structure encapsulates all static and runtime metadata required to schedule
 * and manage a task within the Time-Triggered Architecture. It is evaluated
 * during the kernel initialization phase.
 */
typedef struct
{
    const char * pcName;            /**< Descriptive name for debug and tracing purposes. */
    TaskFunction_t pvTaskFunction;  /**< Pointer to the C function implementing the task payload. */
    TimelineTaskType_t xType;       /**< Task categorization as HRT or SRT. */
    TaskHandle_t xTaskHandle;       /**< FreeRTOS handle, populated dynamically during system initialization. */
    const void * pvTaskParams;      /**< Pointer to task parameters (e.g., workload scaling factors). */

    /* Static Memory Allocation Parameters */
    configSTACK_DEPTH_TYPE xStackDepth; /**< Stack depth specified in words, not bytes. */
    StaticTask_t * pxTaskBuffer;        /**< Pointer to the statically allocated TCB (Task Control Block) memory. */
    StackType_t * pxStackBuffer;        /**< Pointer to the statically allocated stack memory area. */

    /* Temporal Execution Parameters */
    uint32_t ulStart_time;  /**< Absolute activation tick relative to the Major Frame boundary. */
    uint32_t ulEnd_time;    /**< Absolute deadline tick relative to the Major Frame boundary. */
    uint32_t ulSubframe_id; /**< Logical sub-frame grouping identifier (Calculated automatically by the kernel). */
    uint32_t ulMaxDelay;    /**< Maximum execution delay recorded during runtime. */
    uint32_t ulMinDelay;    /**< Minimum execution delay recorded during runtime. */

    /* Internal List Management */
    ListItem_t xSRTListItem; /**< Linked list node for enqueueing the task within the SRT timeline ready list. */
} TimelineTaskConfig_t;

/**
 * @brief Global configuration structure for the Time-Triggered Scheduler.
 * * Defines the entire major frame timeline. This must be populated before 
 * transferring control to the FreeRTOS scheduler.
 */
typedef struct
{
    uint32_t ulMajorFrameTicks;      /**< Total duration of the global scheduling cycle in kernel ticks. */
    uint32_t ulSubFrameTicks;        /**< Granularity of a single scheduling slot in kernel ticks. */
    uint32_t ulNumTasks;             /**< Total number of tasks defined in the timeline array. */
    TimelineTaskConfig_t * pxTasks;  /**< Pointer to the first element of the static task configuration array. */
} TimelineConfig_t;

/*-----------------------------------------------------------*/

/**
 * @brief Initializes the Time-Triggered scheduling environment.
 *
 * This system call validates the provided timeline configuration, statically
 * allocates necessary FreeRTOS internal data structures, and prepares the
 * execution engine. It incorporates internal logging (e.g., sub-frame printout)
 * to verify correct initialization before the global timeline starts.
 *
 * @param[in] pxConfig Constant pointer to the user-defined timeline configuration structure.
 */
void vConfigureScheduler( const TimelineConfig_t * pxConfig );

/**
 * @brief Retrieves the active timeline configuration.
 *
 * Primarily used by kernel internal hooks to query the current timing constraints
 * without duplicating configuration memory.
 *
 * @return A constant pointer to the currently active timeline configuration.
 */
const TimelineConfig_t * pxGetTimelineConfig( void );

#endif /* TIMELINE_H */
