#ifndef MAIN_CAN_H
#define MAIN_CAN_H

#include "driver/twai.h"
#include "hal/twai_types.h"
#include "sdkconfig.h"
#include "freertos/semphr.h"
#include <stdint.h>

typedef struct {
  char status[30];
  int tec;
  int rec;
  int color;
  bool extd;
} can_prompt_t;

typedef enum {
  CAN_NOT_INSTALLED = 0,
  CAN_STOPPED = 1,
  CAN_ERROR_ACTIVE = 2,
  CAN_ERROR_PASSIVE = 3,
  CAN_BUF_OFF = 4,
  CAN_RECOVERING = 5,
} can_state_e;

typedef struct {
  can_state_e state;
  uint32_t msgs_to_tx;            /**< Number of messages queued for transmission or awaiting transmission completion */
  uint32_t msgs_to_rx;            /**< Number of messages in RX queue waiting to be read */
  uint32_t tx_error_counter;      /**< Current value of Transmit Error Counter */
  uint32_t rx_error_counter;      /**< Current value of Receive Error Counter */
  uint32_t tx_failed_count;       /**< Number of messages that failed transmissions */
  uint32_t rx_missed_count;       /**< Number of messages that were lost due to a full RX queue (or errata workaround if enabled) */
  uint32_t rx_overrun_count;      /**< Number of messages that were lost due to a RX FIFO overrun */
  uint32_t arb_lost_count;        /**< Number of instances arbitration was lost */
  uint32_t bus_error_count;       /**< Number of instances a bus error has occurred */
} can_status_t;

extern SemaphoreHandle_t can_mutex;
extern volatile can_status_t curr_can_state;

// functions

void can_task(void* arg);
void can_msg_to_str(twai_message_t *can_msg, char *out_str);

#endif // MAIN_CAN_H
