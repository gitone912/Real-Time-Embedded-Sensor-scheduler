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
#ifndef TIMELINE_INTERNAL_H
#define TIMELINE_INTERNAL_H

#include "timeline.h"
#include <stdio.h> 

/*-----------------------------------------------------------*/
/* Scheduling Hooks & Architecture API                       */
/*-----------------------------------------------------------*/

/**
 * @brief Evaluates the timeline state at every hardware tick to enforce deterministic boundaries.
 *
 * This hook is invoked within the FreeRTOS SysTick interrupt context. It evaluates
 * the current global tick against the predefined Major Frame and Sub-Frame architecture.
 * It identifies deadline misses for Hard Real-Time (HRT) tasks and dictates when a
 * forced context switch is mandatory to preserve strict timeline repeatability.
 *
 * @return pdTRUE if a preemption or context switch is required to maintain the timeline;
 * pdFALSE if the current execution state remains valid.
 */
BaseType_t xTimelineTickHook( void );

/**
 * @brief Resolves the next task to execute.
 *
 * Bypassing the standard FreeRTOS priority-based scheduler, this hook enforces the
 * Time-Triggered paradigm. It strictly guarantees that HRT tasks execute non-preemptively
 * in their assigned slots. During unallocated time (idle time), it resolves pending
 * Soft Real-Time (SRT) tasks using a FIFO policy.
 *
 * @return TaskHandle_t The handle of the task strictly authorized to run in the current tick,
 * or NULL to yield to the system Idle task.
 */
TaskHandle_t xTimelineScheduleHook( void );

/*-----------------------------------------------------------*/
/* Task State Management (Bridge to standard tasks.c)        */
/*-----------------------------------------------------------*/
/**
 * @brief Suspends a task, removing it from the standard FreeRTOS ready lists.
 *
 * Enforces the timeline boundaries by halting a task's execution outside its
 * designated sub-frame or upon explicit completion of its payload.
 *
 * @param[in] xTask The Task Handle of the task to suspend.
 */
void vTaskTimelineSuspend( TaskHandle_t xTask );

/**
 * @brief Suspends a task from an Interrupt Service Routine (ISR) context.
 *
 * Safe-from-ISR variant to halt a task. Primarily used within the tick hook
 * to enforce Hard Real-Time (HRT) deadlines (KILL policy) or handle preemptions.
 *
 * @param[in] xTaskToSuspend The Task Handle of the task to suspend.
 */
void vTaskTimelineSuspendFromISR( TaskHandle_t xTaskToSuspend );

/**
 * @brief Resumes a task, forcing it into an executable state.
 *
 * Bypasses standard priority evaluation to explicitly inject a task into the
 * active execution context at its strictly scheduled start time.
 *
 * @param[in] xTask The Task Handle of the task to resume.
 */
void vTaskTimelineResume( TaskHandle_t xTask );

/**
 * @brief Resumes a task from an Interrupt Service Routine (ISR) context.
 *
 * Safe-from-ISR variant to activate a task. Used by the tick hook when an
 * HRT task's start time boundary is reached.
 *
 * @param[in] xTaskToResume The Task Handle of the task to resume.
 */
void vTaskTimelineResumeFromISR( TaskHandle_t xTaskToResume );

/**
 * @brief Resets a task's context, stack and parameters to its pristine, initial state.
 *
 * Crucial for the Time-Triggered architecture. At the end of each major frame,
 * all tasks must be logically recreated or reset to guarantee fully deterministic
 * repetition across cyclic frame boundaries.
 *
 * @param[in] xTask The Task Handle of the task to reset.
 * @param[in] pxCode TaskFunction_t (pointer) to the task's entry function.
 * @param[in] pvParameters Parameters to pass to the task function.
 * @param[in] uxStackDepth The stack depth allocated for the task.
 */
void vTaskTimelineReset( TaskHandle_t xTask, TaskFunction_t pxCode, void * pvParameters, configSTACK_DEPTH_TYPE uxStackDepth );

/**
 * @brief Checks if a task is currently in the Suspended state.
 * @param[in] xTaskToCheck The Task Handle of the task to evaluate.
 * @return pdTRUE if suspended, pdFALSE otherwise.
 */
BaseType_t xTaskIsSuspended( TaskHandle_t xTaskToCheck );

/**
 * @brief Checks if a task is currently in the Suspended state (ISR safe).
 * @param[in] xTaskToCheck The Task Handle of the task to evaluate.
 * @return pdTRUE if suspended, pdFALSE otherwise.
 */
BaseType_t xTaskIsSuspendedFromISR( TaskHandle_t xTaskToCheck );

