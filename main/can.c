//
// Created by okhsunrog on 8/17/23.
//

#include "can.h"
#include "esp_log.h"
#include "hal/twai_types.h"
#include "sdkconfig.h"
#include <stdio.h>

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

static const char* LOG_TAG = "can";

void can_init() {
    // Install CAN driver
    // calculate_hw_can_filter(CONFIG_DEVICE_ID, &f_config, false);
    static twai_filter_config_t f_config = {.acceptance_code = 0, .acceptance_mask = 0xFFFFFFFF, .single_filter = true};
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(LOG_TAG, "CAN driver installed");
    // Start CAN driver
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(LOG_TAG, "CAN driver started");
}

void can_stop() {
    // stop and uninstall twai driver to change hardware filters
    ESP_ERROR_CHECK(twai_stop());
    ESP_ERROR_CHECK(twai_driver_uninstall());
}

// filtering subset of CAN IDs with hardware filters, need more filtering in software
void calculate_hw_can_filter(uint32_t device_id, twai_filter_config_t* filter, bool ota_mode) {
    filter->single_filter = true;
    filter->acceptance_code = device_id << 11;
    if (ota_mode) {
        filter->acceptance_code += 0x100000 << 11;
        filter->acceptance_mask = ~((0x1000FF) << 11);
    } else {
        filter->acceptance_mask = ~((device_id | 0x1F0000) << 11);
    }
}

void can_bus_off_check() {
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) == ESP_OK) {
        if (alerts & TWAI_ALERT_BUS_OFF) {
            ESP_ERROR_CHECK(twai_initiate_recovery());
        }
        if (alerts & TWAI_ALERT_BUS_RECOVERED) {
            ESP_ERROR_CHECK(twai_start());
        }
    }
}

void can_task(void* arg) {
    twai_message_t rx_msg;
    // ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) { // A Task shall never return or exit.
        // esp_task_wdt_reset();
        can_bus_off_check();
        if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) != ESP_OK) continue;
        // TODO: add software filtering
        // if ((((rx_msg.identifier >> 8) & 0xFF) != CONFIG_DEVICE_ID) && (((rx_msg.identifier >> 8) & 0xFF) != 0xFF)) continue;
        ESP_LOGI(LOG_TAG, "received can package with ID: %" PRIu32, rx_msg.identifier);
        // printf("received can package with ID: %" PRIu32, rx_msg.identifier);
    }
}

