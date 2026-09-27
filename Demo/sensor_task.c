#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#include "Headers/sensor.h"
#include "Headers/sensor_queue.h"
#include "Headers/timeline.h"
#include "Headers/timeline_internal.h"
#include "Headers/uart.h"

#define SENSOR_HIGH_TEMP_LIMIT    350
#define SENSOR_HIGH_TEMP_LIMIT    350
#define SENSOR_HIGH_TEMP_LIMIT    350

static volatile uint32_t ulSensorSamples = 0U;
static volatile uint32_t ulProcessedSamples = 0U;
static volatile uint32_t ulQueueDrops = 0U;
static volatile uint32_t ulHighTempFaults = 0U;
void vSensorTask( void *pvParameters )
{
    SensorData_t xData;
    uint32_t ulSampleCount = 0U;
    uint32_t ulFaultCount = 0U;
    char cBuffer[80];

    ( void ) pvParameters;


    for( ;; )
    {
        xData = xReadTemperature();
        ulSampleCount++;
         ulSensorSamples++;
         if( ulSampleCount <= 5U )
{
    UART_printf( "SENSOR LOOP\n" );
}

        if( xData.temperature_c10 > SENSOR_HIGH_TEMP_LIMIT )
        {
            ulFaultCount++;
            ulHighTempFaults++;

            snprintf(
                cBuffer,
                sizeof( cBuffer ),
                "SENSOR: t=%lu temp=%ld.%ldC status=HIGH_TEMP faults=%lu\n",
                ( unsigned long ) xData.timestamp,
                ( long ) ( xData.temperature_c10 / 10 ),
                ( long ) ( xData.temperature_c10 % 10 ),
                ( unsigned long ) ulFaultCount
            );
        }
        else
        {
            snprintf(
                cBuffer,
                sizeof( cBuffer ),
                "SENSOR: t=%lu temp=%ld.%ldC status=OK\n",
                ( unsigned long ) xData.timestamp,
                ( long ) ( xData.temperature_c10 / 10 ),
                ( long ) ( xData.temperature_c10 % 10 )
            );
        }

        UART_printf( cBuffer );

        if( xSensorQueueSend( &xData ) != pdPASS )
{
   ulQueueDrops++;
   UART_printf( "SENSOR QUEUE FULL\n" );
}

        vTimelineMarkSRTTaskCompleted();
    }
}
void vSensorProcessingTask( void *pvParameters )
{
    SensorData_t xData;
    char cBuffer[80];

    ( void ) pvParameters;

    for( ;; )
    {
        if( xSensorQueueReceive( &xData ) == pdPASS )
        {
            snprintf(
                cBuffer,
                sizeof( cBuffer ),
                "PROCESSOR: t=%lu temp=%ld.%ldC\n",
                ( unsigned long ) xData.timestamp,
                ( long ) ( xData.temperature_c10 / 10 ),
                ( long ) ( xData.temperature_c10 % 10 )
            );

            UART_printf( cBuffer );
            ulProcessedSamples++;
           if((ulProcessedSamples %10U )==0U){
                 vSensorPrintStats();
           }
        }

        vTimelineMarkSRTTaskCompleted();
    }
}
void vSensorPrintStats( void )
{
    char cBuffer[100];

    snprintf(
        cBuffer,
        sizeof( cBuffer ),
        "SENSOR STATS: samples=%lu processed=%lu drops=%lu high_temp=%lu\n",
        ( unsigned long ) ulSensorSamples,
        ( unsigned long ) ulProcessedSamples,
        ( unsigned long ) ulQueueDrops,
        ( unsigned long ) ulHighTempFaults
    );

    UART_printf( cBuffer );
}
