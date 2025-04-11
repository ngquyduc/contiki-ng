#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "lib/random.h"
#include "net/linkaddr.h"
#include <string.h>
#include <stdio.h>
#include "node-id.h"

#include "defs_and_types_2.h"

// Identification information of the node
#define SLEEP_CYCLE  9        	      // 0 for never sleep

// For neighbour discovery, we would like to send message to everyone. We use Broadcast address:
linkaddr_t dest_addr;

// sender timer implemented using rtimer
static struct rtimer rt;

// Protothread variable
static struct pt pt;

// Structure holding the data to be transmitted
static nbr_packet_struct nbr_packet;

// array for received data
static data_tuple_struct data_array[MAX_NUM_DATA];
// static double light_data[60];
// static double motion_data[60];

// counter for position of data and sequence number for ack
static unsigned long counter = 0;

// state for finite state machine. 0 for neighbor discovery, 1 for link quality check, 2 for receiving data.
static int state = 0;

// Current time stamp of the node
unsigned long curr_timestamp;
static bool both_way_discoverd = false;

// Starts the main contiki neighbour discovery process
PROCESS(nbr_discovery_process, "cc2650 neighbour discovery process");
AUTOSTART_PROCESSES(&nbr_discovery_process);

// Function called after reception of a packet
void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest)
{
	// Check if the received packet size matches with what we expect it to be
	if (len == sizeof(nbr_packet)) {
		static nbr_packet_struct nbr_packet_received;

		// Copy the content of packet into the data structure
		memcpy(&nbr_packet_received, data, len);
		nbr_packet.last_discovered_node_id = nbr_packet_received.src_id;

		// Get the current time stamp
		curr_timestamp = clock_time();
		printf("\nNODE B: Received neighbour discovery packet with rssi %d from %d", (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI), nbr_packet_received.src_id);
		if (nbr_packet_received.last_discovered_node_id == node_id) {
			linkaddr_copy(&dest_addr, src);
			both_way_discoverd = true;
		}
	}

	// received data packet
	if (len == sizeof(data_packet_struct)) {
		state = 2;
		static data_packet_struct received_data;
		memcpy(&received_data, data, len);
		if (received_data.seq == counter) {
			ack_packet_struct ack_packet = {nbr_packet.src_id, counter};
			data_array[counter].light = received_data.data_tuple.light;
			data_array[counter].motion = received_data.data_tuple.motion;
			printf("\nNODE B: Received data packet with seq %ld from %ld", received_data.seq, received_data.src_id);
			counter++;
			if (counter == MAX_NUM_DATA) {
				printf("\nNODE B: Light: ");
			for (int i = 0; i < MAX_NUM_DATA - 1; i++) {
				printf("%d.%02d, ", (int)data_array[i].light, ((int)(data_array[i].light * 100) % 100));
			}
			printf("%d.%02d", (int)data_array[MAX_NUM_DATA - 1].light, ((int)(data_array[MAX_NUM_DATA - 1].light * 100) % 100));
			printf("\nNODE B: Motion: ");
			for (int i = 0; i < MAX_NUM_DATA - 1; i++) {
				printf("%d.%02d, ", (int)data_array[i].motion, ((int)(data_array[i].motion * 100) % 100));
			}
			printf("%d.%02d", (int)data_array[MAX_NUM_DATA - 1].motion, ((int)(data_array[MAX_NUM_DATA - 1].motion * 100) % 100));
			}
			nullnet_buf = (uint8_t *)&ack_packet;
			nullnet_len = sizeof(ack_packet);
			NETSTACK_NETWORK.output(&dest_addr);
		}
	}
}

// Scheduler function for the sender of neighbour discovery packets
char sender_scheduler(struct rtimer *t, void *ptr) {

	static uint16_t i = 0;
	static int NumSleep=0;

	// Begin the protothread
	PT_BEGIN(&pt);
	// Get the current time stamp
	curr_timestamp = clock_time();

	printf("\nNODE B: Start clock %lu ticks, timestamp %3lu.%03lu", curr_timestamp, curr_timestamp / CLOCK_SECOND,
			((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

	while(state == 1){
		NETSTACK_RADIO.on();

		printf("\nNODE B: Send link quality check packet");

		// Initialize the nullnet module with information of packet to be trasnmitted
		nullnet_buf = (uint8_t *)&nbr_packet; //data transmitted
		nullnet_len = sizeof(nbr_packet); //length of data transmitted

		NETSTACK_NETWORK.output(&dest_addr); //Packet transmission
		rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND), 1, (rtimer_callback_t)sender_scheduler, ptr);
		PT_YIELD(&pt);
	}

	while(state == 0){
		// radio on
		NETSTACK_RADIO.on();

		// send NUM_SEND number of neighbour discovery beacon packets
		for(i = 0; i < NUM_SEND; i++){
			// Initialize the nullnet module with information of packet to be trasnmitted
			nullnet_buf = (uint8_t *)&nbr_packet; //data transmitted
			nullnet_len = sizeof(nbr_packet); //length of data transmitted
			printf("\nNODE B: Send discovery packet");
			NETSTACK_NETWORK.output(&dest_addr); //Packet transmission
			// wait for WAKE_TIME before sending the next packet
			if(i != (NUM_SEND - 1)){
				rtimer_set(t, RTIMER_TIME(t) + WAKE_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
				PT_YIELD(&pt);
			}
		}

		// sleep for a random number of slots
		if(SLEEP_CYCLE != 0){
			// radio off
			NETSTACK_RADIO.off();
			// SLEEP_SLOT cannot be too large as value will overflow,
			// to have a large sleep interval, sleep many times instead
			// get a value that is uniformly distributed between 0 and 2*SLEEP_CYCLE
			// the average is SLEEP_CYCLE
			NumSleep = SLEEP_CYCLE;
			printf("\nNODE B: Sleep for %d slots",NumSleep);

			// NumSleep should be a constant or static int
			for(i = 0; i < NumSleep; i++){
				rtimer_set(t, RTIMER_TIME(t) + SLEEP_SLOT, 1, (rtimer_callback_t)sender_scheduler, ptr);
				PT_YIELD(&pt);
			}
		}
	}
	PT_END(&pt);
}


// Main thread that handles the neighbour discovery process
PROCESS_THREAD(nbr_discovery_process, ev, data)
{
	// static struct etimer periodic_timer;
	static struct etimer wait_timer;
	PROCESS_BEGIN();

	// initialize data packet sent for neighbour discovery exchange
	nbr_packet.src_id = node_id; //Initialize the node ID

	nullnet_set_input_callback(receive_packet_callback); //initialize receiver callback
	linkaddr_copy(&dest_addr, &linkaddr_null);

	printf("\nNODE B: Node %d is starting", node_id);
	printf("\nNODE B: Node %d will be sending packet of size %d Bytes", node_id, (int)sizeof(nbr_packet_struct));

	// Start sender in one millisecond.
	rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);
	etimer_set(&wait_timer, CLOCK_SECOND * 5);
	while(1) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer));
        etimer_reset(&wait_timer);
		if (both_way_discoverd && state <= 1) {
			curr_timestamp = clock_time();
			printf("\nNODE B: %3lu DETECT %d", curr_timestamp / CLOCK_SECOND, nbr_packet.last_discovered_node_id);
			state = 1;
            
            // Start a new rtimer for link quality check (state 1)
            rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);
            printf("\nNODE B: Starting link quality check phase");
		}
	}
	PROCESS_END();
}
