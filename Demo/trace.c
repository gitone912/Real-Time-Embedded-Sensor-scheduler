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

#include "Headers/trace.h"

/**
 * @def traceBUFFER_SIZE
 * @brief Defines the maximum capacity of the circular trace buffer.
 * * Allocated to be large enough to hold trace data between dumper task executions,
 * minimizing the risk of data loss during high-frequency context switches.
 */
#define traceBUFFER_SIZE 8192

/* Static ring buffer array and indices */
static volatile xTraceEvent_t xTraceBuffer[ traceBUFFER_SIZE ];
static volatile uint32_t ulWriteIndex = 0;
static volatile uint32_t ulReadIndex = 0;

/*-----------------------------------------------------------*/

/* TEST JITTER AND OVERHEAD */

/* Global variables for exact cycle counting */
volatile uint32_t ulKernelOverheadCycles = 0U;
volatile uint32_t ulKernelEntryCycles = 0U;

/* Variables for Jitter tracking */
volatile uint32_t ulExpectedHrtStartCycles = 0U;
volatile uint8_t ucIsSwitchingToHrt = 0U;
/*-----------------------------------------------------------*/

void vTraceInit( void )
{
    ulWriteIndex = 0;
    ulReadIndex = 0;
    ulKernelOverheadCycles = 0U;
    ulKernelEntryCycles = 0U;
}
/*-----------------------------------------------------------*/

void vTraceLog( uint32_t ulMajorFrameTimestamp, uint8_t ucEventID, uint32_t ulData )
{
    /* * We must mask interrupts to prevent race conditions if vTraceLog is called
     * concurrently from an ISR and a task context. We use the "FromISR" variant
     * because this function is frequently invoked directly from within the kernel
     * scheduler (e.g., inside the PendSV or SysTick handlers).
     */
    UBaseType_t uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();

    uint32_t ulNextIndex = ( ulWriteIndex + 1 ) % traceBUFFER_SIZE;

    /* * Lock-free overrun protection: if the buffer is full, the new event is
     * simply discarded. Blocking here would violate the timing constraints
     * of the executing HRT tasks.
     */
    if ( ulNextIndex != ulReadIndex )
    {
        xTraceBuffer[ ulWriteIndex ].ulTimestamp = xTaskGetTickCountFromISR();
        xTraceBuffer[ ulWriteIndex ].ulMajorFrameTimestamp = ulMajorFrameTimestamp;
        xTraceBuffer[ ulWriteIndex ].ucEventID = ucEventID;
        xTraceBuffer[ ulWriteIndex ].ulData = ulData;

        ulWriteIndex = ulNextIndex;
    }

    portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );
}
/*-----------------------------------------------------------*/

BaseType_t xTraceGetEvent( xTraceEvent_t *pxEvent )
{
    /* * Reading must be atomic relative to tasks. taskENTER_CRITICAL is safe here
     * assuming this function is called from a task context (e.g., SRT Dumper task),
     * not from an ISR.
     */
    taskENTER_CRITICAL();

    if ( ulReadIndex == ulWriteIndex )
    {
        taskEXIT_CRITICAL();
        return pdFALSE; /* Buffer empty */
    }

    *pxEvent = xTraceBuffer[ ulReadIndex ];
    ulReadIndex = ( ulReadIndex + 1 ) % traceBUFFER_SIZE;

    taskEXIT_CRITICAL();

    return pdTRUE; /* Data retrieved successfully */
}

/*-----------------------------------------------------------*/
