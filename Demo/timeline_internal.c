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

#include "Headers/timeline_internal.h"
#include "Headers/trace.h"
#include "Headers/uart.h"

/*-----------------------------------------------------------*/
/* Internal Scheduler State Structures                       */
/*-----------------------------------------------------------*/

/**
 * @brief Encapsulates the global execution state of the Time-Triggered engine.
 */
typedef struct
{
    TimelineConfig_t *pxConfig;    /**< Pointer to the active timeline configuration. */
    BaseType_t xIsActive;          /**< Flag indicating if the timeline scheduler is active. */
    uint32_t ulCurrentTick;        /**< Current tick relative to the major frame start boundary. */
    volatile uint32_t ulIdleTicks; /**< Number of idle ticks accumulated in the current major frame. */
    uint32_t ulHrtTaskIndex;       /**< Optimization index to track the next HRT task to activate. */
} TimelineEngineState_t;

/**
 * @brief Encapsulates the runtime state of the Hard Real-Time (HRT) domain.
 */
typedef struct
{
    TaskHandle_t xCurrentTask;                      /**< Handle of the currently active HRT task. */
    volatile TimelineTaskConfig_t *pxCurrentConfig; /**< Configuration of the currently active HRT task. */
} TimelineHrtState_t;

/**
 * @brief Encapsulates the runtime state of the Soft Real-Time (SRT) domain.
 */
typedef struct
{
    TaskHandle_t xCurrentTask;     /**< Handle of the currently active or preempted SRT task. */
    List_t xReadyList;             /**< FIFO queue for pending SRT tasks. */
    uint32_t ulCompletedThisFrame; /**< Number of SRT tasks that have finished in the current major frame. */
    uint32_t ulTotalTasks;         /**< Total number of SRT tasks registered in the system. */
} TimelineSrtState_t;

/* Static Allocation of State Structures */
static TimelineEngineState_t xEngineState;
static TimelineHrtState_t xHrtState;
static TimelineSrtState_t xSrtState;

/* Private Backend Prototypes */
static void prvInitTimelineState( void );
extern BaseType_t xTaskIsSuspendedNoCritical( TaskHandle_t xTaskToCheck ); /* Corrected prototype */
static void vSortTasksByStartTime( const TimelineConfig_t *pxConfig );
static BaseType_t xTimeline_InitAndValidateTasks( TimelineConfig_t *pxCfg );
static void vResetTasks(void );
static void vResetTask( void );
static void prvInitSRTQueue( void );
static void vResetSRTQueue( void );
static TaskHandle_t xGetNextSRTTask( void );
static void vHandleSRTExecution( void );
static void vHandleSRTExecutionFromISR( void );
static void vDebugPrintSortedTasks( const TimelineConfig_t *pxConfig );

/*-----------------------------------------------------------*/
/* State Initialization Function                             */
/*-----------------------------------------------------------*/

/**
 * @brief Initializes the internal state structures of the timeline scheduler.
 *
 * Enforces a deterministic initial state before the Time-Triggered engine commences.
 *
 * @return None.
 */
static void prvInitTimelineState( void )
{
    /* Reset Engine State */
    xEngineState.pxConfig = NULL;
    xEngineState.xIsActive = pdFALSE;
    xEngineState.ulCurrentTick = 0U;
    xEngineState.ulIdleTicks = 0U;
    xEngineState.ulHrtTaskIndex = 0U;

    /* Reset HRT State */
    xHrtState.xCurrentTask = NULL;
    xHrtState.pxCurrentConfig = NULL;

    /* Reset SRT State */
    xSrtState.xCurrentTask = NULL;
    vListInitialise(&xSrtState.xReadyList);
    xSrtState.ulCompletedThisFrame = 0U;
    xSrtState.ulTotalTasks = 0U;
}

/*-----------------------------------------------------------*/
/* Backend Initialization & Bridging API                     */
/*-----------------------------------------------------------*/

/**
 * @brief 
 *
 * @param[in,out] pxConfig Pointer to the timeline configuration structure.
 */
BaseType_t xTimelineInternal_InitAndValidate( const TimelineConfig_t *pxConfig )
{
    /* Always sanitize the environment before applying a new schedule. */
    prvInitTimelineState(); 
    xEngineState.pxConfig = ( TimelineConfig_t * )pxConfig;
    
    /* Sort tasks to establish the foundational priority rule: HRT strictly precedes SRT. */
    vSortTasksByStartTime( xEngineState.pxConfig );
    vDebugPrintSortedTasks( xEngineState.pxConfig );

    /* Perform offline static analysis of the schedule. */
    return xTimeline_InitAndValidateTasks( xEngineState.pxConfig );
}

