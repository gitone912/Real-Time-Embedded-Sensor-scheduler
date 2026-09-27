#include "Headers/sensor_queue.h"

#define SENSOR_QUEUE_LENGTH    8U

static StaticQueue_t xSensorQueue;
static uint8_t ucSensorQueueStorage[
    SENSOR_QUEUE_LENGTH * sizeof( SensorData_t )
];

static QueueHandle_t xQueue = NULL;

static uint32_t ulSentCount = 0U;
static uint32_t ulDroppedCount = 0U;

void vSensorQueueInit( void )
{
    ulSentCount = 0U;
    ulDroppedCount = 0U;

    xQueue = xQueueCreateStatic(
        SENSOR_QUEUE_LENGTH,
        sizeof( SensorData_t ),
        ucSensorQueueStorage,
        &xSensorQueue
    );
}

BaseType_t xSensorQueueSend( const SensorData_t *pxData )
{
    BaseType_t xResult;

    if( ( xQueue == NULL ) || ( pxData == NULL ) )
    {
        return pdFALSE;
    }

    xResult = xQueueSend( xQueue, pxData, 0 );

    if( xResult == pdPASS )
    {
        ulSentCount++;
    }
    else
    {
        ulDroppedCount++;
    }

    return xResult;
}

BaseType_t xSensorQueueReceive( SensorData_t *pxData )
{
    if( ( xQueue == NULL ) || ( pxData == NULL ) )
    {
        return pdFALSE;
    }

    return xQueueReceive( xQueue, pxData, 0 );
}

uint32_t ulSensorQueueGetSentCount( void )
{
    return ulSentCount;
}

uint32_t ulSensorQueueGetDroppedCount( void )
{
    return ulDroppedCount;
}
