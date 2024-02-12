#include "xvprintf.h"
#include <stdarg.h>
#include <stdio.h>
#include "stdbool.h"
#include "esp_log.h"

RingbufHandle_t uart_tx_ringbuf;
bool timestamp_enabled = false;

void init_tx_ringbuf() {
    uart_tx_ringbuf = xRingbufferCreate(2200, RINGBUF_TYPE_NOSPLIT);
}

// This function will be called by the ESP log library every time ESP_LOG needs to be performed.
//      @important Do NOT use the ESP_LOG* macro's in this function ELSE recursive loop and stack overflow! So use printf() instead for debug messages.
int vxprintf(const char *fmt, va_list args) {
    char msg_to_send[300];
    const size_t str_len = vsnprintf(msg_to_send, 299, fmt, args);
    xRingbufferSend(uart_tx_ringbuf, msg_to_send, str_len + 1, pdMS_TO_TICKS(200));
    return str_len;
}

int xprintf(const char *fmt, ...) {
    va_list(args);
    va_start(args, fmt);
    return vxprintf(fmt, args);
}

int print_w_clr_time(char *msg, char *color, bool use_printf) {
    print_func pr_func;
    if (use_printf) pr_func = printf;
    else pr_func = xprintf;
    char timestamp[20];
    timestamp[0] = '\0';
    if (timestamp_enabled) {
        snprintf(timestamp, 19, "[%s] ", esp_log_system_timestamp());
    }
    if (color != NULL) {
        return(pr_func("\033[0;%sm%s%s\033[0m\n", color, timestamp, msg));
    } else {
        return(pr_func("%s%s\n", timestamp, msg));
    }
}
