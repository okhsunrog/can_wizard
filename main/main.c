#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "can.h"
#include "console.h"
#include "fs.h"
#include "xvprintf.h"

void app_main(void) {
    // Shared objects first: every task below relies on them existing.
    init_tx_ringbuf();
    can_init();
    initialize_filesystem();
    initialize_console();

    xTaskCreate(can_task, "can task", 4800, NULL, CONFIG_CAN_TASK_PRIORITY, NULL);
    xTaskCreate(console_task_interactive, "console tsk int", 8000, NULL, CONFIG_CONSOLE_INT_PRIORITY, NULL);
}
