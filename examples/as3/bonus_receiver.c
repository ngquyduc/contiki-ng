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
#define SEND_INTERVAL (CLOCK_SECOND / 4)
static linkaddr_t dest_addr = {{ 0x00, 0x12, 0x4b, 0x00, 0x12, 0x05, 0x15, 0x71 }}; //replace this with your receiver's link address
static unsigned rssi = 0;


/*---------------------------------------------------------------------------*/
PROCESS(unicast_process, "One to One Communication");
AUTOSTART_PROCESSES(&unicast_process);

/*---------Callback executed immediately after reception---------*/
void input_callback(const void *data, uint16_t len,
const linkaddr_t *src, const linkaddr_t *dest)
{
  if(len == sizeof(unsigned)) {
    unsigned count;
    memcpy(&count, data, sizeof(count));
    printf("\n %u | %d", count, (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI));
    rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
    NETSTACK_NETWORK.output(&dest_addr); //Packet transmission
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_process, ev, data)
{
  PROCESS_BEGIN();

  printf("%d\n", SEND_INTERVAL);

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&rssi; //data transmitted
  nullnet_len = sizeof(rssi); //length of data transmitted
  nullnet_set_input_callback(input_callback); //initialize receiver callback

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
