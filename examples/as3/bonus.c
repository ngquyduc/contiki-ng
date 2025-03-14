#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "dev/radio.h"

#include <string.h>
#include <stdio.h>

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "Adaptive TX"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Configuration */
#define SEND_INTERVAL (CLOCK_SECOND / 2) // 2 packets per second
#define TARGET_RSSI -60  // Target RSSI value in dBm
#define DURATION (60 * CLOCK_SECOND) // 1 minute execution
#define MAX_TXPOWER 5
#define MIN_TXPOWER -21

/*---------------------------------------------------------------------------*/

static linkaddr_t dest_addr = {{ 0x00, 0x12, 0x4b, 0x00, 0x0f, 0x0e, 0x6c, 0x03 }};
// 15: {{ 0x00, 0x12, 0x4b, 0x00, 0x0f, 0x0e, 0x6c, 0x03 }}
// 16: {{ 0x00, 0x12, 0x4b, 0x00, 0x12, 0x05, 0x15, 0x71 }}

static int8_t tx_power = 0; // Initial TX power level
static struct etimer periodic_timer;
static struct etimer stop_timer;
static unsigned count = 0;
static bool is_transmitter;

void set_tx_power(int8_t power) {
  if (NETSTACK_RADIO.set_value(RADIO_PARAM_TXPOWER, power) == RADIO_RESULT_OK) {
    printf("\nTX Power set to %d dBm", power);
  } else {
    printf("\nFailed to set TX power\n");
  }
}

/*---------------------------------------------------------------------------*/

PROCESS(adaptive_tx_process, "One to One Adaptive TX Power Control");
AUTOSTART_PROCESSES(&adaptive_tx_process);

/*---------Callback executed immediately after reception---------*/
void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  if (len == sizeof(int8_t)) {
    int8_t received_rssi;
    memcpy(&received_rssi, data, sizeof(received_rssi));
    printf("\nReceived RSSI feedback: %d dBm", received_rssi);

    // Adjust TX power based on received RSSI
    if (received_rssi < TARGET_RSSI && tx_power < MAX_TXPOWER) {
      tx_power++;
    } else if (received_rssi > TARGET_RSSI && tx_power > MIN_TXPOWER) {
      tx_power--;
    }
    set_tx_power(tx_power);
  }
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(adaptive_tx_process, ev, data) {
  PROCESS_BEGIN();

  set_tx_power(tx_power);
  nullnet_set_input_callback(input_callback);

  etimer_set(&stop_timer, DURATION); // Stop after 1 minute

  if (!linkaddr_cmp(&dest_addr, &linkaddr_node_addr)) { 
    // This node is the transmitter
    is_transmitter = true;
    etimer_set(&periodic_timer, SEND_INTERVAL);
    while (1) {
      PROCESS_WAIT_EVENT();
      if (etimer_expired(&stop_timer)) {
        printf("\nStopping transmission");
        break;
      }
      if (etimer_expired(&periodic_timer)) {
        printf("\nSending packet %u to ", count);
        
        NETSTACK_NETWORK.output(&dest_addr);
        count++;
        etimer_reset(&periodic_timer);
      }
    }
  } else {
    // This node is the receiver
    is_transmitter = false;
    while (1) {
      PROCESS_WAIT_EVENT();
      if (etimer_expired(&stop_timer)) {
          printf("\nStopping reception");
          break;
      }
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
