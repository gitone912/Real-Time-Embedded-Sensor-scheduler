#ifndef SENSOR_QUEUE_H
#define SENSOR_QUEUE_H

#include "FreeRTOS.h"
#include "queue.h"
#include "Headers/sensor.h"

void vSensorQueueInit( void );

BaseType_t xSensorQueueSend( const SensorData_t *pxData );

BaseType_t xSensorQueueReceive( SensorData_t *pxData );

uint32_t ulSensorQueueGetSentCount( void );

uint32_t ulSensorQueueGetDroppedCount( void );

#endif
