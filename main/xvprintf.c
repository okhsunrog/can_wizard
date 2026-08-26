#include "xvprintf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#define TX_RINGBUF_SIZE 2200
#define LOG_LINE_MAX    300

RingbufHandle_t uart_tx_ringbuf = NULL;
bool timestamp_enabled = false;

void init_tx_ringbuf(void) {
    uart_tx_ringbuf = xRingbufferCreate(TX_RINGBUF_SIZE, RINGBUF_TYPE_NOSPLIT);
    assert(uart_tx_ringbuf != NULL);
}

// Installed with esp_log_set_vprintf(), so it is called for every ESP_LOG* line.
// Do NOT use ESP_LOG* in here - it would recurse until the stack overflows.
int vxprintf(const char *fmt, va_list args) {
    char msg[LOG_LINE_MAX];
    int len = vsnprintf(msg, sizeof(msg), fmt, args);
    if (len < 0) return 0;
    // vsnprintf returns the length the text *would* have had; clamp to what was written.
    if ((size_t) len >= sizeof(msg)) len = sizeof(msg) - 1;
    // The item is sent including its NUL terminator.
    if (xRingbufferSend(uart_tx_ringbuf, msg, (size_t) len + 1, pdMS_TO_TICKS(200)) != pdTRUE) {
        return 0;  // console TX task is not keeping up; the line is dropped
    }
    return len;
}

int xprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int ret = vxprintf(fmt, args);
    va_end(args);
    return ret;
}

int print_w_clr_time(const char *msg, const char *color, bool use_printf) {
    char timestamp[24] = "";
    if (timestamp_enabled) {
        snprintf(timestamp, sizeof(timestamp), "[%s] ", esp_log_system_timestamp());
    }
    int (*pr)(const char *fmt, ...) = use_printf ? printf : xprintf;
    if (color != NULL) {
        return pr(ANSI_COLOR("%s") "%s%s" ANSI_RESET "\n", color, timestamp, msg);
    }
    return pr("%s%s\n", timestamp, msg);
}

int print_w_clr_time_f(const char *color, bool use_printf, const char *fmt, ...) {
    char msg[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    return print_w_clr_time(msg, color, use_printf);
}
