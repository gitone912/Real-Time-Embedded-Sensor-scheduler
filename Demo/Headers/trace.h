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

#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timeline.h" 

/**
 * @enum eTraceEventID_t
 * @brief Identifiers for kernel tracing events.
 *
 * These events are logged with tick-level precision to monitor the execution
 * of the Timeline-Driven Scheduler, ensuring deterministic behavior and
 * recording deadline violations for HRT tasks.
 */
typedef enum eTRACE_EVENT_ID
{
    eTraceCtxSwitchIn,     /**< Context switch to a new task occurred. */
    eTraceHrtStart,        /**< Hard Real-Time task slot activated. */
    eTraceHrtComplete,     /**< Hard Real-Time task completed within deadline. */
    eTraceDeadlineMiss,    /**< HRT task exceeded its slot and was terminated. */
    eTraceSrtStart,        /**< Soft Real-Time task started executing in idle time. */
    eTraceSrtPreempt,      /**< Soft Real-Time task preempted by an HRT task. */
    eTraceSrtResume,       /**< Soft Real-Time task resumed execution. */
    eTraceSrtComplete,     /**< Soft Real-Time task completed execution. */
    eTraceSrtTerminated,   /**< SRT task forcefully terminated at Major Frame boundary. */
    eTraceMajorFrame,      /**< Start of a new Major Frame boundary. */
    eTraceIdleStats,       /**< Logging idle time statistics. */
    eTraceOverheadStats,   /**< Reporting kernel overhead cycles. */
    eTraceJitterViolation  /**< Jitter exceeded the maximum allowed threshold. */
} eTraceEventID_t;

/**
 * @struct xTraceEvent_t
 * @brief Represents a single trace log entry.
 *
 * The structure is optimized for size (12 bytes) to minimize memory footprint
 * and bus utilization during high-frequency kernel logging operations.
 */
typedef struct xTRACE_EVENT
{
    uint32_t ulTimestamp;           /**< System tick count at the time of the event. */
    uint32_t ulMajorFrameTimestamp; /**< Relative tick count within the current Major Frame. */
    uint8_t ucEventID;              /**< The event identifier (eTraceEventID_t). */
    uint32_t ulData;                /**< Contextual payload (e.g., Task Handle, Subframe ID, or Cycle Count). */
} xTraceEvent_t;

/*-----------------------------------------------------------*/

/* API FUNCTIONS */

/**
 * @brief Initializes the tracing subsystem.
 *
 * Resets the ring buffer pointers and initializes the hardware cycle counters (DWT)
 * required for high-resolution jitter and overhead measurements.
 */
void vTraceInit(void);

/**
 * @brief Logs an event to the trace buffer (O(1) execution time).
 *
 * @param ulMajorFrameTimestamp The current tick offset within the major frame.
 * @param ucEventID The identifier of the event being logged.
 * @param ulData Event-specific payload.
 *
 * @note This function is strictly non-blocking. If the internal buffer is full,
 * the event is silently dropped to prevent blocking the kernel scheduler. It uses
 * ISR-safe critical sections to guarantee atomicity.
 */
void vTraceLog(uint32_t ulMajorFrameTimestamp, uint8_t ucEventID, uint32_t ulData);

/**
 * @brief Retrieves the oldest event from the trace buffer.
 *
 * @param pxEvent Pointer to the structure where the retrieved event will be stored.
 * @return pdTRUE if an event was successfully read, pdFALSE if the buffer is empty.
 *
 * @note This function is intended to be called by a low-priority dumper task or
 * during idle time, ensuring it does not interfere with HRT task execution.
 */
BaseType_t xTraceGetEvent(xTraceEvent_t *pxEvent);

/*-----------------------------------------------------------*/

/* TEST JITTER AND OVERHEAD */ 

/**
 * @name Kernel Overhead Tracking Variables
 * @brief Global variables to accumulate cycles spent in kernel space.
 */
extern volatile uint32_t ulKernelOverheadCycles;
extern volatile uint32_t ulKernelEntryCycles;

/**
 * @name Jitter Tracking Variables
 * @brief Global variables utilized to verify HRT release jitter constraints.
 */
extern volatile uint32_t ulExpectedHrtStartCycles;
extern volatile uint8_t ucIsSwitchingToHrt;

#endif /* TRACE_H */