void vTimelineInternal_Activate( void )
{
    /* Arm the timeline engine. The next SysTick interrupt will evaluate tick 0. */
    xEngineState.xIsActive = pdTRUE;
    prvInitSRTQueue();
}

/*-----------------------------------------------------------*/
/* Sorting & Validation Algorithms                           */
/*-----------------------------------------------------------*/

/**
 * @brief Sorts the task configuration array based on real-time execution rules.
 *
 * This function applies an in-place Stable Insertion Sort to enforce the
 * architectural constraints of the scheduler:
 * 1. Hard Real-Time (HRT) tasks strictly precede Soft Real-Time (SRT) tasks.
 * 2. HRT tasks are sorted based on their scheduled start times.
 * 3. The relative insertion order of SRT tasks is preserved (Stable Sort) to maintain FIFO semantics.
 *
 * @param[in,out] pxConfig Pointer to the timeline configuration structure to be sorted.
 * @return None.
 */
static void vSortTasksByStartTime( const TimelineConfig_t *pxConfig )
{
    uint32_t i, j;
    TimelineTaskConfig_t xKeyTask;
    TimelineTaskConfig_t *pxTasks;
    BaseType_t xShouldSwap;

    /* Checks to prevent kernel panics on null pointers. */
    if( ( pxConfig == NULL ) || ( pxConfig ->pxTasks == NULL ) || ( pxConfig->ulNumTasks < 2 ) )
    {
        return;
    }

    pxTasks = pxConfig->pxTasks;

    /* Stable Insertion Sort, even if O(N^2) complexity, this is executed strictly offline (during initialization).
     * Stability is paramount to preserve the relative declaration order of SRT tasks,
     * effectively enforcing their configured FIFO policy. */
    for( i = 1; i < pxConfig->ulNumTasks; i++ )
    {
        xKeyTask = pxTasks[ i ];
        j = i;

        /* Evaluate elements backwards to find the correct insertion point */
        while ( j > 0 )
        {
            xShouldSwap = pdFALSE;

            /* HRT absolute precedence rule: SRT tasks must yield the upper array slots to HRT. */
            if( ( pxTasks[ j - 1 ].xType == SRT_TASK ) && ( xKeyTask.xType == HRT_TASK ) )
            {
                xShouldSwap = pdTRUE;
            }
            /* Temporal ordering rule: HRT tasks are ordered strictly chronologically. */
            else if( ( pxTasks[ j - 1 ].xType == HRT_TASK ) && ( xKeyTask.xType == HRT_TASK ) )
            {
                if( pxTasks[ j - 1 ].ulStart_time > xKeyTask.ulStart_time )
                {
                    xShouldSwap = pdTRUE;
                }
            }
            /* If both are SRT, or if HRT precedes SRT, do NOT swap to preserve stability */
            if( xShouldSwap == pdTRUE )
            {
                /* Shift the previous element one position to the right */
                pxTasks[ j ] = pxTasks[ j - 1 ];
                j--;
            }
            else
            {
                /* Correct insertion point found for the current key */
                break;
            }
        }
        pxTasks[ j ] = xKeyTask;
    }
}

/**
 * @brief Temporary debug utility to dump the sorted task array.
 *
 * Iterates through the active timeline configuration and prints the
 * sequential order, scheduling type, and start time of each task
 * via the UART interface.
 *
 * @param[in] pxConfig Pointer to the timeline configuration structure.
 * @return None.
 */
static void vDebugPrintSortedTasks( const TimelineConfig_t *pxConfig )
{
    char cBuffer[ 80 ];
    uint32_t i;
    const TimelineTaskConfig_t *pxTask;

    if( pxConfig == NULL )
    {
        return;
    }

    UART_printf( "\n--- DEBUG: Sorted Task Array ---\n" );
    for( i = 0; i < pxConfig->ulNumTasks; i++ )
    {
        pxTask = &pxConfig->pxTasks[ i ];
        snprintf( cBuffer, sizeof( cBuffer ),
                  "[ %lu ] Name: %s \tType: %s \tStart: %lu\n",
                  ( unsigned long )i,
                  pxTask->pcName,
                  ( pxTask->xType == HRT_TASK ) ? "HRT" : "SRT",
                  ( unsigned long )pxTask->ulStart_time );
        UART_printf( cBuffer );
    }
    UART_printf( "--------------------------------\n\n" );
}

