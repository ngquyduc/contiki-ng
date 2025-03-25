/*
* CS4222/5422: Project
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

// ADDED: Device IDs for measurement
#define DEVICE_A_ID 1  // Change this to match your Device A's node ID
#define DEVICE_B_ID 2  // Change this to match your Device B's node ID

#define WAKE_TIME RTIMER_SECOND/10
#define SLEEP_CYCLE  9
#define SLEEP_SLOT RTIMER_SECOND/10

linkaddr_t dest_addr;

#define NUM_SEND 2

// ADDED: Data structures for time measurement
#define MAX_TIMESTAMPS 100
static unsigned long last_reception_time = 0;
static unsigned long time_intervals[MAX_TIMESTAMPS];
static int interval_count = 0;
static unsigned long first_reception_after_reset = 0;
static unsigned long reset_time = 0;
static int reset_detection = 0;

typedef struct {
  unsigned long src_id;
  unsigned long timestamp;
  unsigned long seq;
} data_packet_struct;

static struct rtimer rt;
static struct pt pt;
static data_packet_struct data_packet;
unsigned long curr_timestamp;

PROCESS(nbr_discovery_process, "cc2650 neighbour discovery process");
AUTOSTART_PROCESSES(&nbr_discovery_process);

void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) 
{
  if(len == sizeof(data_packet)) {
    static data_packet_struct received_packet_data;
    memcpy(&received_packet_data, data, len);
    
    // MODIFIED: Add timestamp measurement logic
    curr_timestamp = clock_time();
    
    // ADDED: Logic for Device A to track packets from Device B
    if(node_id == DEVICE_A_ID && received_packet_data.src_id == DEVICE_B_ID) {
      if(last_reception_time == 0) {
        printf("\nFirst packet received from Device B at time %lu", curr_timestamp);
      } 
      else {
        // Calculate and store interval between successive receptions
        unsigned long interval = curr_timestamp - last_reception_time;
        if(interval_count < MAX_TIMESTAMPS) {
          time_intervals[interval_count++] = interval;
          printf("\nInterval between receptions: %lu ticks (%lu.%03lu seconds)", 
                interval, interval / CLOCK_SECOND,
                ((interval % CLOCK_SECOND)*1000) / CLOCK_SECOND);
        }
      }
      
      // For part (b): Check if this is first packet after reset
      if(reset_detection) {
        unsigned long time_since_reset = curr_timestamp - reset_time;
        printf("\nFirst packet after reset received. Time since reset: %lu ticks (%lu.%03lu seconds)", 
               time_since_reset, time_since_reset / CLOCK_SECOND,
               ((time_since_reset % CLOCK_SECOND)*1000) / CLOCK_SECOND);
        reset_detection = 0;
      }
      
      last_reception_time = curr_timestamp;
    }

    printf("\nReceived neighbour discovery packet %lu with rssi %d from %ld", 
           received_packet_data.seq, 
           (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI),
           received_packet_data.src_id);
    printf("\nAt %lu ticks, timestamp %3lu.%03lu", curr_timestamp, curr_timestamp / CLOCK_SECOND, 
      ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);
  }
}

char sender_scheduler(struct rtimer *t, void *ptr) {
  static uint16_t i = 0;
  static int NumSleep=0;
 
  PT_BEGIN(&pt);

  curr_timestamp = clock_time();

  printf("\nStart clock %lu ticks, timestamp %3lu.%03lu", curr_timestamp, curr_timestamp / CLOCK_SECOND, 
  ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

  while(1){
    NETSTACK_RADIO.on();

    for(i = 0; i < NUM_SEND; i++){
      nullnet_buf = (uint8_t *)&data_packet;
      nullnet_len = sizeof(data_packet);
      
      data_packet.seq++;
      
      curr_timestamp = clock_time();
      data_packet.timestamp = curr_timestamp;

      printf("\nSend seq# %lu  @ %8lu ticks   %3lu.%03lu", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

      NETSTACK_NETWORK.output(&dest_addr);
      
      if(i != (NUM_SEND - 1)){
        rtimer_set(t, RTIMER_TIME(t) + WAKE_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
        PT_YIELD(&pt);
      }
    }

    if(SLEEP_CYCLE != 0){
      NETSTACK_RADIO.off();

      NumSleep = random_rand() % (2 * SLEEP_CYCLE + 1);  
      printf("\nSleep for %d slots",NumSleep);

      for(i = 0; i < NumSleep; i++){
        rtimer_set(t, RTIMER_TIME(t) + SLEEP_SLOT, 1, (rtimer_callback_t)sender_scheduler, ptr);
        PT_YIELD(&pt);
      }
    }
  }
  
  PT_END(&pt);
}

PROCESS_THREAD(nbr_discovery_process, ev, data)
{
  PROCESS_BEGIN();

  data_packet.src_id = node_id;
  data_packet.seq = 0;
  
  nullnet_set_input_callback(receive_packet_callback);
  linkaddr_copy(&dest_addr, &linkaddr_null);

  printf("\nCC2650 neighbour discovery with time measurement");
  printf("\nNode %d will be sending packet of size %d Bytes", node_id, (int)sizeof(data_packet_struct));
  
  // ADDED: Instructions for Device A
  if(node_id == DEVICE_A_ID) {
    printf("\nDevice A: Recording time intervals between packets from Device B");
    printf("\nUse 'z' key to mark when Device B is reset (for part b)");
  }
  
  rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);

  // ADDED: Handle key press for marking Device B reset
  char c;
  while(1) {
    PROCESS_YIELD();
    if(ev == serial_line_event_message) {
      c = ((char *)data)[0];
      if(c == 'z' && node_id == DEVICE_A_ID) {
        printf("\nDevice B reset marked. Waiting for first packet...");
        reset_detection = 1;
        reset_time = clock_time();
      }
    }
  }

  PROCESS_END();
}