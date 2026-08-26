#ifndef MAIN_XVPRINTF_H
#define MAIN_XVPRINTF_H

#include <stdarg.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

// ANSI colour codes used by print_w_clr_time(). Deliberately independent of the
// esp_log LOG_COLOR_* macros, which only exist when CONFIG_LOG_COLORS is enabled.
#define CLR_RED    "31"
#define CLR_GREEN  "32"
#define CLR_YELLOW "33"
#define CLR_BLUE   "34"
#define CLR_PURPLE "35"
#define CLR_CYAN   "36"
#define ANSI_COLOR(code) "\033[0;" code "m"
#define ANSI_RESET       "\033[0m"

// Everything printed asynchronously (CAN frames, ESP_LOG output) goes through this ring
// buffer; the console TX task drains it and prints without corrupting the line being edited.
extern RingbufHandle_t uart_tx_ringbuf;
extern bool timestamp_enabled;

void init_tx_ringbuf(void);

// printf-like functions that write into uart_tx_ringbuf instead of stdout.
int vxprintf(const char *fmt, va_list args);
int xprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Print one line, optionally coloured (CLR_*) and prefixed with a timestamp.
// use_printf=true writes to stdout directly (only safe from a console command, which owns
// the terminal while it runs); false goes through the ring buffer (safe from any task).
int print_w_clr_time(const char *msg, const char *color, bool use_printf);
int print_w_clr_time_f(const char *color, bool use_printf, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#endif // MAIN_XVPRINTF_H