/**
 * @brief Validates temporal constraints of the timeline configuration.
 *
 * Verifies that HRT tasks do not overlap, respect sub-frame boundaries,
 * and do not exceed the major frame duration.
 * 
 * @param[in,out] pxCfg Pointer to the timeline configuration structure.
 * @return pdPASS if the configuration meets temporal constraints, pdFAIL otherwise.
 */
/**
 * @brief Validates temporal constraints of the timeline configuration.
 *
 * Verifies that HRT tasks do not overlap, respect sub-frame boundaries, 
 * and do not exceed the major frame duration. Provides explicit UART 
 * logging to identify exact constraint violations.
 */
static BaseType_t xTimeline_InitAndValidateTasks( TimelineConfig_t * pxCfg )
{
    uint32_t ulIndex;
    TimelineTaskConfig_t * pxTask;
    TickType_t xSlotStart, xSlotEnd;
    uint32_t ulLastHRTEndTime = 0;
    char cBuf[128];

    if( ( pxCfg == NULL ) || ( pxCfg->pxTasks == NULL ) )
    {
        UART_printf("ERROR: Timeline Configuration pointer is NULL.\n");
        return pdFAIL;
    }

    if( pxCfg->ulSubFrameTicks == 0 || pxCfg->ulMajorFrameTicks == 0 )
    {
        UART_printf("ERROR: Invalid frame dimensions. Major or Sub-frame is 0.\n");
        return pdFAIL;
    }

    for( ulIndex = 0; ulIndex < pxCfg->ulNumTasks; ulIndex++ )
    {
        pxTask = &( pxCfg->pxTasks[ ulIndex ] );

        /* Calculate mathematical sub-frame ID  */
        pxTask->ulSubframe_id = pxTask->ulStart_time / pxCfg->ulSubFrameTicks;

        if( pxTask->xType == HRT_TASK )
        {
            /* Temporal check: A task cannot finish before it starts. */
            /* 1. Start Time >= End Time */
            if( pxTask->ulStart_time >= pxTask->ulEnd_time )
            {
                snprintf( cBuf, sizeof( cBuf ), "ERROR: Task %s has Start ( %lu ) >= End ( %lu ).\n", 
                         pxTask->pcName, 
                         ( unsigned long )pxTask->ulStart_time, 
                         ( unsigned long )pxTask->ulEnd_time );
                UART_printf( cBuf );
                return pdFAIL;
            }
            /* Major frame constraint: No task can spill into the next cyclic frame. */
            if( pxTask->ulEnd_time > pxCfg->ulMajorFrameTicks )
            {
                snprintf( cBuf, sizeof( cBuf ), "ERROR: Task %s End ( %lu ) exceeds Major Frame ( %lu ).\n", 
                         pxTask->pcName, 
                         ( unsigned long )pxTask->ulEnd_time, 
                         ( unsigned long )pxCfg->ulMajorFrameTicks );
                UART_printf( cBuf );
                return pdFAIL;
            }

            /* Sub-frame constraint validation. */
            xSlotStart = pxTask->ulSubframe_id * pxCfg->ulSubFrameTicks;
            xSlotEnd = xSlotStart + pxCfg->ulSubFrameTicks;

            /* 3. Task crosses Sub-frame boundary */
            if( pxTask->ulEnd_time > xSlotEnd )
            {
                snprintf(cBuf, sizeof(cBuf), "ERROR: Task %s End (%lu) crosses Sub-frame boundary (%lu).\n", 
                         pxTask->pcName, (unsigned long)pxTask->ulEnd_time, (unsigned long)xSlotEnd);
                UART_printf(cBuf);
                return pdFAIL;
            }
            /* Overlap Check: Since the array is chronologically sorted, comparing
             * the current start time against the previous end time guarantees
             * that HRT slots are strictly disjoint, preventing mutual exclusion violations. */
            if( pxTask->ulStart_time < ulLastHRTEndTime )
            {
                snprintf( cBuf, sizeof( cBuf ), "ERROR: Task %s overlaps with previous HRT. Start: %lu, Prev End: %lu.\n", 
                         pxTask->pcName, 
                        ( unsigned long )pxTask->ulStart_time, 
                        ( unsigned long )ulLastHRTEndTime );
                UART_printf( cBuf );
                return pdFAIL; 
            }

            ulLastHRTEndTime = pxTask->ulEnd_time;
        }
    }
    return pdPASS;
}

