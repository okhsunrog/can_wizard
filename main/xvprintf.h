#ifndef MAIN_XVPRINTF_H
#define MAIN_XVPRINTF_H

#include "freertos/ringbuf.h"

extern RingbufHandle_t can_messages;

void init_tx_ringbuf();
int xvprintf(const char *fmt, va_list args);
int xprintf(const char *fmt, ...);

// functions

#endif // MAIN_XVPRINTF_H
