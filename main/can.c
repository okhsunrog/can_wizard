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
volatile can_status_t curr_can_state = { 0 };

static can_status_t get_can_state() {
    can_status_t result;
    twai_status_info_t status = { 0 };
    esp_err_t res = twai_get_status_info(&status);
    if (res != ESP_OK) {
        result.state = CAN_NOT_INSTALLED;
        return result;
    }
    result.msgs_to_rx = status.msgs_to_rx;
    result.msgs_to_tx = status.msgs_to_tx;
    result.arb_lost_count = status.arb_lost_count;
    result.bus_error_count = status.bus_error_count;
    result.tx_error_counter = status.tx_error_counter;
    result.rx_error_counter = status.rx_error_counter;
    result.tx_failed_count = status.tx_failed_count;
    result.rx_missed_count = status.rx_missed_count;
    result.rx_overrun_count = status.rx_overrun_count;
    switch (status.state) {
        case TWAI_STATE_STOPPED:
            result.state = CAN_STOPPED;
            break;
        case TWAI_STATE_BUS_OFF:
            result.state = CAN_BUF_OFF;
            break;
        case TWAI_STATE_RECOVERING:
            result.state = CAN_RECOVERING;
            break;
        default:
            if (is_error_passive) result.state = CAN_ERROR_PASSIVE;
            else result.state = CAN_ERROR_ACTIVE;
            break;
    }
    return result;
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

// TODO: add software filtering
void can_task(void* arg) {
    can_mutex = xSemaphoreCreateMutex();
    twai_message_t rx_msg;
    char data_bytes_str[50];
    // ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) { // A Task shall never return or exit.
        // esp_task_wdt_reset();
        // can_bus_off_check();
        curr_can_state = get_can_state();
        xSemaphoreTake(can_mutex, pdMS_TO_TICKS(200));
        while(twai_receive(&rx_msg, 0) == ESP_OK) {
            can_msg_to_str(&rx_msg, data_bytes_str); 
            xprintf(LOG_COLOR(LOG_COLOR_BLUE) "recv %s\n" LOG_RESET_COLOR, data_bytes_str);
        }
        xSemaphoreGive(can_mutex);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
