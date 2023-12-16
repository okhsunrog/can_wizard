#include "can.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "hal/twai_types.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include "freertos/ringbuf.h"
#include "xvprintf.h"

bool is_error_passive = false;
bool auto_recovery = false;
adv_filt_t adv_filters = {
    .filters = NULL,
    .enabled = false,
    .sw_filtering = false,
};

SemaphoreHandle_t can_mutex;
volatile can_status_t curr_can_state = { 0 };

static can_status_t get_can_state() {
    can_status_t result = { 0 };
    twai_status_info_t status = {0};
    const esp_err_t res = twai_get_status_info(&status);
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

void can_msg_to_str(const twai_message_t *can_msg, char *start_str, char *out_str) {
    out_str[0] = '\0';
    sprintf(out_str, "%scan frame: ID: %08X dlc: %d ", start_str, (int) can_msg->identifier, can_msg->data_length_code);
    if (can_msg->data_length_code == 0) {
        strcat(out_str, "(no data)");
    } else {
        strcat(out_str, "data: ");
        for (int i = 0; i < can_msg->data_length_code; i++) {
            char byte_str[3];
            sprintf(byte_str, "%02X", can_msg->data[i]);
            strcat(out_str, byte_str);
        }
    }
}

bool matches_filters(const twai_message_t *msg) {
    const List *tmp_cursor = adv_filters.filters;
    while (tmp_cursor != NULL) {
        const smart_filt_element_t* curr_filter = tmp_cursor->data;
        if ((msg->identifier & curr_filter->mask) == curr_filter->filt) {
            return true;
        }
        tmp_cursor = tmp_cursor->next;
    }
    return false;
}

void can_task(void* arg) {
    static const TickType_t can_task_timeout = pdMS_TO_TICKS(200);
    uint32_t alerts = 0;
    esp_err_t ret = ESP_OK;
    can_mutex = xSemaphoreCreateMutex();
    twai_message_t rx_msg;
    for (;;) { // A Task shall never return or exit.
        if (twai_read_alerts(&alerts, 0) == ESP_OK) {
            if (alerts & TWAI_ALERT_ERR_ACTIVE) {
                is_error_passive = false;
            }
            if (alerts & TWAI_ALERT_ERR_PASS) {
                is_error_passive = true;
            }
            if (alerts & TWAI_ALERT_BUS_ERROR) {
                print_w_clr_time("CAN error!", LOG_COLOR_RED, false);
            }
            if (alerts & TWAI_ALERT_BUS_OFF) {
                print_w_clr_time("CAN went bus-off!", LOG_COLOR_RED, false);
                if (auto_recovery) {
                    print_w_clr_time("Initiating auto-recovery...", LOG_COLOR_GREEN, false);
                    twai_initiate_recovery();
                }
            }
            if (alerts & TWAI_ALERT_BUS_RECOVERED) {
                print_w_clr_time("CAN recovered!", LOG_COLOR_BLUE, false);
                if (auto_recovery) {
                    print_w_clr_time("Starting CAN...", LOG_COLOR_GREEN, false);
                    ESP_ERROR_CHECK(twai_start());
                    is_error_passive = false;
                }
            }
        }
        curr_can_state = get_can_state();
        const BaseType_t sem_res = xSemaphoreTake(can_mutex, 0);
        if (sem_res == pdTRUE) {
            while ((ret = twai_receive(&rx_msg, can_task_timeout)) == ESP_OK) {
                char data_bytes_str[70];
                if (adv_filters.sw_filtering) {
                    if (!matches_filters(&rx_msg)) continue;
                }
                can_msg_to_str(&rx_msg, "recv ", data_bytes_str); 
                print_w_clr_time(data_bytes_str, LOG_COLOR_BLUE, false);
            }
            xSemaphoreGive(can_mutex);
            vTaskDelay(1);
        }
        if (sem_res != pdTRUE || ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_NOT_SUPPORTED) {
            vTaskDelay(can_task_timeout);
        }
    }
}
