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

#include <stdio.h>
#include "Headers/trace_dumper.h"
#include "FreeRTOSConfig.h"
#include "Headers/trace.h"
#include "Headers/uart.h"
#include "Headers/timeline.h"
#include "Headers/timeline_internal.h"

/**
 * @brief Tracks the total number of Major Frames executed.
 * @note Marked as volatile to prevent compiler optimization, allowing it to be
 * reliably inspected via hardware debuggers.
 */
volatile uint32_t ulMajorFrameCount = 0;

/* =========================================================
 * Hardware Constants for Overhead Calculation
 * ========================================================= */
#define traceDUMPER_CYCLES_PER_TICK ( configCPU_CLOCK_HZ / configTICK_RATE_HZ )

/*-----------------------------------------------------------*/

void vTraceDumperTask( void *pvParameters )
{
    ( void )pvParameters;
    char cBuffer[ 128 ];
    xTraceEvent_t xEvent;

    /* Variables for pre-calculated context data. */
    TaskHandle_t xTaskHandle;
    const char *pcTaskName;
    const TimelineTaskConfig_t *pxTaskConfig;
    uint32_t ulSubFrameIndex;
    uint32_t ulSubFrameDuration;

    /* Retrieve global timeline configuration */
    const TimelineConfig_t *pxTimeline = pxGetTimelineConfig();

    UART_printf( "DUMPER: Scheduler Started & Dumper Alive!\n" );

    for ( ;; )
    {
        /* Process all available events in the buffer non-destructively. */
        while ( xTraceGetEvent( &xEvent ) == pdTRUE )
        {
            /* Extract common payload. */
            xTaskHandle = ( TaskHandle_t )xEvent.ulData;

            /* Resolve Task Name safely to avoid dereferencing NULL. */
            if ( xTaskHandle != NULL )
            {
                pcTaskName = pcTaskGetName( xTaskHandle );
            }
            else
            {
                pcTaskName = "N/A";
            }

            /* Resolve Timeline Configuration for this task. */
            pxTaskConfig = pxTimeline_FindTaskByHandle( pxTimeline, xTaskHandle );

            /* Calculate Timing Metadata based on the Major Frame configuration. */
            ulSubFrameDuration = ( pxTimeline != NULL ) ? pxTimeline->ulSubFrameTicks : 10;

            if ( ulSubFrameDuration > 0 )
            {
                ulSubFrameIndex = xEvent.ulMajorFrameTimestamp / ulSubFrameDuration;
            }
            else
            {
                ulSubFrameIndex = 0;
            }

            switch ( xEvent.ucEventID )
            {
            case eTraceHrtStart:
                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] HRT START:\t%s ( SubFrame: %lu )\tDeadline @%05lu\t delay max:%lu cycles \t delay min: %lu cycles\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         pcTaskName,
                         ( unsigned long )ulSubFrameIndex,
                         ( unsigned long )ulTimelineTask_GetEnd( pxTaskConfig ),
                         ( unsigned long )ulTimelineTask_GetDelayMax( pxTaskConfig ),
                         ( unsigned long )ulTimelineTask_GetDelayMin( pxTaskConfig ) );
                break;

            case eTraceHrtComplete:
                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] HRT COMPLETE:\t%s ( SubFrame: %lu )\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         pcTaskName,
                         ( unsigned long )ulSubFrameIndex );
                break;

            case eTraceDeadlineMiss:
                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] DEADLINE MISS!\t%s\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         pcTaskName );
                break;

            case eTraceSrtStart:
                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] SRT START:\t%s ( SubFrame: %lu )\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         pcTaskName,
                         ( unsigned long )ulSubFrameIndex );
                break;

            case eTraceSrtPreempt:
                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] SRT PREEMPT:\t%s ( SubFrame: %lu )\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         pcTaskName,
                         ( unsigned long )ulSubFrameIndex );
                break;

            case eTraceSrtResume:
                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] SRT RESUME:\t%s ( SubFrame: %lu )\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         pcTaskName,
                         ( unsigned long )ulSubFrameIndex );
                break;

            case eTraceSrtComplete:
                snprintf(cBuffer, sizeof(cBuffer),
                         "[ %05lu\t%05lu ] SRT COMPLETE:\t%s ( SubFrame: %lu )\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         pcTaskName,
                         ( unsigned long )ulSubFrameIndex );
                break;

            case eTraceSrtTerminated:
                /**
                 * @brief Format and output the SRT termination event.
                 * Provides visibility into Soft Real-Time tasks that failed to
                 * complete execution before the Major Frame boundary and were reset.
                 */
                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] SRT KILLED:\t%s ( SubFrame: %lu )\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         pcTaskName,
                         ( unsigned long )ulSubFrameIndex );
                break;

            case eTraceMajorFrame:
                ulMajorFrameCount++;
                snprintf( cBuffer, sizeof( cBuffer ),
                         "\n[ %05lu\t%05lu ] MAJOR FRAME\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp );
                break;

            case eTraceIdleStats:
                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] CPU STATS: Idle Ticks = %lu\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         xEvent.ulData );
                break;

            case eTraceOverheadStats:
            {
                /* 1. Retrieve cycles spent by the Kernel (Event Payload) */
                uint32_t ulOverheadCycles = xEvent.ulData;

                /* 2. Calculate total available cycles in the entire Major Frame. */
                uint32_t ulTotalMajorFrameCycles = pxTimeline->ulMajorFrameTicks * traceDUMPER_CYCLES_PER_TICK;

                /* 3. Calculate percentage using 64-bit integer math to prevent overflow.
                 * Multiplying by 10000 provides 2 decimal places (e.g., 1234 -> 12.34%) */
                uint32_t ulScaledPercentage = ( uint32_t )( ( ( uint64_t )ulOverheadCycles * 10000ULL ) / ulTotalMajorFrameCycles );

                /* 4. Separate integer and fractional parts for formatting */
                uint32_t ulIntPart = ulScaledPercentage / 100;
                uint32_t ulFracPart = ulScaledPercentage % 100;

                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] KERNEL OVERHEAD: %lu Cycles (%lu.%02lu%%)\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         ( unsigned long )ulOverheadCycles,
                         ( unsigned long )ulIntPart,
                         ( unsigned long )ulFracPart );
                break;
            }

            case eTraceJitterViolation:
            {
                uint32_t ulJitterCycles = xEvent.ulData;

                /* Convert cycles to microseconds for readability.
                 * (ulJitterCycles * 1000000) / CPU_CLOCK_HZ = microseconds */
                uint32_t ulJitterUs = (ulJitterCycles * 1000000U) / configCPU_CLOCK_HZ;

                snprintf( cBuffer, sizeof( cBuffer ),
                         "[ %05lu\t%05lu ] JITTER VIOLATION: %lu Cycles (~%lu us)\n",
                         ( unsigned long )xEvent.ulMajorFrameTimestamp,
                         ( unsigned long )xEvent.ulTimestamp,
                         ( unsigned long )ulJitterCycles,
                         ( unsigned long )ulJitterUs );
                break;
            }

            default:
                cBuffer[ 0 ] = '\0';
                break;
            }

            if (cBuffer[ 0 ] != '\0')
            {
                UART_printf( cBuffer );
            }
        }

        /* Yield to other tasks. Polling delay prevents starvation. */
        vTaskDelay( pdMS_TO_TICKS( 1 ) );
    }
}

/*-----------------------------------------------------------*/
