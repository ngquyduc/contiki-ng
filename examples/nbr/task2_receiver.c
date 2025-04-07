/*
* CS4222/5422: Assignment 3b
* Perform neighbour discovery
*/

#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "lib/random.h"
#include "net/linkaddr.h"
#include <string.h>
#include <stdio.h>
#include "node-id.h"

// Identification information of the node


// Configures the wake-up timer for neighbour discovery
#define WAKE_TIME RTIMER_SECOND/10    // 10 HZ, 0.1s

#define SLEEP_CYCLE  9        	      // 0 for never sleep
#define SLEEP_SLOT RTIMER_SECOND/10   // sleep slot should not be too large to prevent overflow

// For neighbour discovery, we would like to send message to everyone. We use Broadcast address:
linkaddr_t dest_addr;

#define NUM_SEND 2
/*---------------------------------------------------------------------------*/
typedef struct {
  unsigned long src_id;
} nbr_packet_struct;

typedef struct {
	double light;
	double motion;
} data_tuple_struct;

typedef struct {
	unsigned long src_id;
	unsigned long seq;
 	data_tuple_struct data_tuple;
} data_packet_struct;

typedef struct {
  unsigned long src_id;
	unsigned long seq;
} ack_packet_struct;

/*---------------------------------------------------------------------------*/
// duty cycle = WAKE_TIME / (WAKE_TIME + SLEEP_SLOT * SLEEP_CYCLE)
/*---------------------------------------------------------------------------*/

// sender timer implemented using rtimer
static struct rtimer rt;

// Protothread variable
static struct pt pt;

// Structure holding the data to be transmitted
static nbr_packet_struct data_packet;

// array for received data
static double light_data[60];
static double motion_data[60];

// counter for position of data and sequence number for ack
static unsigned long counter = 0;

// state for finite state machine. 0 for neighbor discovery, 1 for link quality check, 2 for receiving data.
static int state = 0;

// Current time stamp of the node
unsigned long curr_timestamp;

// Starts the main contiki neighbour discovery process
PROCESS(nbr_discovery_process, "cc2650 neighbour discovery process");
AUTOSTART_PROCESSES(&nbr_discovery_process);

// Function called after reception of a packet
void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest)
{


  // Check if the received packet size matches with what we expect it to be

  if (len == sizeof(data_packet)) {
    static nbr_packet_struct received_packet_data;

    // Copy the content of packet into the data structure
    memcpy(&received_packet_data, data, len);

    // Get the current time stamp
    curr_timestamp = clock_time();

    // Print the details of the received packet
    printf("\nReceived neighbour discovery packet %lu with rssi %d from %ld at timestamp %3lu.%03lu", received_packet_data.seq, (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI), received_packet_data.src_id, curr_timestamp / CLOCK_SECOND,
    ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);
    state = 1;
  }

  // received data packet
  if (len == sizeof(data_packet_struct)) {
    state = 2;
    static data_packet_struct received_data;
    memcpy(&received_data, data, len);
    received_rssi = (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI);
    if (received_data.seq == counter) {
      ack_packet_struct ack_packet = {data_packet.src_id, counter};
      light_data[counter] = received_data.data_tuple.light;
      motion_data[counter] = received_data.data_tuple.motion;
      counter++;
      if (counter >= 61) {
        printf("\nLight: ");
        for (int i = 0; i < 60; i++) {
          printf("%d.%d, ", (int)light_data[i], (int)((light_data[i] * 100) % 100));
        }
        printf("\nMotion: ");
        for (int i = 0; i < 60; i++) {
          printf("%d.%d,", (int)motion_data[i], (int)((motion_data[i] * 100) % 100));
        }
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

  printf("\nStart clock %lu ticks, timestamp %3lu.%03lu", curr_timestamp, curr_timestamp / CLOCK_SECOND,
  ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

  while(state == 1){
    NETSTACK_RADIO.on();

    // Initialize the nullnet module with information of packet to be trasnmitted
    nullnet_buf = (uint8_t *)&data_packet; //data transmitted
    nullnet_len = sizeof(data_packet); //length of data transmitted

    NETSTACK_NETWORK.output(&dest_addr); //Packet transmission

    rtimer_set(t, RTIMER_TIME(t) + WAKE_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
  }

  while(state == 0){

    // radio on
    NETSTACK_RADIO.on();

    // send NUM_SEND number of neighbour discovery beacon packets
    for(i = 0; i < NUM_SEND; i++){



      // Initialize the nullnet module with information of packet to be trasnmitted
      nullnet_buf = (uint8_t *)&data_packet; //data transmitted
      nullnet_len = sizeof(data_packet); //length of data transmitted

      // printf("\nSend seq# %lu  @ %8lu ticks   %3lu.%03lu\n", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

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
      NumSleep = 8;
      printf("\nSleep for %d slots \n",NumSleep);

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

  PROCESS_BEGIN();

    // initialize data packet sent for neighbour discovery exchange
  data_packet.src_id = node_id; //Initialize the node ID
  // data_packet.seq = 0; //Initialize the sequence number of the packet

  nullnet_set_input_callback(receive_packet_callback); //initialize receiver callback
  linkaddr_copy(&dest_addr, &linkaddr_null);



  printf("\nCC2650 neighbour discovery");
  printf("\nNode %d will be sending packet of size %d Bytes", node_id, (int)sizeof(nbr_packet_struct));

  // Start sender in one millisecond.
  rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);



  PROCESS_END();
}