/*-----------------------------------------------------------*/
/* Task & Queue Management Logic                             */
/*-----------------------------------------------------------*/

/**
 * @brief Resets the system tasks and the Soft Real-Time (SRT) queue.
 *
 * Typically invoked at the boundary of a major frame to guarantee
 * deterministic cyclic repetition.
 *
 * @return None.
 */
static void vResetTasks( void )
{
    /* A Major Frame boundary requires a complete state flush.
     * Failure to reset accurately introduces jitter into the next frame. */
    vResetSRTQueue();
    vResetTask();
}

/**
 * @brief Reinitializes all configured tasks to their pristine initial states.
 *
 * @return None.
 */
static void vResetTask( void )
{
    uint32_t ulIndex;
    TimelineTaskConfig_t *pxTask;

    for ( ulIndex = 0; ulIndex < xEngineState.pxConfig->ulNumTasks; ulIndex++ )
    {
        pxTask = &( xEngineState.pxConfig->pxTasks[ ulIndex ] );
        if ( pxTask->xTaskHandle != NULL )
        {
            /* Purge the execution stack and reset the Program Counter (PC).
             * This ensures the task operates as if the system was just booted,
             * guaranteeing true cyclic determinism. */
            /*vTaskTimelineReset( pxTask->xTaskHandle, pxTask->pvTaskFunction, ( void * )pxTask->pvTaskParams, pxTask->xStackDepth );*/
        }
    }
}

/**
 * @brief Initializes the Soft Real-Time (SRT) Ready List and populates it.
 *
 * Scans the active configuration for SRT tasks and inserts them into the
 * SRT FIFO queue, preserving their stable order.
 *
 * @return None.
 */
static void prvInitSRTQueue( void )
{
    uint32_t ulIndex;
    TimelineTaskConfig_t *pxTaskCfg;

    vListInitialise( &xSrtState.xReadyList );

    if ( xEngineState.pxConfig == NULL )
    {
        return;
    }

    /* Iterate over the configuration. Because the array was subjected to a
     * Stable Sort, appending elements sequentially to the List effectively
     * hardcodes the configured FIFO policy into the dynamic data structure. */
    for ( ulIndex = 0; ulIndex < xEngineState.pxConfig->ulNumTasks; ulIndex++ )
    {
        pxTaskCfg = &( xEngineState.pxConfig->pxTasks[ ulIndex ] );

        if ( pxTaskCfg->xType == SRT_TASK )
        {
            vListInitialiseItem( &( pxTaskCfg->xSRTListItem ) );
            listSET_LIST_ITEM_OWNER( &( pxTaskCfg->xSRTListItem ), ( void * )pxTaskCfg );
            vListInsertEnd( &xSrtState.xReadyList, &( pxTaskCfg->xSRTListItem ) );
            xSrtState.ulTotalTasks++;
        }
    }
}

/**
 * @brief Retrieves the next available Soft Real-Time (SRT) task from the FIFO queue.
 *
 * @return TaskHandle_t The handle of the next SRT task, or NULL if the queue is empty.
 */
static TaskHandle_t xGetNextSRTTask( void )
{
    TimelineTaskConfig_t *pxNextTask = NULL;

    /* Extract the next task based strictly on insertion order.
     * No priority evaluation occurs here, maintaining pure FIFO semantics. */
    if ( listLIST_IS_EMPTY( &xSrtState.xReadyList ) == pdFALSE )
    {
        listGET_OWNER_OF_NEXT_ENTRY( pxNextTask, &xSrtState.xReadyList );
        if ( pxNextTask != NULL )
        {
            return pxNextTask->xTaskHandle;
        }
    }
    return NULL;
}

/**
 * @brief Resets the Soft Real-Time (SRT) execution queue safely.
 *
 * Suspends any currently running SRT task and resets the queue index
 * back to the beginning to start a fresh cycle.
 *
 * @return None.
 */
