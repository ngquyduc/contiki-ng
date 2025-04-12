#include <stdio.h>
#include <math.h>
#include <string.h>

#include "contiki.h"
#include "board-peripherals.h"
#include "node-id.h"

#include "sys/pt.h"
#include "sys/cc.h"
#include "sys/critical.h"
#include "sys/etimer.h"

#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "net/linkaddr.h"

#include "defs_and_types_2.h"

/*****************************************************/

PROCESS(sensing_process, "Node A | SENSE process");
PROCESS(sending_process, "NODE A | SEND PROCESS");
AUTOSTART_PROCESSES(&sending_process, &sensing_process);

/*****************************************************/

linkaddr_t dest_addr;

/*****************************************************/

#define SENSE_FREQUENCY 1 // 1 Hz
#define SEND_FREQUENCY 1 // 1 Hz
#define POLL_FREQUENCY 0.2 // 0.2 Hz
#define TIMEOUT 5 // 5 seconds
#define SLEEP_CYCLE 7

/*****************************************************/

static int state = 0; // 0: neighbor discovery, 1: link quality check, 2: send data
static int data_counter = 0;
static data_tuple_struct data_array[MAX_NUM_DATA];
static int send_counter = 0;
static bool send_done = false;
static struct rtimer rt;
static struct pt pt;
static data_packet_struct data_packet;
static nbr_packet_struct nbr_packet;
static ack_packet_struct ack_packet;
unsigned long curr_timestamp;
static bool both_way_discoverd = false;
static uint8_t good_quality = 0;
static short received_rssi = -100;

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

/*****************************************************/

void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) 
{
	received_rssi = (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI);
	if (len == sizeof(nbr_packet) && state == 0) {
		static nbr_packet_struct nbr_packet_received;
		memcpy(&nbr_packet_received, data, len);
		nbr_packet.last_discovered_node_id = nbr_packet_received.src_id;
		printf("\nNODE A | SEND PROCESS:  Received neighbour discovery packet with rssi %d from node ID %d", (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI), nbr_packet_received.src_id);
		if (nbr_packet_received.last_discovered_node_id == node_id) {
			linkaddr_copy(&dest_addr, src);
			both_way_discoverd = true;
		}
	} 
	else if (len == sizeof(nbr_packet) && state == 1) {
		static nbr_packet_struct link_quality_packet_received;
		memcpy(&link_quality_packet_received, data, len);
		printf("\nNODE A | SEND PROCESS:  Received link quality check packet with rssi %d from node id %d", (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI), link_quality_packet_received.src_id);
		if ((signed short) packetbuf_attr(PACKETBUF_ATTR_RSSI) > -60) {
			good_quality++;
		} else {
			good_quality = 0;
		}
	} else if (len == sizeof(ack_packet) && state == 2) {
		static ack_packet_struct ack_packet;
		memcpy(&ack_packet, data, len);
		if (ack_packet.seq == send_counter && send_counter < MAX_NUM_DATA) {
			printf("\nNODE A | SEND PROCESS:  Received ack from Node B. Proceed to send next packet.");
			send_counter++;
		}
	}
}

