#include "cmd_can.h"
#include "driver/twai.h"
#include "freertos/projdefs.h"
#include "hal/twai_types.h"
#include "string.h"
#include "esp_console.h"
#include "xvprintf.h"
#include "can.h"

static void register_send_can_frame(void);

void register_can_commands(void) {
    register_send_can_frame();
    
}

static int send_can_frame(int argc, char **argv) {
    char data_bytes_str[30];
    twai_message_t msg = {.extd = 1};
    msg.data_length_code = 0;
    msg.identifier = 0xFF << 8; 
    twai_transmit(&msg, pdMS_TO_TICKS(1000));
    can_msg_to_str(&msg, data_bytes_str);
    printf("sent %s\n", data_bytes_str);
    return 0;
}

static void register_send_can_frame(void) {
    const esp_console_cmd_t cmd = {
        .command = "cansend",
        .help = "Send a can message to the bus",
        .hint = NULL,
        .func = &send_can_frame,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}
