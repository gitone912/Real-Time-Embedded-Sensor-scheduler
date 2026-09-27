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

#include "Headers/timeline.h"
#include "Headers/timeline_internal.h"
#include "Headers/tasks_generated.h"
#include "Headers/uart.h"
#include "Headers/trace.h"
#include "Headers/trace_dumper.h"

/* --- Trace Dumper Static Allocation --- */
#define DUMPER_STACK_SIZE ( configMINIMAL_STACK_SIZE * 4 )

static StaticTask_t xDumperTCB;
static StackType_t xDumperStack[ DUMPER_STACK_SIZE ];
TaskHandle_t xTraceDumperHandle = NULL;

/**
 * @brief Default static initialization for the timeline configuration.
 * @details The values here are dummy initializers. They will be dynamically 
 * overwritten by the code-generated values during pxGetTimelineConfig().
 */
static TimelineConfig_t xTimeline =
{
    .ulMajorFrameTicks = 0,
    .ulSubFrameTicks = 0,
    .ulNumTasks = 0,
    .pxTasks = NULL
};

/*-----------------------------------------------------------*/

/**
 * @brief Provides the memory required by the FreeRTOS Idle task.
 * * @details The Timeline architecture mandates static memory allocation
 * (configSUPPORT_STATIC_ALLOCATION = 1) to ensure strict temporal determinism
 * and prevent heap fragmentation. This callback supplies the statically
 * allocated memory for the Idle task's TCB and stack to the core kernel.
 * @param[out] ppxIdleTaskTCBBuffer Pointer to pass out the static TCB buffer.
 * @param[out] ppxIdleTaskStackBuffer Pointer to pass out the static stack buffer.
 * @param[out] pulIdleTaskStackSize Pointer to pass out the stack size in words.
 */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
    /* Statically allocated buffers must persist across the entire system lifecycle. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = ( uint32_t )configMINIMAL_STACK_SIZE;
}

/*-----------------------------------------------------------*/

/**
 * @brief Retrieves the actively bound timeline configuration.
 *
 * @details Extracts the dynamically generated configuration arrays and 
 * temporal bounds exported by the Python tooling. This links the user-defined 
 * JSON specification to the underlying kernel runtime.
 *
 * @return Constant pointer to the populated timeline configuration.
 */
const TimelineConfig_t * pxGetTimelineConfig( void )
{
    /* External linkage to the auto-generated task configuration arrays.
     * This decouples the core OS timeline logic from the user-specific
     * application payload, facilitating code generation from high-level scripts. */
    extern TimelineTaskConfig_t xTaskConfig[ ];
    extern const uint32_t ulNumTasks;
    extern const uint32_t ulMajorFrameTicks;
    extern const uint32_t ulSubFrameTicks;

    xTimeline.ulMajorFrameTicks = ulMajorFrameTicks;
    xTimeline.ulSubFrameTicks   = ulSubFrameTicks;
    xTimeline.pxTasks           = xTaskConfig;
    xTimeline.ulNumTasks        = ulNumTasks;
    
    return &xTimeline;
}

/*-----------------------------------------------------------*/

/**
 * @brief Internal utility to print the sub-frame assignment over UART.
 *
 * Provides human-readable validation of the static scheduling configuration
 * prior to the commencement of the first major frame.
 *
 * @param[in] pxCfg Pointer to the active timeline configuration.
 */
