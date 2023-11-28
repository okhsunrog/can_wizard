#include "cmd_utils.h"
#include "esp_log.h"
#include "inttypes.h"
#include "freertos/projdefs.h"
#include "string.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "xvprintf.h"
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>

static void register_timestamp(void);

void register_utils_commands(void) {
    register_timestamp();
}

static struct {
    struct arg_str *enable;
    struct arg_end *end;
} timestamp_args;

static int timestamp(int argc, char **argv) {
    bool en = true;
    int nerrors = arg_parse(argc, argv, (void **) &timestamp_args);
    if (nerrors == 0) {
        const char *arg_str_ptr = timestamp_args.enable->sval[0];
        if (arg_str_ptr[0] == 'd') en = false;
    }
    if (en) {
        print_w_clr_time("Enabled timestamps!", LOG_COLOR_PURPLE, true);
        timestamp_enabled = true;
    } else {
        print_w_clr_time("Disabled timestamps!", LOG_COLOR_PURPLE, true);
        timestamp_enabled = false;
    }
    return 0;
}

static void register_timestamp(void) {

    timestamp_args.enable = arg_str1(NULL, NULL, "<enable|disable>", "Enable by default, can be abbreviations.");
    timestamp_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "timestamp",
        .help = "Enable or disable timestamp in messages.",
        .hint = NULL,
        .func = &timestamp,
        .argtable = &timestamp_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
