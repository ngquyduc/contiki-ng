#include <stdio.h>
#include <math.h>

#include "contiki.h"
#include "sys/etimer.h"
#include "board-peripherals.h"
#include "buzzer.h"

PROCESS(task3, "task3");
AUTOSTART_PROCESSES(&task3);

const int MOTION_FREQUENCY = 10;
const int LIGHT_FREQUENCY = 4;

static void init_mpu_reading(void) {
    mpu_9250_sensor.configure(SENSORS_ACTIVE, MPU_9250_SENSOR_TYPE_ALL);
}

static void init_opt_reading(void) {
    SENSORS_ACTIVATE(opt_3001_sensor);
}

static double get_light(void) {
    int value = opt_3001_sensor.value(0);
    if (value != CC26XX_SENSOR_READING_ERROR) {
        return (double) value / 100;
    } else {
        return -1.0;
    }
}

static double get_motion(void) {
    // 1 is normal
    double x_acc = (double) mpu_9250_sensor.value(MPU_9250_SENSOR_TYPE_ACC_X) / 100;
    double y_acc = (double) mpu_9250_sensor.value(MPU_9250_SENSOR_TYPE_ACC_Y) / 100;
    double z_acc = (double) mpu_9250_sensor.value(MPU_9250_SENSOR_TYPE_ACC_Z) / 100;

    return sqrt(x_acc * x_acc + y_acc * y_acc + z_acc * z_acc);
}

PROCESS_THREAD(task3, ev, data) {

    static struct etimer timer;
    static int state = 0; // 0: idle, 1: interim, 2: buzz, 3: wait
    static int buzz_count = 0;
    static double light_value = -1.0;

    PROCESS_BEGIN();
    buzzer_init();
    init_mpu_reading();
    init_opt_reading(); // Initialize light sensor at startup
    printf("\nThe value of CLOCK_SECOND is %d",CLOCK_SECOND);

    while (1) {
        if (state == 0) {
            // Reset buzz count when entering IDLE state
            buzz_count = 0;
            
            etimer_set(&timer, CLOCK_SECOND / MOTION_FREQUENCY);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            double motion = get_motion();
            printf("\nMotion: %d", (int) (motion * 100));
            // motion (acceleration) is 1 by default due to earth's gravity.
            if (motion > 2.0) {
                state = 1;
                // Initialize light sensor when motion is detected
                init_opt_reading();
                // Reset light value when transitioning to INTERIM
                light_value = -1.0;
            }
        } else if (state == 1) {
            etimer_set(&timer, CLOCK_SECOND / LIGHT_FREQUENCY);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            double current_light = get_light();
            printf("\nInterim, Current light: %d, Old light: %d", (int) (current_light * 100), (int) (light_value * 100));
            if (light_value != -1.0 && abs(current_light - light_value) > 300) {
                state = 2;
                buzz_count = 0; // Reset buzz count before starting to buzz
            }
            light_value = current_light;
        } else if (state == 2) {
            etimer_set(&timer, CLOCK_SECOND * 2);
            buzzer_start(5000);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            buzzer_stop();
            
            // Increment buzz count
            buzz_count++;

            // Getting another light reading so if light changes during buzzing it stops
            init_opt_reading();
            etimer_set(&timer, CLOCK_SECOND / LIGHT_FREQUENCY);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            double current_light = get_light();
            printf("\nBuzz, Current light: %d, Old light: %d, Buzz count: %d", 
                  (int) (current_light * 100), (int) (light_value * 100), buzz_count);
                  
            // Check if we should stop buzzing (either light changed or max buzzes reached)
            if ((light_value != -1.0 && abs(current_light - light_value) > 300) || buzz_count >= 5) {
                state = 0;
                light_value = -1.0; // Reset light value when returning to IDLE
            } else {
                state = 3;
            }
            light_value = current_light;
        } else if (state == 3) {
            // Subtracting light reading delay to make up for the delay in state 2.
            etimer_set(&timer, CLOCK_SECOND * 4 - CLOCK_SECOND / LIGHT_FREQUENCY);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            state = 2;
        }
    }

    PROCESS_END();
}