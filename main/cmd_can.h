#ifndef MAIN_CMD_CAN_H
#define MAIN_CMD_CAN_H

#include "can.h"


static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
static const twai_general_config_t g_config = {
    .mode = TWAI_MODE_NORMAL,
    .tx_io = CONFIG_CAN_TX_GPIO,
    .rx_io = CONFIG_CAN_RX_GPIO,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 5,
    .rx_queue_len = 5,
    .alerts_enabled = TWAI_ALERT_AND_LOG | TWAI_ALERT_ALL,
    .clkout_divider = 0,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
};

// functions

void register_can_commands(void);

#endif // MAIN_CMD_CAN_H