void prvTimeline_PrintSubFrames( const TimelineConfig_t * pxCfg )
{
    char cBuffer[ 64 ];
    uint32_t ulIndex;
    const TimelineTaskConfig_t * pxTask;

    /* Prevent kernel panics resulting from uninitialized configuration pointers. */
    if( pxCfg == NULL )
    {
        return;
    }

    UART_printf( "\nConfigured sub-frames:\n" );
    
    snprintf( cBuffer, sizeof( cBuffer ),
              "ulNumTasks\tpxTasks\t\tsubFrameTicks\tmajorFrameTicks\n%lu\t\t%p\t%lu\t\t%lu\n",
              ( unsigned long ) pxCfg->ulNumTasks,
              ( void * ) pxCfg->pxTasks,
              ( unsigned long ) pxCfg->ulSubFrameTicks,
              ( unsigned long ) pxCfg->ulMajorFrameTicks );
              
    UART_printf( cBuffer );

    /* Iterate through the configuration and print the logical sub-frame assignment.
     * This is crucial for verifying that the timeline offline sorting phase correctly
     * partitioned the tasks within the major frame boundaries. */
    for( ulIndex = 0; ulIndex < pxCfg->ulNumTasks; ulIndex++ )
    {
        pxTask = &( pxCfg->pxTasks[ ulIndex ] );
        snprintf( cBuffer, sizeof( cBuffer ),
                  "Task: %s\t->\tsub-frame: %lu\n",
                  pxTask->pcName,
                  ( unsigned long ) pxTask->ulSubframe_id );
                  
        UART_printf( cBuffer );
    }

    UART_printf( "\n\nSTART SIMULATION\n\n" );
}

/*-----------------------------------------------------------*/

/**
 * @brief Initializes the Time-Triggered scheduling environment.
 *
 * @param[in] pxConfig Constant pointer to the user-defined timeline configuration structure.
 */
void vConfigureScheduler( const TimelineConfig_t * pxConfig )
{
    uint32_t ulIndex;
    TimelineTaskConfig_t * pxCurrentTaskCfg;

    /* Initialize foundational subsystems required by the scheduler. */
    UART_init();
    vTraceInit();

    /* Delegate configuration sorting and temporal validation to the backend.
     * The system must never start if the timeline parameters violate real-time
     * constraints (e.g., overlapping HRT tasks or tasks exceeding the major frame). */
    if ( xTimelineInternal_InitAndValidate( pxConfig ) == pdFAIL )
    {
        UART_printf( "FATAL: Timeline initialization failed due to temporal constraint violation. System halted.\n" );
        configASSERT( pdFALSE );
        for( ;; )
        {
            /* Infinite loop to halt system on critical failure. */
        }
    }

    /* We don't need to suspend standerd FreeRTOS scheduling mechanisms because
     * the initializzation is done before the call vTaskStartScheduler() that
     * starts the actual FreeRTOS scheduler. */
    for( ulIndex = 0; ulIndex < pxConfig->ulNumTasks; ulIndex++ )
    {
        pxCurrentTaskCfg = &( pxConfig->pxTasks[ ulIndex ] );

        /* Static allocation, to prevents non-deterministic memory fragmentation
            * and bounds the initialization execution time. */
        pxCurrentTaskCfg->xTaskHandle = xTaskCreateStatic (
            pxCurrentTaskCfg->pvTaskFunction,
            pxCurrentTaskCfg->pcName,
            pxCurrentTaskCfg->xStackDepth,
            ( void * ) pxCurrentTaskCfg->pvTaskParams,
            tskIDLE_PRIORITY + 1,  /* Not really relevant, as the timeline scheduler will manage task execution explicitly. */
            pxCurrentTaskCfg->pxStackBuffer,
            pxCurrentTaskCfg->pxTaskBuffer );

        /* Ensure task creation was successful. Memory is statically allocated,
            * so failure implies a configuration error. */
        configASSERT( pxCurrentTaskCfg->xTaskHandle != NULL );

        /* All timeline tasks begin in a suspended state;
            * activation is strictly controlled by the tick hook. */
        vTaskTimelineSuspend( pxCurrentTaskCfg->xTaskHandle );
    }

    xTraceDumperHandle = xTaskCreateStatic(
        vTraceDumperTask,
        "Dumper",
        DUMPER_STACK_SIZE,
        NULL,
        configMAX_PRIORITIES - 1,
        xDumperStack,
        &xDumperTCB );

    configASSERT( xTraceDumperHandle != NULL );

    /* Signal the backend engine to enable custom scheduling hooks. */
    vTimelineInternal_Activate();

    /* Log configuration (Integrated inside the initialization sequence for validation). */
    prvTimeline_PrintSubFrames( pxConfig );
}
