#include "FreeRTOS.h"
#include "task.h"
#include "Headers/timeline.h"
#include "Headers/sensor_queue.h"
int main( void )
{
    /* Fetch the static, auto-generated temporal configuration matrix. */
    const TimelineConfig_t *pxTimeline = pxGetTimelineConfig();
         vSensorQueueInit();
    /* Bootstrap the hardware, trace system, and Time-Triggered engine. */
    vConfigureScheduler( pxTimeline );

    /* Transfer execution control to the FreeRTOS kernel. */
    vTaskStartScheduler();

    /* Execution should theoretically never reach this point.
     * If it does, system RAM was insufficient to allocate the Idle task. */
    for (;;);

    return 0;
}