static void vResetSRTQueue( void )
{
    UBaseType_t uxSavedInterruptStatus;

    /* Critical section from ISR. A major frame reset
     * occurs inside the Tick Hook (SysTick context). We must prevent lower
     * priority interrupts from mangling the list pointers mid-reset. */
    uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    {
        if ( xSrtState.xCurrentTask != NULL )
        {
            /* Any SRT task still running at frame end loses its time budget and is terminated. */
            vTraceLog( xEngineState.ulCurrentTick, eTraceSrtTerminated, ( uint32_t )xSrtState.xCurrentTask );
            vTaskTimelineSuspendFromISR( xSrtState.xCurrentTask );
            xSrtState.xCurrentTask = NULL;
        }

        if ( listLIST_IS_EMPTY( &xSrtState.xReadyList ) == pdFALSE )
        {
            /* Rewind the internal list traversal pointer to the anchor element.
             * This avoids the overhead of destroying and rebuilding the list
             * every major frame. */
            xSrtState.xReadyList.pxIndex = ( ListItem_t * )&( xSrtState.xReadyList.xListEnd );
        }
        xSrtState.ulCompletedThisFrame = 0U;
    }
    taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
}

/**
 * @brief Evaluates and triggers Soft Real-Time (SRT) task execution.
 *
 * Ensures that SRT tasks strictly execute only when no Hard Real-Time (HRT)
 * tasks are active (slack time reclaiming). Evaluates from standard context.
 *
 * @return None.
 */
static void vHandleSRTExecution( void )
{
    TaskHandle_t xTaskToResume = NULL;
    BaseType_t xIsNewTask = pdFALSE;

    /* A standard critical section disables interrupts up to configMAX_SYSCALL_INTERRUPT_PRIORITY.
     * This protects the SRT state variables from the Tick Hook evaluating them simultaneously. */
    taskENTER_CRITICAL();
    {
        if ( xEngineState.xIsActive == pdFALSE )
        {
            taskEXIT_CRITICAL();
            return;
        }

        /* Strict Architecture Rule: SRT tasks only execute during "Slack Time".
         * If an HRT task is active and not explicitly suspended, SRT execution is strictly forbidden. */
        if ( ( xHrtState.xCurrentTask != NULL ) && ( xTaskIsSuspended( xHrtState.xCurrentTask ) == pdFALSE ) )
        {
            taskEXIT_CRITICAL();
            return;
        }

        /* If an SRT task was previously preempted by an HRT task, it maintains
         * priority over new SRT tasks in the FIFO queue and must be resumed first. */
        if( xSrtState.xCurrentTask != NULL )
        {
            if( xTaskIsSuspended( xSrtState.xCurrentTask ) == pdTRUE )
            {
                xTaskToResume = xSrtState.xCurrentTask;
            }
        }
        
        /* Otherwise, fetch the next task in the queue if unfulfilled demands exist. */
        else if( xSrtState.ulCompletedThisFrame < xSrtState.ulTotalTasks )
        {
            TaskHandle_t xNextSRT = xGetNextSRTTask();
            if( xNextSRT != NULL )
            {
                xSrtState.xCurrentTask = xNextSRT;
                xTaskToResume = xNextSRT;
                xIsNewTask = pdTRUE;
            }
        }
    }
    taskEXIT_CRITICAL();

    /* Invoking resumption outside the critical section reduces interrupt latency. */
    if( xTaskToResume != NULL )
    {
        if( xIsNewTask == pdTRUE )
        {
            vTraceLog( xEngineState.ulCurrentTick, eTraceSrtStart, ( uint32_t )xTaskToResume );
        }
        else
        {
            vTraceLog( xEngineState.ulCurrentTick, eTraceSrtResume, ( uint32_t )xTaskToResume );
        }
        vTaskTimelineResume( xTaskToResume );
    }
}

/**
 * @brief Evaluates and triggers Soft Real-Time (SRT) task execution from an ISR.
 *
 * Ensures that SRT tasks strictly execute only when no Hard Real-Time (HRT)
 * tasks are active. Safe to be called from within interrupt handlers.
 *
 * @return None.
 */