/*-----------------------------------------------------------*/
/* Synchronization & Completion System Calls                 */
/*-----------------------------------------------------------*/

/**
 * @brief Signals the successful execution and completion of an HRT task.
 *
 * Must be invoked by an HRT task immediately upon finishing its workload for the
 * current cycle. This explicit signaling allows the kernel to safely transition
 * the HRT task to a suspended state and immediately reclaim the remaining sub-frame
 * time (slack time) for SRT task execution, thereby maximizing CPU utilization
 * without violating timing guarantees.
 *
 * @return None. (Yields the CPU immediately).
 */
void vTimelineMarkHRTComplete( void );

/**
 * @brief Signals the successful execution and completion of an SRT task's payload.
 *
 * Invoked by an SRT task upon completing its work. This signals the scheduler to
 * increment the SRT completion metrics for the current Major Frame, suspend the
 * calling task, and immediately yield the CPU to the next pending SRT task in the
 * FIFO queue.
 *
 * @return None. (Yields the CPU immediately).
 */
void vTimelineMarkSRTTaskCompleted( void );

/**
 * @brief Activates the timeline scheduling engine.
 *
 * Transitions the system state to active, initializing the Soft Real-Time (SRT)
 * FIFO queues and enabling the timeline tick evaluation logic.
 */
void vTimelineInternal_Activate( void );

/**
 * @brief Initializes and validates the timeline configuration constraints.
 *
 * Performs static analysis on the provided configuration. It sorts tasks to enforce
 * HRT priority over SRT, validates sub-frame boundaries, checks for HRT temporal
 * overlaps, and guarantees that no task exceeds the major frame duration.
 *
 * @param[in] pxConfig Constant pointer to the user-defined timeline configuration.
 * @return pdPASS if the configuration is architecturally sound; pdFAIL if constraints are violated.
 */
BaseType_t xTimelineInternal_InitAndValidate( const TimelineConfig_t * pxConfig );

/*-----------------------------------------------------------*/
/* Kernel Internal Utilities                                 */
/*-----------------------------------------------------------*/

/**
 * @brief Maps a standard FreeRTOS Task Handle back to its static Timeline configuration.
 *
 * Necessary for external diagnostic modules to resolve
 * Time-Triggered metadata from a raw OS handle.
 *
 * @param[in] pxCfg Pointer to the active timeline configuration.
 * @param[in] xHandle The FreeRTOS task handle to search for.
 * @return const TimelineTaskConfig_t* Pointer to the static configuration, or NULL if not found.
 */
const TimelineTaskConfig_t * pxTimeline_FindTaskByHandle( const TimelineConfig_t * pxCfg, TaskHandle_t xHandle );

/**
 * @brief Updates the maximum and minimum observed release delay for the active HRT task.
 *
 * Used for strict performance monitoring and to update delay's values 
 * used to compute jitter later on. 
 *
 * @param[in] ulJitterCycles The calculated release delay in CPU cycles.
 */
void vTimelineUpdateJitterStats( uint32_t ulJitterCycles );

/*-----------------------------------------------------------*/
/* Inline Accessors for Kernel optimization                  */
/*-----------------------------------------------------------*/

/**
 * @brief Retrieves the configured end time (deadline) for a task.
 *
 * @param[in] pxTask Pointer to the static configuration of the task.
 * @return uint32_t The deadline tick, or 0U if the task is invalid.
 */
static inline uint32_t ulTimelineTask_GetEnd( const TimelineTaskConfig_t * pxTask ) 
{ 
    return ( pxTask != NULL ) ? pxTask->ulEnd_time : 0U; 
}

/**
 * @brief Retrieves the minimum recorded release delay for the specified task.
 *
 * @param[in] pxTask Pointer to the static configuration of the task.
 * @return uint32_t The minimum activation delay measured in CPU cycles.
 */
static inline uint32_t ulTimelineTask_GetDelayMin( const TimelineTaskConfig_t * pxTask )
{
    return ( pxTask != NULL ) ? pxTask->ulMinDelay : 0U;
}

/**
 * @brief Retrieves the maximum recorded release delay for the specified task.
 *
 * @param[in] pxTask Pointer to the static configuration of the task.
 * @return uint32_t The maximum activation delay measured in CPU cycles.
 */
static inline uint32_t ulTimelineTask_GetDelayMax( const TimelineTaskConfig_t * pxTask )
{
    return ( pxTask != NULL ) ? pxTask->ulMaxDelay : 0U;
}

#endif /* TIMELINE_INTERNAL_H */
