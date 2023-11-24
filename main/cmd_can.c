#include "cmd_can.h"
#include "driver/twai.h"
#include "freertos/projdefs.h"
#include "hal/twai_types.h"
#include "string.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "xvprintf.h"
#include "can.h"

static void register_send_can_frame(void);

void register_can_commands(void) {
    register_send_can_frame();
    
}

static struct {
    struct arg_str *message;
    struct arg_end *end;
} cansend_args;

static int send_can_frame(int argc, char **argv) {
    const char *delim = "#";
    int tmp_can_id = 0;
    int tmp_id = 0;
    char printf_str[30];
    char can_msg_str_buf[40];
    int nerrors = arg_parse(argc, argv, (void **) &cansend_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cansend_args.end, argv[0]);
        return 1;
    }
    const char* can_msg_ptr = cansend_args.message->sval[0];

    strlcpy(can_msg_str_buf, can_msg_ptr, sizeof(can_msg_str_buf));
    printf("%s\n", can_msg_str_buf);

    twai_message_t msg = {.extd = 1};
    msg.data_length_code = 0;
    msg.identifier = 0xFF << 8; 
    twai_transmit(&msg, pdMS_TO_TICKS(1000));
    can_msg_to_str(&msg, printf_str);
    printf("sent %s\n", printf_str);
    return 0;
}

static void register_send_can_frame(void) {
    
    cansend_args.message = arg_str1(NULL, NULL, "ID#data", "Message to send, ID and data bytes, all in hex. # is the delimiter.");
    cansend_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "cansend",
        .help = "Send a can message to the bus, example: cansend 00008C03#02",
        .hint = NULL,
        .func = &send_can_frame,
        .argtable = &cansend_args,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}
