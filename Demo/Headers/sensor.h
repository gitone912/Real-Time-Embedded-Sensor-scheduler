#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

typedef struct
{
    uint32_t timestamp;
    int32_t temperature_c10;
} SensorData_t;
void vSensorPrintStats( void );
void vSensorInit( void );
SensorData_t xReadTemperature( void );

#endif
