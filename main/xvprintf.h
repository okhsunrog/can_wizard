#ifndef MAIN_XVPRINTF_H
#define MAIN_XVPRINTF_H

#include "freertos/ringbuf.h"

typedef int (*print_func)(const char *fmt, ...);

extern RingbufHandle_t uart_tx_ringbuf;
extern bool timestamp_enabled;

void init_tx_ringbuf();
int vxprintf(const char *fmt, va_list args);
int xprintf(const char *fmt, ...);
int print_w_clr_time(char *msg, char *color, bool use_printf);

// functions

#endif // MAIN_XVPRINTF_H
