#ifndef MAIN_CAN_H
#define MAIN_CAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

typedef enum {
    CAN_NOT_INSTALLED = 0,
    CAN_STOPPED,
    CAN_ERROR_ACTIVE,
    CAN_ERROR_PASSIVE,
    CAN_BUS_OFF,
    CAN_RECOVERING,
    CAN_STATE_COUNT,
} can_state_e;

typedef struct {
    uint32_t code;
    uint32_t mask;
} smart_filt_element_t;

// "Smart" filters: the widest acceptance filter common to all entries is programmed into the
// hardware; if that still lets through more than the entries themselves, every received frame
// is additionally matched against the list in software (see can_task).
typedef struct {
    smart_filt_element_t items[CONFIG_CAN_MAX_SMARTFILTERS_NUM];
    size_t count;
    bool configured;    // cansmartfilter has been run; `canup -f` will use it
    bool needs_sw;      // the hardware filter alone is wider than the list
    bool sw_filtering;  // software matching is active right now (set by `canup -f`)
} adv_filt_t;

typedef struct {
    can_state_e state;
    uint32_t msgs_to_tx;        // messages queued for transmission
    uint32_t msgs_to_rx;        // messages in the RX queue waiting to be read
    uint32_t tx_error_counter;
    uint32_t rx_error_counter;
    uint32_t tx_failed_count;   // messages that failed transmission
    uint32_t rx_missed_count;   // messages lost because the RX queue was full
    uint32_t rx_overrun_count;  // messages lost to an RX FIFO overrun
    uint32_t arb_lost_count;
    uint32_t bus_error_count;
} can_status_t;

// Guards every call into the TWAI driver and all access to adv_filters. The console commands
// take it while (un)installing the driver; can_task takes it while receiving.
extern SemaphoreHandle_t can_mutex;

// Snapshot refreshed by can_task a few times per second. For display only (prompt, canstats);
// anything that makes a decision must query the driver under can_mutex instead.
extern volatile can_status_t curr_can_state;

extern bool auto_recovery;
extern bool is_error_passive;
extern adv_filt_t adv_filters;

// Creates can_mutex. Must be called before any task that uses CAN is started.
void can_init(void);

void can_task(void *arg);

// Reads the driver status. Call with can_mutex held.
can_status_t can_read_status(void);

const char *can_state_str(can_state_e state);

// Formats a frame as "<prefix>can frame: ID: ... dlc: N data: ..." into out (NUL-terminated,
// truncated to out_size). 80 bytes is enough for any frame.
void can_msg_to_str(const twai_message_t *msg, const char *prefix, char *out, size_t out_size);

#endif // MAIN_CAN_H
