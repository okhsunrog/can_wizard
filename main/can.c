#include "can.h"
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "freertos/task.h"
#include "xvprintf.h"

// Longest wait inside twai_receive() while holding can_mutex. Bounds how long a console
// command (canup/candown/...) can be blocked on a quiet bus.
#define RX_WAIT_TICKS   pdMS_TO_TICKS(200)
// Frames drained per mutex acquisition. Bounds how long a console command can be blocked
// on a busy bus.
#define RX_BURST_MAX    32
// Poll period while the driver is not installed.
#define IDLE_POLL_TICKS pdMS_TO_TICKS(200)

bool is_error_passive = false;
bool auto_recovery = false;
adv_filt_t adv_filters = { 0 };
SemaphoreHandle_t can_mutex = NULL;
volatile can_status_t curr_can_state = { 0 };

static const char *const can_state_names[] = {
    [CAN_NOT_INSTALLED] = "not installed",
    [CAN_STOPPED] = "stopped",
    [CAN_ERROR_ACTIVE] = "error active",
    [CAN_ERROR_PASSIVE] = "error passive",
    [CAN_BUS_OFF] = "bus off",
    [CAN_RECOVERING] = "recovering",
};
_Static_assert(sizeof(can_state_names) / sizeof(can_state_names[0]) == CAN_STATE_COUNT,
               "can_state_names must cover every can_state_e value");

const char *can_state_str(can_state_e state) {
    return (state >= 0 && state < CAN_STATE_COUNT) ? can_state_names[state] : "unknown";
}

void can_init(void) {
    can_mutex = xSemaphoreCreateMutex();
    assert(can_mutex != NULL);
}

can_status_t can_read_status(void) {
    can_status_t result = { 0 };
    twai_status_info_t status = { 0 };
    if (twai_get_status_info(&status) != ESP_OK) {
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
            result.state = CAN_BUS_OFF;
            break;
        case TWAI_STATE_RECOVERING:
            result.state = CAN_RECOVERING;
            break;
        default:
            // The driver reports RUNNING for both; error-passive is tracked from the alerts.
            result.state = is_error_passive ? CAN_ERROR_PASSIVE : CAN_ERROR_ACTIVE;
            break;
    }
    return result;
}

// snprintf that appends at *pos and never runs past out_size.
static void append(char *out, size_t out_size, size_t *pos, const char *fmt, ...) {
    if (*pos >= out_size) return;
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(out + *pos, out_size - *pos, fmt, args);
    va_end(args);
    if (n < 0) return;
    *pos = (*pos + (size_t) n >= out_size) ? out_size : *pos + (size_t) n;
}

void can_msg_to_str(const twai_message_t *msg, const char *prefix, char *out, size_t out_size) {
    if (out_size == 0) return;
    out[0] = '\0';
    size_t pos = 0;
    // For received frames the driver passes the raw DLC field through (0..15) but never fills
    // more than TWAI_FRAME_MAX_DLC data bytes, so print the DLC as-is but clamp the data loop.
    const unsigned dlc = msg->data_length_code;
    const unsigned data_len = dlc > TWAI_FRAME_MAX_DLC ? TWAI_FRAME_MAX_DLC : dlc;
    if (msg->rtr) {
        append(out, out_size, &pos, "%sremote frame: ID: %08" PRIX32 " dlc: %u", prefix, msg->identifier, dlc);
        return;
    }
    append(out, out_size, &pos, "%scan frame: ID: %08" PRIX32 " dlc: %u ", prefix, msg->identifier, dlc);
    if (data_len == 0) {
        append(out, out_size, &pos, "(no data)");
        return;
    }
    append(out, out_size, &pos, "data: ");
    for (unsigned i = 0; i < data_len; i++) {
        append(out, out_size, &pos, "%02X", msg->data[i]);
    }
}

static bool matches_filters(const twai_message_t *msg) {
    for (size_t i = 0; i < adv_filters.count; i++) {
        const smart_filt_element_t *f = &adv_filters.items[i];
        if ((msg->identifier & f->mask) == (f->code & f->mask)) return true;
    }
    return false;
}

// Call with can_mutex held.
static void handle_alerts(void) {
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) != ESP_OK) return;
    if (alerts & TWAI_ALERT_ERR_ACTIVE) is_error_passive = false;
    if (alerts & TWAI_ALERT_ERR_PASS) is_error_passive = true;
    if (alerts & TWAI_ALERT_BUS_ERROR) {
        print_w_clr_time("CAN error!", CLR_RED, false);
    }
    if (alerts & TWAI_ALERT_BUS_OFF) {
        print_w_clr_time("CAN went bus-off!", CLR_RED, false);
        if (auto_recovery) {
            print_w_clr_time("Initiating auto-recovery...", CLR_GREEN, false);
            const esp_err_t err = twai_initiate_recovery();
            if (err != ESP_OK) {
                print_w_clr_time_f(CLR_RED, false, "Couldn't start recovery: %s", esp_err_to_name(err));
            }
        }
    }
    if (alerts & TWAI_ALERT_BUS_RECOVERED) {
        print_w_clr_time("CAN recovered!", CLR_BLUE, false);
        if (auto_recovery) {
            const esp_err_t err = twai_start();
            if (err == ESP_OK) {
                is_error_passive = false;
                print_w_clr_time("CAN started", CLR_GREEN, false);
            } else {
                print_w_clr_time_f(CLR_RED, false, "Couldn't start CAN after recovery: %s", esp_err_to_name(err));
            }
        }
    }
}

void can_task(void *arg) {
    (void) arg;
    twai_message_t rx_msg;
    char line[80];
    for (;;) {
        // Every driver call happens under can_mutex so that a concurrent `candown`
        // (twai_driver_uninstall) cannot free driver state we are blocked on.
        if (xSemaphoreTake(can_mutex, 0) != pdTRUE) {
            vTaskDelay(IDLE_POLL_TICKS);
            continue;
        }
        handle_alerts();
        curr_can_state = can_read_status();

        esp_err_t ret = twai_receive(&rx_msg, RX_WAIT_TICKS);
        int drained = 0;
        while (ret == ESP_OK) {
            if (!adv_filters.sw_filtering || matches_filters(&rx_msg)) {
                can_msg_to_str(&rx_msg, "recv ", line, sizeof(line));
                print_w_clr_time(line, CLR_BLUE, false);
            }
            if (++drained >= RX_BURST_MAX) break;
            ret = twai_receive(&rx_msg, 0);
        }
        xSemaphoreGive(can_mutex);

        // Giving a mutex does not hand it to a lower-priority waiter, so yield for a tick to
        // let a pending console command in. Poll slowly while the driver is not installed.
        vTaskDelay(ret == ESP_ERR_INVALID_STATE ? IDLE_POLL_TICKS : 1);
    }
}
