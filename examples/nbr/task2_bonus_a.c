#include <stdio.h>
#include <math.h>
#include <string.h>

#include "contiki.h"
#include "board-peripherals.h"
#include "node-id.h"

#include "sys/cc.h"
#include "sys/etimer.h"

#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "net/linkaddr.h"

#include "defs_and_types_2.h"

/*****************************************************/

PROCESS(node_a_bonus, "Node A Bonus");
AUTOSTART_PROCESSES(&node_a_bonus);

/*****************************************************/

#define SENSE_FREQUENCY 1 // 1 Hz
#define SEND_FREQUENCY 1 // 1 Hz
#define POLL_FREQUENCY 0.2 // 0.2 Hz
#define TIMEOUT 5 // 5 seconds
#define SLEEP_CYCLE 7

/*****************************************************/

linkaddr_t dest_addr;

static int state = 0; // 0: neighbor discovery, 1: link quality check, 2: send data
static data_tuple_struct data_array[MAX_NUM_DATA];
static int send_counter = 0;
static data_packet_struct data_packet;
static nbr_packet_struct nbr_packet;
static ack_packet_struct ack_packet;
unsigned long curr_timestamp;
static bool both_way_discoverd = false;
static uint8_t good_quality = 0;
static short received_rssi = -100;
static uint8_t i = 0;
static uint8_t j = 1;

/*****************************************************/

static void init_mpu_reading(void) {
    mpu_9250_sensor.configure(SENSORS_ACTIVE, MPU_9250_SENSOR_TYPE_ALL);
}

static void init_opt_reading(void) {
    SENSORS_ACTIVATE(opt_3001_sensor);
}

static double get_motion_reading(void) {
    double x_acc = (double) mpu_9250_sensor.value(MPU_9250_SENSOR_TYPE_ACC_X) / 100;
    double y_acc = (double) mpu_9250_sensor.value(MPU_9250_SENSOR_TYPE_ACC_Y) / 100;
    double z_acc = (double) mpu_9250_sensor.value(MPU_9250_SENSOR_TYPE_ACC_Z) / 100;

    return sqrt(x_acc * x_acc + y_acc * y_acc + z_acc * z_acc);
}

static double get_light_reading(void) {
    int value = opt_3001_sensor.value(0);
	init_opt_reading();
    if (value != CC26XX_SENSOR_READING_ERROR) {
        return (double)value / 100;
    } else {
        return -1.0;
    }
}

static bool is_significant_motion(double motion) {
	return motion > MOTION_THRESHOLD;
}

/*****************************************************/

void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) 
{
	received_rssi = (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI);
	if (len == sizeof(nbr_packet) && state == 2) {
		static nbr_packet_struct nbr_packet_received;
		memcpy(&nbr_packet_received, data, len);
		nbr_packet.last_discovered_node_id = nbr_packet_received.src_id;
		printf("\nNODE A: Received neighbour discovery packet with rssi %d from node ID %d", (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI), nbr_packet_received.src_id);
		if (nbr_packet_received.last_discovered_node_id == node_id) {
			linkaddr_copy(&dest_addr, src);
			both_way_discoverd = true;
		}
	} 
	else if (len == sizeof(nbr_packet) && state == 3) {
		static nbr_packet_struct link_quality_packet_received;
		memcpy(&link_quality_packet_received, data, len);
		printf("\nNODE A: Received link quality check packet with rssi %d from node id %d", (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI), link_quality_packet_received.src_id);
		if ((signed short) packetbuf_attr(PACKETBUF_ATTR_RSSI) > -60) {
			good_quality++;
		} else {
			good_quality = 0;
		}
	} else if (len == sizeof(ack_packet) && state == 4) {
		static ack_packet_struct ack_packet;
		memcpy(&ack_packet, data, len);
		if (ack_packet.seq == send_counter && send_counter < MAX_NUM_DATA) {
			printf("\nNODE A: Received ack from Node B. Proceed to send next packet.");
			send_counter++;
		}
	}
}