char sender_scheduler(struct rtimer *t, void *ptr) {
	static uint16_t i = 0;
	static int NumSleep=0;

	PT_BEGIN(&pt);
	curr_timestamp = clock_time();

	while(state == 0) {
	  	NETSTACK_RADIO.on();
		for (i = 0; i < NUM_SEND; i++) {
			nullnet_buf = (uint8_t *)&nbr_packet;
			nullnet_len = sizeof(nbr_packet);

			printf("\nNODE A | SEND PROCESS:  Send neighbour discovery packet.");
			NETSTACK_NETWORK.output(&dest_addr);
			if (i != (NUM_SEND - 1)) {
				rtimer_set(t, RTIMER_TIME(t) + WAKE_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
				PT_YIELD(&pt);
			}
	  	}
		if (SLEEP_CYCLE != 0) {
			NETSTACK_RADIO.off();
			NumSleep = SLEEP_CYCLE;
			printf("\nNODE A | SEND PROCESS:  Sleep for %d slots.", NumSleep);
			for (i = 0; i < NumSleep; i++) {
				rtimer_set(t, RTIMER_TIME(t) + SLEEP_SLOT, 1, (rtimer_callback_t)sender_scheduler, ptr);
				PT_YIELD(&pt);
			}
		}
	}
	PT_END(&pt);
}

/*****************************************************/

PROCESS_THREAD(sensing_process, ev, data)
{
    static struct etimer timer;
	static int_master_status_t status;
    
    PROCESS_BEGIN();
	printf("\n/*****************************************************/");
	printf("\nNode A | SENSE PROCESS: Start sensing.");
	printf("\n/*****************************************************/");
	init_mpu_reading();
	get_light_reading();
	etimer_set(&timer, CLOCK_SECOND / SENSE_FREQUENCY);
    
    while(1)
    {
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

		// Check if max number of data reached
		status = critical_enter();
		if (data_counter >= MAX_NUM_DATA) {
			printf("\nNode A | SENSE PROCESS: Max number of data reached.");
			critical_exit(status);
			break;
		}
		
		// Read data
		data_tuple_struct data_tuple;
		data_tuple.light = get_light_reading();
		data_tuple.motion = get_motion_reading();

		// Store data
        status = critical_enter();
		data_array[data_counter] = data_tuple;
		printf("\nNode A | SENSE PROCESS: Stored packet #%d light(%d lux) and motion(%d).", data_counter, (int) (data_tuple.light * 100), (int) (data_tuple.motion * 100));
		data_counter++;
        critical_exit(status);

        etimer_reset(&timer);
    }

	// Print out all data for debug
	while(1) {
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
		status = critical_enter();
		if (send_done) {
			printf("\nNODE A | SENSE PROCESS: Light: ");
			for (int i = 0; i < MAX_NUM_DATA - 1; i++) {
				printf("%d.%02d, ", (int)data_array[i].light, ((int)(data_array[i].light * 100) % 100));
			}
			printf("%d.%02d", (int)data_array[MAX_NUM_DATA - 1].light, ((int)(data_array[MAX_NUM_DATA - 1].light * 100) % 100));
			printf("\nNODE A | SENSE PROCESS: Motion: ");
			for (int i = 0; i < MAX_NUM_DATA - 1; i++) {
				printf("%d.%02d, ", (int)data_array[i].motion, ((int)(data_array[i].motion * 100) % 100));
			}
			printf("%d.%02d", (int)data_array[MAX_NUM_DATA - 1].motion, ((int)(data_array[MAX_NUM_DATA - 1].motion * 100) % 100));
			break;
		}
        critical_exit(status);
		etimer_reset(&timer);
	}

	printf("\nNODE A | SENSE PROCESS: FINISHED.");
    
    PROCESS_END();
}  

PROCESS_THREAD(sending_process, ev, data)
{
	static struct etimer wait_timer;
    PROCESS_BEGIN();

	// Neighbor discovery
	printf("\nNODE A | SEND PROCESS:  Start Neighbor discovery.");

	nbr_packet.src_id = node_id;
	nullnet_set_input_callback(receive_packet_callback);
	linkaddr_copy(&dest_addr, &linkaddr_null);
	rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);
	etimer_set(&wait_timer, CLOCK_SECOND * 5);
    while(state == 0) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer));
        etimer_reset(&wait_timer);
		if (both_way_discoverd) {
			curr_timestamp = clock_time();
			printf("\nNODE A | SEND PROCESS:  %3lu DETECT %d", curr_timestamp / CLOCK_SECOND, nbr_packet.last_discovered_node_id);
			state = 1;
		}
    }
    
	static struct etimer timer;
	static int available_data_counter = 0;
    
    while(1)
    {
		if (state == 1) {
			NETSTACK_RADIO.on();
			// Send discovery packet to Node B to check link quality
			printf("\nNODE A | SEND PROCESS:  Check link quality.");
			etimer_set(&timer, CLOCK_SECOND / POLL_FREQUENCY);
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

			if (good_quality >= 5) {
				curr_timestamp = clock_time();
				printf("\nNODE A | SEND PROCESS:  %3lu TRANSFER %d RSSI: %d", curr_timestamp / CLOCK_SECOND, nbr_packet.last_discovered_node_id, received_rssi);
				state = 2;
			}
		} 
		else if (state == 2) {

			// Check if all data sent
			int_master_status_t status = critical_enter();
			if (send_counter >= MAX_NUM_DATA) {
				send_done = true;
				printf("\nNODE A | SEND PROCESS:  All data sent.");
				critical_exit(status);
				break;
			}

			// Get the current available data counter
			status = critical_enter();
			available_data_counter = data_counter;
			printf("\nNODE A | SEND PROCESS:  Available data counter: %d.", available_data_counter);
			critical_exit(status);
			
			while (send_counter < available_data_counter && send_counter < MAX_NUM_DATA) {
				// Send all the current available packets
				etimer_set(&timer, CLOCK_SECOND / SEND_FREQUENCY);
				PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
				nullnet_set_input_callback(receive_packet_callback);
				data_packet.data_tuple = data_array[send_counter];
				data_packet.src_id = node_id;
				data_packet.seq = send_counter;
				nullnet_buf = (uint8_t *)&data_packet;
				nullnet_len = sizeof(data_packet);
				NETSTACK_NETWORK.output(&dest_addr);
				printf("\nNODE A | SEND PROCESS:  Sent packet #%d.", send_counter);
			}

			if (send_counter >= MAX_NUM_DATA) {
				break;
			}
		}
    }

	printf("\nNODE A | SEND PROCESS:  FINISHED.");
    
    PROCESS_END();
}