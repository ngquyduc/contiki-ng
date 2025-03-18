#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"

#include <string.h>
#include <stdio.h>

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Configuration */
#define SEND_INTERVAL (CLOCK_SECOND)
static linkaddr_t dest_addr = {{ 0x00, 0x12, 0x4b, 0x00, 0x0f, 0x0e, 0x6c, 0x03 }}; //replace this with your receiver's link address

int power = 0;


/*---------------------------------------------------------------------------*/
PROCESS(unicast_process, "One to One Communication");
AUTOSTART_PROCESSES(&unicast_process);

/*---------Callback executed immediately after reception---------*/
void input_callback(const void *data, uint16_t len,
const linkaddr_t *src, const linkaddr_t *dest)
{
  if(len == sizeof(int)) {
    int rssi;
    memcpy(&rssi, data, sizeof(rssi));
    printf("\n %d", rssi);
    if (rssi < -60) {
      power += 1;
    } else if (rssi > 60) {
      power -= 1;
    }
    if (power > 5) {
      power = 5;
    } else if (power < -20) {
      power = -20;
    }
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_process, ev, data)
{
  static struct etimer periodic_timer;
  static unsigned count = 0;

  PROCESS_BEGIN();

  printf("%d\n", SEND_INTERVAL);

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&count; //data transmitted
  nullnet_len = sizeof(count); //length of data transmitted
  nullnet_set_input_callback(input_callback); //initialize receiver callback

  if(!linkaddr_cmp(&dest_addr, &linkaddr_node_addr)) { //ensures destination is not same as sender
    etimer_set(&periodic_timer, SEND_INTERVAL);
    while(true) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      NETSTACK_RADIO.set_value(RADIO_PARAM_TXPOWER, power);
      NETSTACK_NETWORK.output(&dest_addr); //Packet transmission
      LOG_INFO_("\n");
      LOG_INFO("Sending %u to ", count);
      LOG_INFO_LLADDR(&dest_addr);
      LOG_INFO(" with power %d", power);
      count++;
      etimer_reset(&periodic_timer);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