static void vHandleSRTExecutionFromISR( void )
{
    TaskHandle_t xTaskToResume = NULL;
    BaseType_t xIsNewTask = pdFALSE;
    UBaseType_t uxSavedInterruptStatus;

    /* ISR safe variant. Maintains identical slack reclaiming logic but utilizes
     * ISR-specific port macros to save and restore interrupt masks safely during nested interrupts. */
    uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    {
        if( xEngineState.xIsActive == pdFALSE )
        {
            taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
            return;
        }

        /* HRT strict preemption check */
        if( ( xHrtState.xCurrentTask != NULL ) && ( xTaskIsSuspendedFromISR( xHrtState.xCurrentTask ) == pdFALSE ) )
        {
            taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
            return;
        }

        if( xSrtState.xCurrentTask != NULL )
        {
            if( xTaskIsSuspendedFromISR( xSrtState.xCurrentTask ) == pdTRUE )
            {
                xTaskToResume = xSrtState.xCurrentTask;
            }
        }
        else if( xSrtState.ulCompletedThisFrame < xSrtState.ulTotalTasks )
        {
            TaskHandle_t xNextSRT = xGetNextSRTTask();
            if( xNextSRT != NULL )
            {
                xSrtState.xCurrentTask = xNextSRT;
                xTaskToResume = xNextSRT;
                xIsNewTask = pdTRUE;
            }
        }
    }
    taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );

    /* Perform actual resumption and logging outside the critical section */
    if( xTaskToResume != NULL )
    {
        if( xIsNewTask == pdTRUE )
        {
            vTraceLog( xEngineState.ulCurrentTick, eTraceSrtStart, ( uint32_t )xTaskToResume );
        }
        else
        {
            vTraceLog( xEngineState.ulCurrentTick, eTraceSrtResume, ( uint32_t )xTaskToResume );
        }
        vTaskTimelineResumeFromISR( xTaskToResume );
    }
}

/*-----------------------------------------------------------*/
/* Completion & System Calls                                 */
/*-----------------------------------------------------------*/

void vTimelineMarkHRTComplete( void )
{
    TaskHandle_t xCompletedTask = NULL;

    /*Critical section ensures that state transition is fully atomic.
    * We cannot allow the Tick Hook to preempt the system while the HRT
    * state pointers are being nulled out.*/
    taskENTER_CRITICAL();
    {
        if( xHrtState.pxCurrentConfig != NULL )
        {
            xCompletedTask = xHrtState.xCurrentTask;
            xHrtState.pxCurrentConfig = NULL;
            xHrtState.xCurrentTask = NULL;
        }
        if( xCompletedTask != NULL )
            {
                vTraceLog( xEngineState.ulCurrentTick, eTraceHrtComplete, ( uint32_t )xCompletedTask );
                /* Suspending the HRT task artificially creates slack time before the
                 * assigned sub-frame has formally ended. */
                vTaskTimelineSuspend( xCompletedTask );
            }

            /* Immediately attempt to reclaim the newly generated slack time
             * by evaluating the SRT FIFO queue before exiting the critical block. */
            vHandleSRTExecution();
        }
    taskEXIT_CRITICAL();
    
     /* Requesting a context switch (taskYIELD) outside the critical block guarantees
     * the CPU will immediately honor the PendSV exception upon re-enabling interrupts,
     * launching the newly assigned SRT task without waiting for the next SysTick. */
    if ( xCompletedTask != NULL )
    {
        taskYIELD();
    }
}

void vTimelineMarkSRTTaskCompleted( void )
{
    TaskHandle_t xCompletedTask = NULL;

    taskENTER_CRITICAL();
    {
        xCompletedTask = xSrtState.xCurrentTask;
        xSrtState.xCurrentTask = NULL;
        xSrtState.ulCompletedThisFrame++;

        if( xCompletedTask != NULL )
        {
            vTraceLog( xEngineState.ulCurrentTick, eTraceSrtComplete, ( uint32_t )xCompletedTask );
           
            /* * Suspension MUST occur atomically with the state variables update
             * to prevent the SysTick from preempting a semi-terminated task. */
            vTaskTimelineSuspend( xCompletedTask );
        }

        /* The SRT task is finished; immediately evaluate the queue to pull
         * the next FIFO task and maintain maximum CPU utilization. */
        vHandleSRTExecution();
    }
    taskEXIT_CRITICAL();

    /* Yield the processor outside the critical section to enforce immediate execution. */
    if ( xCompletedTask != NULL )
    {
        taskYIELD();
    }
}

/*-----------------------------------------------------------*/
/* Scheduling Hooks                                          */
/*-----------------------------------------------------------*/

