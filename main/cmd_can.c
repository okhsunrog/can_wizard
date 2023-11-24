#include "cmd_can.h"
#include "driver/twai.h"
#include "freertos/projdefs.h"
#include "hal/twai_types.h"
#include "string.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "xvprintf.h"
#include "can.h"
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>

static void register_send_can_frame(void);

void register_can_commands(void) {
    register_send_can_frame();
    
}

static struct {
    struct arg_str *message;
    struct arg_end *end;
} cansend_args;

static int send_can_frame(int argc, char **argv) {
    twai_message_t msg = {.extd = 1};
    char printf_str[50];
    int nerrors = arg_parse(argc, argv, (void **) &cansend_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cansend_args.end, argv[0]);
        return 1;
    }
    const char *can_msg_ptr = cansend_args.message->sval[0];
    char *can_msg_str_buf = strdup(can_msg_ptr);
    char *id_substr = strtok(can_msg_str_buf, "#");
    char *data_substr = strtok(NULL, "#");
    if ((id_substr == NULL) || (strtok(NULL, "#") != NULL)) goto invalid_args;
    int id_l = strlen(id_substr);
    int dt_l = data_substr == NULL ? 0 : strlen(data_substr);
    if ((id_l > 8) || (dt_l > 16) || (id_l % 2) || (dt_l % 2)) goto invalid_args;
    for (int i = 0; i < id_l; i++) if(!isxdigit((int) id_substr[i])) goto invalid_args;
    for (int i = 0; i < dt_l; i++) if(!isxdigit((int) data_substr[i])) goto invalid_args;
    int msg_id;
    if (sscanf(id_substr, "%X", &msg_id) < 1) goto invalid_args;
    for (int i = 0; i < (dt_l / 2); i++) {
        char *byte_to_parse = malloc(3);
        strncpy(byte_to_parse, data_substr + i * 2, 2);
        int num;
        int res = sscanf(byte_to_parse, "%X", &num);
        free(byte_to_parse);
        if (res < 1) goto invalid_args;
        msg.data[i] = num;
    }
    msg.data_length_code = dt_l / 2;
    msg.identifier = msg_id;
    twai_transmit(&msg, pdMS_TO_TICKS(1000));
    can_msg_to_str(&msg, printf_str);
    printf("sent %s\n", printf_str);
    // free(can_msg_str_buf);
    return 0;
invalid_args:
    printf("Invalid arguments!");
    free(can_msg_str_buf);
    return 1;
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
