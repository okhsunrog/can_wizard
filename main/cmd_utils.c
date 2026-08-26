#include "cmd_utils.h"
#include <stdio.h>
#include <unistd.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "linenoise/linenoise.h"
#include "fs.h"
#include "xvprintf.h"

static void register_timestamp(void);
static void register_clrhistory(void);

void register_utils_commands(void) {
    register_timestamp();
    register_clrhistory();
}

static struct {
    struct arg_lit *disable;
    struct arg_end *end;
} timestamp_args;

static int timestamp(int argc, char **argv) {
    const int nerrors = arg_parse(argc, argv, (void **) &timestamp_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, timestamp_args.end, argv[0]);
        return 1;
    }
    if (timestamp_args.disable->count) {
        print_w_clr_time("Disabled timestamps!", CLR_PURPLE, true);
        timestamp_enabled = false;
    } else {
        print_w_clr_time("Enabled timestamps!", CLR_PURPLE, true);
        timestamp_enabled = true;
    }
    return 0;
}

static void register_timestamp(void) {

    timestamp_args.disable = arg_lit0("d", "disable", "Set to disable timestamps.");
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

static int clrhistory(int argc, char **argv) {
    linenoiseHistoryFree();
    unlink(HISTORY_PATH);
    return 0;
}

static void register_clrhistory(void) {
    const esp_console_cmd_t cmd = {
        .command = "clrhistory",
        .help = "Clear command history",
        .hint = NULL,
        .func = &clrhistory,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

