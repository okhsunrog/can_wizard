#include "can.h"
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "hal/twai_types.h"
#include "sdkconfig.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "freertos/ringbuf.h"
#include "xvprintf.h"

static const char* LOG_TAG = "can";

bool is_error_passive = false;

SemaphoreHandle_t can_mutex;
can_status_t curr_can_state = { 0 };

can_state_e get_can_state() {
    twai_status_info_t status;
    esp_err_t res = twai_get_status_info(&status);
    if (res != ESP_OK) return CAN_NOT_INSTALLED;
    switch (status.state) {
        case TWAI_STATE_STOPPED:
            return CAN_STOPPED;
        case TWAI_STATE_BUS_OFF:
            return CAN_BUF_OFF;
        case TWAI_STATE_RECOVERING:
            return CAN_RECOVERING;
        default:
            if (is_error_passive) return CAN_ERROR_PASSIVE;
            else return CAN_ERROR_ACTIVE;
    }
}

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

void can_msg_to_str(twai_message_t *can_msg, char *out_str) {
    char byte_str[3];
    out_str[0] = '\0';
    sprintf(out_str, "can frame: ID: %08X dlc: %d ", (int) can_msg->identifier, can_msg->data_length_code);
    if (can_msg->data_length_code == 0) {
        strcat(out_str, "(no data)");
    } else {
        strcat(out_str, "data: ");
        for (int i = 0; i < can_msg->data_length_code; i++) {
            sprintf(byte_str, "%02X", can_msg->data[i]);
            strcat(out_str, byte_str);
        }
    }
}

void can_task(void* arg) {
    can_mutex = xSemaphoreCreateMutex();
    twai_message_t rx_msg;
    char data_bytes_str[50];
    // ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) { // A Task shall never return or exit.
        // esp_task_wdt_reset();
        can_bus_off_check();
        if (twai_receive(&rx_msg, pdMS_TO_TICKS(1000)) != ESP_OK) continue;
        // TODO: add software filtering
        // if ((((rx_msg.identifier >> 8) & 0xFF) != CONFIG_DEVICE_ID) && (((rx_msg.identifier >> 8) & 0xFF) != 0xFF)) continue;
        // ESP_LOGI(LOG_TAG, "received can frame: %" PRIu32, rx_msg.identifier);
        can_msg_to_str(&rx_msg, data_bytes_str); 
        xprintf(LOG_COLOR(LOG_COLOR_BLUE) "recv %s\n" LOG_RESET_COLOR, data_bytes_str);
    }
}
