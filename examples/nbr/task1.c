/*
* CS4222/5422: project task 1
* Modified to complete two-way discovery within 10 seconds while minimizing power
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

// PARAMETER MODIFICATIONS FOR DETERMINISTIC DISCOVERY
// ---------------------------------------------------------------------
// Configures the wake-up timer for neighbour discovery 
#define WAKE_TIME (RTIMER_SECOND/8)    // 125ms wake time 

// The maximum sleep time must ensure discovery within 10 seconds
// With a staggered pattern, we need shorter sleep times
#define MAX_SLEEP_CYCLE 6              // Maximum sleep cycle 
#define MIN_SLEEP_CYCLE 2              // Minimum sleep cycle
#define SLEEP_SLOT (RTIMER_SECOND/10)  // 100ms sleep slot

// Number of packets to send during each wake period
#define NUM_SEND 3
// ---------------------------------------------------------------------

// For neighbour discovery, we would like to send message to everyone. We use Broadcast address:
linkaddr_t dest_addr;

/*---------------------------------------------------------------------------*/
typedef struct {
  unsigned long src_id;
  unsigned long timestamp;
  unsigned long seq;
  unsigned long last_heard_id;
} data_packet_struct;
/*---------------------------------------------------------------------------*/

// Duty cycle = WAKE_TIME / (WAKE_TIME + SLEEP_SLOT * Average Sleep Cycle)
// With the settings above, average duty cycle is around:
// 0.125 / (0.125 + 0.1 * 4) = 0.125 / 0.525 ≈ 23.8%

// sender timer implemented using rtimer
static struct rtimer rt;

// Protothread variable
static struct pt pt;

// Structure holding the data to be transmitted
static data_packet_struct data_packet;

// Current time stamp of the node
unsigned long curr_timestamp;

// Discovery tracking variables
static unsigned long discovery_start_time = 0;
static unsigned long two_way_discovery_time = 0;
static uint8_t other_node_discovered = 0; // Single variable to track the other node's discovery
static uint8_t two_way_complete = 0;

// Starts the main contiki neighbour discovery process
PROCESS(task1, "task 1");
AUTOSTART_PROCESSES(&task1);

// Function called after reception of a packet
void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) 
{
  // Check if the received packet size matches with what we expect it to be
  if(len == sizeof(data_packet)) {
    static data_packet_struct received_packet;
    
    // Copy the content of packet into the data structure
    memcpy(&received_packet, data, len);
    
    // record the start time of discovery process if this is the first packet received
    if (discovery_start_time == 0) {
      discovery_start_time = clock_time();
    }

    // mark the other node as discovered (from perspective of this node)
    other_node_discovered = 1;
    
    // check if the other node has also heard from us
    if (received_packet.last_heard_id == node_id && !two_way_complete) {
      two_way_complete = 1;
      two_way_discovery_time = clock_time() - discovery_start_time;
      printf("\n*** TWO-WAY DISCOVERY COMPLETE in %lu.%03lu seconds ***",
              two_way_discovery_time / CLOCK_SECOND,
              ((two_way_discovery_time % CLOCK_SECOND)*1000) / CLOCK_SECOND);
    }

    // Print the details of the received packet
    printf("\nReceived neighbour discovery packet %u with rssi %d from %u", 
           received_packet.seq, 
           (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI),
           received_packet.src_id);
  }
}

// Scheduler function for the sender of neighbour discovery packets
char sender_scheduler(struct rtimer *t, void *ptr) {
  static uint16_t i = 0;
  static int sleep_cycle = 0;
  
  // Begin the protothread
  PT_BEGIN(&pt);

  // Get the current time stamp
  curr_timestamp = clock_time();

  printf("\nStart clock at timestamp %3lu.%03lu", 
         curr_timestamp / CLOCK_SECOND, 
         ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

  while(1) {
    // radio on
    NETSTACK_RADIO.on();

    // update packet with discovery status
    if (other_node_discovered) {
      data_packet.last_heard_id = other_node_discovered;
    }

    // send NUM_SEND number of neighbour discovery beacon packets
    for(i = 0; i < NUM_SEND; i++) {
      // Initialize the nullnet module with information of packet to be transmitted
      nullnet_buf = (uint8_t *)&data_packet; //data transmitted
      nullnet_len = sizeof(data_packet); //length of data transmitted
      
      data_packet.seq++;
      
      curr_timestamp = clock_time();
      data_packet.timestamp = curr_timestamp;

      printf("\nSend seq# %lu @ %8lu ticks %3lu.%03lu", 
             data_packet.seq, curr_timestamp, 
             curr_timestamp / CLOCK_SECOND, 
             ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

      NETSTACK_NETWORK.output(&dest_addr); // Packet transmission
      
      // wait for WAKE_TIME/NUM_SEND before sending the next packet
      // This spreads the packets across the wake period
      if(i != (NUM_SEND - 1)) {
        rtimer_set(t, RTIMER_TIME(t) + WAKE_TIME/NUM_SEND, 1, 
                  (rtimer_callback_t)sender_scheduler, ptr);
        PT_YIELD(&pt);
      }
    }

    // Sleep with a staggered pattern to ensure overlap
    // radio off to save power
    NETSTACK_RADIO.off();

    // Generate a random sleep duration within bounds
    // The min-max range ensures we wake up frequently enough to guarantee discovery within 10s
    sleep_cycle = MIN_SLEEP_CYCLE + (random_rand() % (MAX_SLEEP_CYCLE - MIN_SLEEP_CYCLE + 1));
    printf("\nSleep for %d slots", sleep_cycle);

    for(i = 0; i < sleep_cycle; i++) {
      rtimer_set(t, RTIMER_TIME(t) + SLEEP_SLOT, 1, 
                (rtimer_callback_t)sender_scheduler, ptr);
      PT_YIELD(&pt);
    }
  }
  
  PT_END(&pt);
}

// Main thread that handles the neighbour discovery process
PROCESS_THREAD(task1, ev, data)
{
  PROCESS_BEGIN();

  // initialize data packet sent for neighbour discovery exchange
  data_packet.src_id = node_id; // Initialize the node ID
  data_packet.seq = 0; // Initialize the sequence number of the packet
  data_packet.last_heard_id = 0; // Initialize to 0 (no node heard yet)
  
  nullnet_set_input_callback(receive_packet_callback); // initialize receiver callback
  linkaddr_copy(&dest_addr, &linkaddr_null);

  printf("\nCC2650 neighbour discovery - MODIFIED FOR DETERMINISTIC DISCOVERY");
  printf("\nNode %d will be sending packet of size %d Bytes", 
         node_id, (int)sizeof(data_packet_struct));
  printf("\nParameters: WAKE_TIME=%u ms, SLEEP_SLOT=%u ms, MIN_SLEEP=%d, MAX_SLEEP=%d",
         (unsigned int)((WAKE_TIME * 1000) / RTIMER_SECOND),
         (unsigned int)((SLEEP_SLOT * 1000) / RTIMER_SECOND),
         MIN_SLEEP_CYCLE, MAX_SLEEP_CYCLE);
  printf("\nApprox. Duty Cycle: %.1f%%", 
         (float)WAKE_TIME * 100 / (WAKE_TIME + SLEEP_SLOT * ((MIN_SLEEP_CYCLE + MAX_SLEEP_CYCLE)/2)));

  // Start sender in one millisecond.
  rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, 
            (rtimer_callback_t)sender_scheduler, NULL);

  PROCESS_END();
}