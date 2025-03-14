#include "contiki.h"

#include <stdio.h>
/*---------------------------------------------------------------------------*/
PROCESS(as1_process, "as1 process");
AUTOSTART_PROCESSES(&as1_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(as1_process, ev, data)
{

  PROCESS_BEGIN();

  printf("Nguyen Quy Duc\n");
  printf("Ong Zheng Long\n");

  PROCESS_END();
}