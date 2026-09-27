#include "Headers/sensor.h"
#include <stdint.h>

static uint32_t ulTimestamp = 0U;

void vSensorInit( void )
{
    ulTimestamp = 0U;
}

SensorData_t xReadTemperature( void )
{
    SensorData_t xData;

    xData.timestamp = ulTimestamp;

    /*
     * Temperature stored in tenths of a degree Celsius.
     * 260 = 26.0 C, 270 = 27.0 C, etc.
     */
    xData.temperature_c10 = 260 + ( int32_t )( ( ulTimestamp % 6U ) * 10U );

    ulTimestamp++;

    return xData;
}