BaseType_t xTimelineTickHook( void )
{
    BaseType_t xSwitchNeeded = pdFALSE;
    uint32_t ulIndex;
    TimelineTaskConfig_t *pxTask;
    static BaseType_t xIsFirstBootTick = pdTRUE;

    if ( xEngineState.pxConfig == NULL )
    {
        return pdFALSE;
    }

    /* Capture the hardware cycle counter to measure the scheduler's overhead. */
    uint32_t ulTickHookStartCycles = ( *( ( volatile uint32_t * )0xE000E018 ) );

    /* Phase 1: Deadline Enforcement for HRT Tasks.
     * Hard real-time constraints dictate that exceeding the allocated time budget
     * is a critical failure.
     * Checking the end time O(1). */
    if ( xHrtState.pxCurrentConfig != NULL )
    {
        if ( xEngineState.ulCurrentTick == xHrtState.pxCurrentConfig->ulEnd_time )
        {
            vTaskTimelineSuspendFromISR( xHrtState.xCurrentTask );
            vTraceLog( xEngineState.ulCurrentTick, eTraceDeadlineMiss, ( uint32_t )xHrtState.xCurrentTask );
            xHrtState.pxCurrentConfig = NULL;
            xHrtState.xCurrentTask = NULL;
            xSwitchNeeded = pdTRUE;
        }
    }
    /* idle time metrics for performance profiling. */
    if ( xIsFirstBootTick == pdTRUE )
    {
        xIsFirstBootTick = pdFALSE;
    }
    else if ( xTaskGetCurrentTaskHandle() == xTaskGetIdleTaskHandle() )
    {
        xEngineState.ulIdleTicks++;
    }

    /* Phase 2: HRT Activation Evaluation.
     * Iterates from the last known index to locate tasks slated to start exactly now. */
    for ( ulIndex = xEngineState.ulHrtTaskIndex; ulIndex < xEngineState.pxConfig->ulNumTasks; ulIndex++ )
    {
        pxTask = &( xEngineState.pxConfig->pxTasks[ ulIndex ] );

        if ( pxTask->xType != HRT_TASK )
        {
            continue;
        }

        if ( xEngineState.ulCurrentTick == pxTask->ulStart_time )
        {
            /* Strict Preemption: If an SRT task is currently running in slack time,
             * it is preempted the moment an HRT start boundary is reached. */
            if ( xSrtState.xCurrentTask != NULL )
            {
                vTaskTimelineSuspendFromISR( xSrtState.xCurrentTask );
                vTraceLog( xEngineState.ulCurrentTick, eTraceSrtPreempt, (uint32_t)xSrtState.xCurrentTask);
            }

            xHrtState.xCurrentTask = pxTask->xTaskHandle;
            xHrtState.pxCurrentConfig = pxTask;

            /* Force the HRT task into a ready state. The xTimelineScheduleHook will ensure
             * it bypasses standard FreeRTOS priorities. */
            vTaskTimelineResumeFromISR( xHrtState.xCurrentTask );
            vTraceLog( xEngineState.ulCurrentTick, eTraceHrtStart, (uint32_t)xHrtState.xCurrentTask);

            xEngineState.ulHrtTaskIndex = ulIndex;
            ulExpectedHrtStartCycles = ( *( ( volatile uint32_t * )0xE000E018 ) );
            ucIsSwitchingToHrt = 1U;
            xSwitchNeeded = pdTRUE;

            break;
        }
    }

    /* Phase 3: SRT Execution / Slack Reclaiming.
     * If no HRT task was triggered or the active HRT task has completed early,
     * the system grants execution time to the SRT queue. */
    if ( ( xHrtState.xCurrentTask == NULL ) || ( xTaskIsSuspendedFromISR( xHrtState.xCurrentTask ) == pdTRUE ) )
    {
        vHandleSRTExecutionFromISR();
        if ( xSrtState.xCurrentTask != NULL )
        {
            xSwitchNeeded = pdTRUE;
        }
    }

    /* Phase 4: Cyclic Major Frame Boundary Handling. */
    BaseType_t xIsEndOfFrame = ( xEngineState.ulCurrentTick + 1 >= xEngineState.pxConfig->ulMajorFrameTicks );

    if ( xIsEndOfFrame == pdTRUE )
    {
        /* An HRT task still active at frame termination inherently violated its boundary. */
        if ( xHrtState.pxCurrentConfig != NULL )
        {
            vTraceLog( xEngineState.ulCurrentTick, eTraceDeadlineMiss, ( uint32_t )xHrtState.xCurrentTask );
            xHrtState.pxCurrentConfig = NULL;
            xHrtState.xCurrentTask = NULL;
            xSwitchNeeded = pdTRUE;
        }

        /* Purge all state and prepare for an exact deterministic repetition of the timeline. */
        vResetTasks();
    }

    /* Calculate the total CPU cycles consumed by the scheduler tick itself. */
    uint32_t ulTickHookEndCycles = ( *( ( volatile uint32_t * )0xE000E018 ) );
    uint32_t ulDelta;

    if ( ulTickHookEndCycles <= ulTickHookStartCycles )
    {
        ulDelta = ulTickHookStartCycles - ulTickHookEndCycles;
    }
    else
    {
        /* Calculate the ammount of ticks spent into Cpu mode. */
        ulDelta = ulTickHookStartCycles + ( ( configCPU_CLOCK_HZ / configTICK_RATE_HZ - 1U ) - ulTickHookEndCycles ) + 1U;
    }

    ulKernelOverheadCycles += ulDelta;

    if ( xIsEndOfFrame == pdTRUE )
    {
        /* Dump statistics strictly once per major frame */
        vTraceLog( xEngineState.ulCurrentTick, eTraceIdleStats, xEngineState.ulIdleTicks );
        vTraceLog( xEngineState.ulCurrentTick, eTraceOverheadStats, ulKernelOverheadCycles );

        /* Reset telemetry and time counters for the new cycle. */
        ulKernelOverheadCycles = 0U;
        xEngineState.ulCurrentTick = 0U;
        xEngineState.ulIdleTicks = 0U;
        xEngineState.ulHrtTaskIndex = 0U;

        vTraceLog( 0, eTraceMajorFrame, 0 );
        xSwitchNeeded = pdTRUE;
    }
    else
    {
        xEngineState.ulCurrentTick++;
    }

    return xSwitchNeeded;
}