PROCESS_THREAD(node_a_bonus, ev, data)
{
	static struct etimer timer;

    PROCESS_BEGIN();
	printf("\nNODE A: STARTING.");
	init_mpu_reading();
	get_light_reading();

	while (1) {
		if (state == 0) { // idle state
			etimer_set(&timer, CLOCK_SECOND / MOTION_FREQUENCY);
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
			double motion = get_motion_reading();
			// proceed to sensing state if significant motion detected
			printf("\nNODE A: Motion: %d", (int) (motion * 100));
			if (is_significant_motion(motion)) {
				state = 1;
			}
		} else if (state == 1) { // sensing state, reset if significant motion detected
			// collect data at 1 Hz but check for significant motion at 10Hz
			for (i = 0; i < MAX_NUM_DATA; i++) {
				etimer_set(&timer, CLOCK_SECOND / SENSE_FREQUENCY);
				PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
				
				// Read data
				data_tuple_struct data_tuple;
				data_tuple.light = get_light_reading();
				data_tuple.motion = get_motion_reading();
				// reset collection if significant motion detected
				if (is_significant_motion(data_tuple.motion)) {
					state = 1;
					break;
				}
		
				// Store data
				data_array[i] = data_tuple;
				printf("\nNode A: Stored packet #%d light(%d lux) and motion(%d).", i, (int) (data_tuple.light * 100), (int) (data_tuple.motion * 100));
			}
			if (i == MAX_NUM_DATA) {
				state = 2;
			}
		} else if (state == 2) { // neighbor discovery state
			j = 1;
			if (both_way_discoverd) {
				curr_timestamp = clock_time();
				printf("\nNODE A: %3lu DETECT %d", curr_timestamp / CLOCK_SECOND, nbr_packet.last_discovered_node_id);
				state = 3;
				j = 10;
			}
			
			nbr_packet.src_id = node_id;
			nullnet_set_input_callback(receive_packet_callback);
			static int NumSleep=0;

			

			NETSTACK_RADIO.on();
			double motion = get_motion_reading();
			if (is_significant_motion(motion)) {
				printf("\nNODE A: Motion detected. Restart capturing");
				state = 1;
				continue;
			}
			linkaddr_copy(&dest_addr, &linkaddr_null); // broadcast
			while (j > 0) {
				for (i = 0; i < NUM_SEND; i++) {
					nullnet_buf = (uint8_t *)&nbr_packet;
					nullnet_len = sizeof(nbr_packet);

					printf("\nNODE A: Send neighbour discovery packet.");
					NETSTACK_NETWORK.output(&dest_addr);
					if (i != (NUM_SEND - 1)) {
						etimer_set(&timer, E_WAKE_TIME);
						PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
					}
				}
				if (SLEEP_CYCLE != 0) {
					NETSTACK_RADIO.off();
					NumSleep = SLEEP_CYCLE;
					printf("\nNODE A: Sleep for %d slots.", NumSleep);
					for (i = 0; i < NumSleep; i++) {
						etimer_set(&timer, E_SLEEP_SLOT);
						PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
					}
				}
				j--;
			}
		} else if (state == 3) {
			NETSTACK_RADIO.on();
			// Send discovery packet to Node B to check link quality
			printf("\nNODE A: Check link quality.");
			etimer_set(&timer, CLOCK_SECOND / POLL_FREQUENCY);
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

			if (good_quality >= 5) {
				curr_timestamp = clock_time();
				printf("\nNODE A: %3lu TRANSFER %d RSSI: %d", curr_timestamp / CLOCK_SECOND, nbr_packet.last_discovered_node_id, received_rssi);
				state = 4;
			}
		} else if (state == 4) {
			while (send_counter < MAX_NUM_DATA) {
				// Send all the current available packets
				etimer_set(&timer, CLOCK_SECOND / (SEND_FREQUENCY * 4));
				PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
				nullnet_set_input_callback(receive_packet_callback);
				data_packet.data_tuple = data_array[i];
				data_packet.src_id = node_id;
				data_packet.seq = send_counter;
				nullnet_buf = (uint8_t *)&data_packet;
				nullnet_len = sizeof(data_packet);
				NETSTACK_NETWORK.output(&dest_addr);
				printf("\nNODE A: Sent packet #%d.", send_counter);
			}
			printf("\nNODE A: Light: ");
			for (i = 0; i < MAX_NUM_DATA - 1; i++) {
				printf("%d.%02d, ", (int)data_array[i].light, ((int)(data_array[i].light * 100) % 100));
			}
			printf("%d.%02d", (int)data_array[MAX_NUM_DATA - 1].light, ((int)(data_array[MAX_NUM_DATA - 1].light * 100) % 100));
			printf("\nNODE A: Motion: ");
			for (i = 0; i < MAX_NUM_DATA - 1; i++) {
				printf("%d.%02d, ", (int)data_array[i].motion, ((int)(data_array[i].motion * 100) % 100));
			}
			printf("%d.%02d", (int)data_array[MAX_NUM_DATA - 1].motion, ((int)(data_array[MAX_NUM_DATA - 1].motion * 100) % 100));
			break;
		} 
	}

	printf("\nNODE A: FINISHED.");
    
    PROCESS_END();
}