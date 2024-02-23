#ifndef MAIN_CMD_CAN_H
#define MAIN_CMD_CAN_H

#include "can.h"

static const twai_general_config_t default_g_config = {
    .mode = TWAI_MODE_NORMAL,
    .tx_io = CONFIG_CAN_TX_GPIO,
    .rx_io = CONFIG_CAN_RX_GPIO,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 10,
    .rx_queue_len = 10,
    .alerts_enabled = TWAI_ALERT_ERR_ACTIVE | TWAI_ALERT_BUS_RECOVERED | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF,
    .clkout_divider = 0,
    .intr_flags = ESP_INTR_FLAG_IRAM,
};

// functions

void register_can_commands(void);

#endif // MAIN_CMD_CAN_H