TaskHandle_t xTimelineScheduleHook( void )
{
    /* The fundamental paradigm shift of this scheduler: dynamic priority inversion
     * is bypassed. The system forces execution based strictly on timeline authority.
     * HRT maintains absolute dominion over the CPU. */
    if ( ( xHrtState.xCurrentTask != NULL ) && ( xTaskIsSuspendedFromISR( xHrtState.xCurrentTask ) == pdFALSE ) )
    {
        return xHrtState.xCurrentTask;
    }

    /* Only in the absence of HRT authority can SRT tasks execute. */
    if ( ( xSrtState.xCurrentTask != NULL ) && ( xTaskIsSuspendedFromISR( xSrtState.xCurrentTask ) == pdFALSE ) )
    {
        return xSrtState.xCurrentTask;
    }

    /* If no timeline tasks are scheduled or active, yield to the native idle loop. */
    return NULL;
}

/*-----------------------------------------------------------*/
/* Utilities & Accessors                                     */
/*-----------------------------------------------------------*/

void vTimelineUpdateJitterStats( uint32_t ulJitterCycles )
{
    /* Performance metric calculation. This tracks minimum and maximum delays
     * between the theoretic release boundary and the actual PC branch execution. */
    if ( xHrtState.pxCurrentConfig != NULL )
    {
        if ( xHrtState.pxCurrentConfig->ulMaxDelay < ulJitterCycles )
        {
            /* Explicit cast required to bypass const/volatile qualifiers during telemetry updates. */
            ( ( TimelineTaskConfig_t * )xHrtState.pxCurrentConfig )->ulMaxDelay = ulJitterCycles;
        }
        if ( ( ulJitterCycles < xHrtState.pxCurrentConfig->ulMinDelay ) || ( xHrtState.pxCurrentConfig->ulMinDelay == 0U ) )
        {
            ( ( TimelineTaskConfig_t * )xHrtState.pxCurrentConfig )->ulMinDelay = ulJitterCycles;
        }
    }
}

/**
 * @brief Retrieves the static configuration of a task given its runtime handle.
 * * Used by external modules (like workloads or trace dumpers) to map a FreeRTOS
 * TaskHandle_t back to our Time-Triggered deterministic parameters.
 * * @param[in] pxCfg Pointer to the active timeline configuration.
 * @param[in] xHandle The FreeRTOS task handle to search for.
 * @return Constant pointer to the task's configuration, or NULL if not found.
 */
const TimelineTaskConfig_t *pxTimeline_FindTaskByHandle( const TimelineConfig_t *pxCfg, TaskHandle_t xHandle )
{
    uint32_t ulIndex;
    const TimelineTaskConfig_t *pxTask;

    if ( ( pxCfg == NULL ) || ( xHandle == NULL ) )
    {
        return NULL;
    }

    for ( ulIndex = 0; ulIndex < pxCfg->ulNumTasks; ulIndex++ )
    {
        pxTask = &( pxCfg->pxTasks[ ulIndex ] );
        if ( pxTask->xTaskHandle == xHandle )
        {
            return pxTask;
        }
    }
    return NULL;
}
/*-----------------------------------------------------------*/
