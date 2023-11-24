//
// Created by okhsunrog on 8/17/23.
//

#ifndef MAIN_CAN_H
#define MAIN_CAN_H

#include "driver/twai.h"
#include "sdkconfig.h"

// functions

void can_init();
void can_stop();
void calculate_hw_can_filter(uint32_t device_id, twai_filter_config_t* filter, bool ota_mode);
void can_bus_off_check();
void can_task(void* arg);
void can_msg_to_str(twai_message_t *can_msg, char *out_str);

#endif // MAIN_CAN_H
