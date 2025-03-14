#include <stdio.h>
#include <math.h>

#include "contiki.h"
#include "sys/etimer.h"
#include "board-peripherals.h"
#include "buzzer.h"

PROCESS(task2, "task2");
AUTOSTART_PROCESSES(&task2);

static void init_mpu_reading(void) {
    mpu_9250_sensor.configure(SENSORS_ACTIVE, MPU_9250_SENSOR_TYPE_ALL);
}

static void init_opt_reading(void) {
    SENSORS_ACTIVATE(opt_3001_sensor);
}

static double get_light(void) {
    int value = opt_3001_sensor.value(0);
    init_opt_reading();
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

PROCESS_THREAD(task2, ev, data) {

    static struct etimer timer;
    static int state = 0; // 0: idle, 1: buzz, 2: wait
    static int buzz_count = 0;
    static double light_value = -1.0;

    PROCESS_BEGIN();
    buzzer_init();
    init_mpu_reading();
    init_opt_reading();
    printf("\nThe value of CLOCK_SECOND is %d",CLOCK_SECOND);

    while (1) {
        if (state == 0) {
            etimer_set(&timer, CLOCK_SECOND / 4);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            double current_light = get_light();
            double motion = get_motion();
            // printed values are 100 times larger because %f does not work.
            printf("\nMotion: %d, light: %d", (int) (motion * 100), (int) (current_light * 100));
            // motion (acceleration) is 1 by default due to earth's gravity.
            if (motion > 2.0 || (light_value != -1.0 && abs(current_light - light_value) > 300)) {
                state = 1;
            }
            light_value = current_light;
        } else if (state == 1) {
            etimer_set(&timer, CLOCK_SECOND * 2);
            buzzer_start(5000);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            buzzer_stop();
            
            // Increment count after buzzing
            buzz_count++;

            // for the 5th (extra) buzz, we don't need to go to WAIT state
            if (buzz_count > 4) {
                state = 0;
                buzz_count = 0;
                // doing the below to reset the light level after buzzing.
                light_value = -1;
                init_opt_reading();
                continue;
            }
            
            state = 2;  // Always go to WAIT state after buzzing
        } else if (state == 2) {
            etimer_set(&timer, CLOCK_SECOND * 2);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            state = 1;
        }
    }

    PROCESS_END();
}