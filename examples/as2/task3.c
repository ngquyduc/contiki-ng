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
    static int light_changed = 0; // 0 is no light changed, 1 is light changed
    static int i = 0;

    PROCESS_BEGIN();
    buzzer_init();
    init_mpu_reading();

    while (1) {
        if (state == 0) {
            etimer_set(&timer, CLOCK_SECOND / MOTION_FREQUENCY);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            double motion = get_motion();
            printf("\nIDLE | Motion: %d", (int) (motion * 100));
            // motion (acceleration) is 1 by default due to earth's gravity.
            if (motion > 2.0) {
                // if there is motion change, go to INTERIM
                state = 1;
            }
        } else if (state == 1) {
            init_opt_reading();
            etimer_set(&timer, CLOCK_SECOND / LIGHT_FREQUENCY);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
            double current_light = get_light();
            printf("\nINTERIM | Current light: %d, Old light: %d", (int) (current_light * 100), (int) (light_value * 100));
            if (light_value != -1.0 && abs(current_light - light_value) > 300) {
                // if there is light change, go to BUZZ
                state = 2;
            }
            light_value = current_light;
        } else if (state == 2) {
            buzzer_start(5000);
            i = 0;
            // buzz for 8 times (0.25s - 4Hz) equals 2 seconds
            while (i < 8) {
                i++;
                // keep checking the light reading
                init_opt_reading();
                etimer_set(&timer, CLOCK_SECOND / LIGHT_FREQUENCY);
                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
                double current_light = get_light();
                printf("\nBUZZ %d | Current light: %d, Old light: %d", i, (int) (current_light * 100), (int) (light_value * 100));
                if (light_value != -1.0 && abs(current_light - light_value) > 300) {
                    light_changed = 1;
                }
                light_value = current_light;
            }
            buzzer_stop();
            if (light_changed == 1) {
                // if the light change during BUZZ or WAIT, go back to IDLE
                state = 0;
                light_changed = 0;
            } else {
                // if there is no light change during BUZZ or WAIT, go to WAIT
                state = 3;
            }

        } else if (state == 3) {
            // Subtracting light reading delay to make up for the delay in state 2.
            i = 0;
            // wait for 16 times (0.25s - 4Hz) equals 4 seconds
            while (i < 16) {
                i++;
                // keep checking the light reading
                init_opt_reading();
                etimer_set(&timer, CLOCK_SECOND / LIGHT_FREQUENCY);
                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
                double current_light = get_light();
                printf("\nWAIT %d | Current light: %d, Old light: %d", i, (int) (current_light * 100), (int) (light_value * 100));
                if (light_value != -1.0 && abs(current_light - light_value) > 300) {
                    light_changed = 1;
                }
                light_value = current_light;
            }
            state = 2;
        }
    }

    PROCESS_END();
}