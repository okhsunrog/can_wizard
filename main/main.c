#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "can.h"
#include "fs.h"
#include "console.h"
#include "xvprintf.h"

void app_main(void) {
    can_init();
    init_tx_ringbuf();
    xTaskCreate(can_task, "can task", 4096, NULL, CONFIG_CAN_TASK_PRIORITY, NULL);
    initialize_filesystem();
    initialize_console();
    xTaskCreate(console_task_tx, "console tsk tx", 4096, NULL, CONFIG_CONSOLE_TX_PRIORITY, NULL);
    xTaskCreate(console_task_interactive, "console tsk int", 4096, NULL, CONFIG_CONSOLE_INT_PRIORITY, NULL);
}
