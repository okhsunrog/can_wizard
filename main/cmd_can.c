#include "cmd_can.h"
#include "inttypes.h"
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
static void register_send_can_frame(void);
static void register_canup(void);
static void register_candown(void);
static void register_canstats(void);
static void register_canstart(void);
static void register_canrecover(void);

void register_can_commands(void) {
    register_send_can_frame();
    
}

static struct {
    struct arg_str *message;
    struct arg_end *end;
} cansend_args;

static struct {
    struct arg_str *speed;
    struct arg_str *filters;
    struct arg_str *autorecover;
    struct arg_str *mode;
    struct arg_end *end;
} canup_args;

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


static int canrecover(int argc, char **argv) {
    return 0;
}

static const char* can_states_str[] = {
    "not installed",
    "stopped",
    "error active",
    "error passive",
    "bus off",
    "recovering"
};

static int canstats(int argc, char **argv) {
    can_state_e canstate = get_can_state();
    if (canstate == CAN_NOT_INSTALLED) {
        printf("CAN driver is not installed!");
        return 0;
    } else {
        twai_status_info_t status;
        twai_get_status_info(&status);
        const char *state_str = can_states_str[canstate];
        printf("status: %s\n", state_str);
        printf("TX Err Counter: %" PRIu32 "\n", status.tx_error_counter);
        printf("RX Err Counter: %" PRIu32 "\n", status.rx_error_counter);
        printf("Failed transmit: %" PRIu32 "\n", status.tx_failed_count);
        printf("Arbitration lost times: %" PRIu32 "\n", status.arb_lost_count);
        printf("Bus-off count: %" PRIu32 "\n", status.bus_error_count);
    }
    return 0;
}

static int canup(int argc, char **argv) {
    // Install CAN driver
    // TODO: add CAN filtering
    static twai_filter_config_t f_config = {.acceptance_code = 0, .acceptance_mask = 0xFFFFFFFF, .single_filter = true};
    esp_err_t res = twai_driver_install(&g_config, &t_config, &f_config);
    if (res != ESP_OK) {
        printf("Couldn't install CAN driver! Rebooting...\n");
        esp_restart();
    }
    printf("CAN driver installed\n");
    // Start CAN driver
    ESP_ERROR_CHECK(twai_start());
    printf("CAN driver started\n");
    return 0;
}

static int canstart(int argc, char **argv) {
    // Start CAN driver
    ESP_ERROR_CHECK(twai_start());
    printf("CAN driver started\n");
    return 0;
}

static int candown(int argc, char **argv) {
    ESP_ERROR_CHECK(twai_stop());
    ESP_ERROR_CHECK(twai_driver_uninstall());
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

static void register_canup(void) {
    canup_args.speed = arg_str1(NULL, NULL, "<25|50|100|125|250|500|800|1000>", "CAN bus speed, in kbits.");
    canup_args.mode = arg_str0("m", "mode", "<normal|no_ack|listen_only", "Set CAN mode. Normal, No Ack (for self-testing) or Listen Only (to prevent transmitting, for monitoring).");
    canup_args.filters = arg_str0("f", "filters", "<filters>", "CAN filters to receive only selected frames.");
    canup_args.autorecover = arg_str0("r", "auto-recover", "<1|0>", "Set 1 to enable auto-recovery of CAN bus if case of bus-off event.");
    canup_args.end = arg_end(2);
    const esp_console_cmd_t cmd = {
        .command = "canup",
        .help = "Install can drivers and start can interface. Used right after board start or during runtime for changing CAN configuration.",
        .hint = NULL,
        .func = &canup,
        .argtable = &canup_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_candown(void) {
    const esp_console_cmd_t cmd = {
        .command = "candown",
        .help = "Stop CAN interface and uninstall CAN driver, for example, to install and start with different parameters/filters.",
        .hint = NULL,
        .func = &candown,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_canstats(void) {
    const esp_console_cmd_t cmd = {
        .command = "canstats",
        .help = "Print CAN statistics.",
        .hint = NULL,
        .func = &canstats,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_canstart(void) {
    const esp_console_cmd_t cmd = {
        .command = "canstart",
        .help = "Start CAN interface, used after buf recovery, otherwise see canup command.",
        .hint = NULL,
        .func = &canstart,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_canrecover(void) {
    const esp_console_cmd_t cmd = {
        .command = "canrecover",
        .help = "Recover CAN after buf-off. Used when auto-recovery is turned off.",
        .hint = NULL,
        .func = &canrecover,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
