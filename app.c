/* standard library headers */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

/* BG stack headers */
#include "bg_types.h"
#include "gecko_bglib.h"

/* Own header */
#include "app.h"
#include "dump.h"
#include "support.h"
#include "common.h"

struct timeval now, next, timestep;

// App booted flag
static bool appBooted = false;
static struct {
  uint32 advertising_interval;
  uint16 connection_interval, mtu; 
  bd_addr remote;
  uint8 connection, connect, toggle;
} config = { .remote = { .addr = {0,0,0,0,0,0}},
	     .connection = 0xff,
	     .advertising_interval = 160, // 100 ms
	     .connection_interval = 80, // 100 ms
	     .mtu = 23,
	     .toggle = 0,
};
  
const char *getAppOptions(void) {
  return "ta<remote-address>i<interval-ms>";
}

void appOption(int option, const char *arg) {
  double dv;
  switch(option) {
  case 'a':
    parse_address(arg,&config.remote);
    config.connect = 1;
    break;
  case 'i':
    sscanf(arg,"%lf",&dv);
    config.advertising_interval = round(dv/0.625);
    config.connection_interval = round(dv/1.25);
    timestep.tv_sec = 0;
    timestep.tv_usec = 500*dv; // alternating toggles each at connection interval
    break;
  case 't':
    config.toggle = 1;
    break;
  default:
    fprintf(stderr,"Unhandled option '-%c'\n",option);
    exit(1);
  }
}

void appInit(void) {
  if(config.connect || config.toggle) return;
  printf("Usage: host-control-pin [ -t ][ -i interval ][ -a <address> ]\n");
  exit(1);
}

uint8_t cmd[2][3] = { { 0, 2, 6 }, { 0, 2, 7 } };
uint8_t busy;

/***********************************************************************************************//**
 *  \brief  Event handler function.
 *  \param[in] evt Event pointer.
 **************************************************************************************************/
void appHandleEvents(struct gecko_cmd_packet *evt)
{
  
  if (NULL == evt) {
    if(appBooted) {
      gettimeofday(&now,NULL);
      timersub(&next,&now,&now);
      if((now.tv_sec < 0)||(now.tv_usec < 0)) { // next is in past?
	timeradd(&next,&timestep,&next);
	int index = rand() & 1;
	cmd[index][0] ^= 1;
	if(config.toggle)gecko_cmd_user_message_to_target(3,&cmd[index][0]);
	//if(rand() & 0xff) return;
	if(busy) return;
	gecko_cmd_gatt_read_characteristic_value(config.connection,26);
	busy = 1;
      }
    }
    return;
  }

  // Do not handle any events until system is booted up properly.
  if ((BGLIB_MSG_ID(evt->header) != gecko_evt_system_boot_id)
      && !appBooted) {
#if defined(DEBUG)
    printf("Event: 0x%04x\n", BGLIB_MSG_ID(evt->header));
#endif
    millisleep(50);
    return;
  }

  /* Handle events */
#ifdef DUMP
  switch (BGLIB_MSG_ID(evt->header)) {
  default:
    dump_event(evt);
  }
#endif
  switch (BGLIB_MSG_ID(evt->header)) {
  case gecko_evt_system_boot_id: /*********************************************************************************** system_boot **/
#define ED evt->data.evt_system_boot
    appBooted = true;
    gettimeofday(&now,NULL);
    timeradd(&now,&timestep,&next);
    busy = 1;
    if(config.connect) gecko_cmd_le_gap_connect(config.remote,le_gap_address_type_public,le_gap_phy_1m);
    break;
#undef ED

  case gecko_evt_gatt_procedure_completed_id: /*************************************************** gatt_procedure_completed **/
#define ED evt->data.evt_gatt_procedure_completed
    if((0 != ED.result) || (0 == (rand() & 0xfff))) {
      gecko_cmd_le_connection_close(config.connection);
    } else {
      busy = 0;
    }
    break;
#undef ED

  case gecko_evt_le_connection_opened_id: /***************************************************************** le_connection_opened **/
#define ED evt->data.evt_le_connection_opened
    config.connection = ED.connection;
    break;
#undef ED

  case gecko_evt_gatt_mtu_exchanged_id: /********************************************************************* gatt_mtu_exchanged **/
#define ED evt->data.evt_gatt_mtu_exchanged
    config.mtu = ED.mtu;
    busy = 0;
    break;
#undef ED

  case gecko_evt_le_connection_closed_id: /***************************************************************** le_connection_closed **/
#define ED evt->data.evt_le_connection_closed
    gecko_cmd_system_reset(0);
    break;
#undef ED

  default:
    break;
  }
}
