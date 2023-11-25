#ifndef MAIN_CAN_H
#define MAIN_CAN_H

#include "driver/twai.h"
#include "sdkconfig.h"

typedef struct {
  char status[30];
  int tec;
  int rec;
  int color;
  bool extd;
} can_prompt_t;

typedef enum {
  CAN_NOT_INSTALLED = 0,
  CAN_STOPPED = 1,
  CAN_ERROR_ACTIVE = 2,
  CAN_ERROR_PASSIVE = 3,
  CAN_BUF_OFF = 4,
  CAN_RECOVERING = 5,
} can_state_e;


static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
static const twai_general_config_t g_config = {
    .mode = TWAI_MODE_NORMAL,
    .tx_io = CONFIG_CAN_TX_GPIO,
    .rx_io = CONFIG_CAN_RX_GPIO,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 5,
    .rx_queue_len = 5,
    .alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED,
    .clkout_divider = 0,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
};


// functions

void can_init();
void can_stop();
void calculate_hw_can_filter(uint32_t device_id, twai_filter_config_t* filter, bool ota_mode);
void can_bus_off_check();
void can_task(void* arg);
void can_msg_to_str(twai_message_t *can_msg, char *out_str);
can_state_e get_can_state();

#endif // MAIN_CAN_H
