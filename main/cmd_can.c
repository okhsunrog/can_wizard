#include "cmd_can.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "can.h"
#include "sdkconfig.h"
#include "xvprintf.h"

#define CAN_STD_ID_MAX 0x7FFU
#define CAN_EXT_ID_MAX 0x1FFFFFFFU

static const twai_general_config_t default_g_config = {
    .mode = TWAI_MODE_NORMAL,
    .tx_io = CONFIG_CAN_TX_GPIO,
    .rx_io = CONFIG_CAN_RX_GPIO,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 10,
    .rx_queue_len = 32,
    .alerts_enabled = TWAI_ALERT_ERR_ACTIVE | TWAI_ALERT_BUS_RECOVERED | TWAI_ALERT_BUS_ERROR |
                      TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF,
    .clkout_divider = 0,
    .intr_flags = ESP_INTR_FLAG_IRAM,
};

// Hardware acceptance filter applied by `canup -f`; written by canfilter and cansmartfilter.
static twai_filter_config_t hw_filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

/* ------------------------------------------------------------------------- helpers */

static int invalid_args(void) {
    print_w_clr_time("Invalid arguments!", CLR_RED, true);
    return 1;
}

// Parses 1..8 hex digits (no prefix) from s[0..len). Returns false on empty/too long/non-hex.
static bool parse_hex_u32(const char *s, size_t len, uint32_t *out) {
    if (len == 0 || len > 8) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char) s[i];
        if (!isxdigit(c)) return false;
        v = (v << 4) | (uint32_t) (isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
    }
    *out = v;
    return true;
}

// Splits "left#right" without modifying the string. Returns false unless there is exactly
// one '#' with something on both sides.
static bool split_hash(const char *s, const char **left, size_t *left_len, const char **right, size_t *right_len) {
    const char *hash = strchr(s, '#');
    if (hash == NULL || hash == s || strchr(hash + 1, '#') != NULL) return false;
    *left = s;
    *left_len = (size_t) (hash - s);
    *right = hash + 1;
    *right_len = strlen(hash + 1);
    return *right_len > 0;
}

/* ------------------------------------------------------------------------- cansend */

static struct {
    struct arg_str *message;
    struct arg_end *end;
} cansend_args;

static int send_can_frame(int argc, char **argv) {
    const int nerrors = arg_parse(argc, argv, (void **) &cansend_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cansend_args.end, argv[0]);
        return 1;
    }
    // Format: ID#DATA, ID#r or ID#rN. Everything hex. 3 ID digits = standard frame,
    // 4..8 digits = extended frame (same convention as SocketCAN's cansend).
    const char *arg = cansend_args.message->sval[0];
    const char *hash = strchr(arg, '#');
    if (hash == NULL || strchr(hash + 1, '#') != NULL) return invalid_args();
    const size_t id_len = (size_t) (hash - arg);
    const char *data = hash + 1;
    const size_t data_len = strlen(data);

    twai_message_t msg = { 0 };
    uint32_t id;
    if (!parse_hex_u32(arg, id_len, &id)) return invalid_args();
    msg.extd = id_len > 3;
    if (id > (msg.extd ? CAN_EXT_ID_MAX : CAN_STD_ID_MAX)) return invalid_args();
    msg.identifier = id;

    if (data_len >= 1 && (data[0] == 'r' || data[0] == 'R')) {
        msg.rtr = 1;
        if (data_len == 1) {
            msg.data_length_code = 0;
        } else if (data_len == 2 && data[1] >= '0' && data[1] <= '8') {
            msg.data_length_code = (uint8_t) (data[1] - '0');
        } else {
            return invalid_args();
        }
    } else {
        if (data_len > 2 * TWAI_FRAME_MAX_DLC || data_len % 2 != 0) return invalid_args();
        for (size_t i = 0; i < data_len / 2; i++) {
            uint32_t byte;
            if (!parse_hex_u32(data + 2 * i, 2, &byte)) return invalid_args();
            msg.data[i] = (uint8_t) byte;
        }
        msg.data_length_code = (uint8_t) (data_len / 2);
    }

    const esp_err_t res = twai_transmit(&msg, pdMS_TO_TICKS(1000));
    switch (res) {
        case ESP_OK: {
            char line[80];
            can_msg_to_str(&msg, "sent ", line, sizeof(line));
            print_w_clr_time(line, NULL, true);
            return 0;
        }
        case ESP_ERR_TIMEOUT:
            print_w_clr_time("Timeout!", CLR_RED, true);
            break;
        case ESP_ERR_NOT_SUPPORTED:
            print_w_clr_time("Can't send in Listen-Only mode!", CLR_RED, true);
            break;
        case ESP_ERR_INVALID_STATE:
            print_w_clr_time("CAN driver is not running!", CLR_RED, true);
            break;
        default:
            print_w_clr_time_f(CLR_RED, true, "Transmit failed: %s", esp_err_to_name(res));
            break;
    }
    return 1;
}

static void register_cansend(void) {
    cansend_args.message = arg_str1(NULL, NULL, "ID#data",
        "Message to send, ID and data bytes, all in hex. # is the delimiter. Use 'r' followed by DLC (0-8) for remote frames.");
    cansend_args.end = arg_end(2);
    const esp_console_cmd_t cmd = {
        .command = "cansend",
        .help = "Send a CAN message to the bus. Data frame: cansend 00008C03#02 | Remote frame: cansend 00008C03#r4",
        .hint = NULL,
        .func = &send_can_frame,
        .argtable = &cansend_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ------------------------------------------------------------------------- canup */

static const struct {
    const char *name;
    twai_mode_t mode;
    const char *description;
} can_modes[] = {
    { "normal", TWAI_MODE_NORMAL, "Normal" },
    { "no_ack", TWAI_MODE_NO_ACK, "No Ack" },
    { "listen_only", TWAI_MODE_LISTEN_ONLY, "Listen Only" },
};

static bool timing_for_speed(int bps, twai_timing_config_t *t) {
    switch (bps) {
// The very low bit rates need a large prescaler that not every chip has.
#ifdef TWAI_TIMING_CONFIG_1KBITS
        case 1000:    *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_1KBITS(); return true;
        case 5000:    *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_5KBITS(); return true;
        case 10000:   *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_10KBITS(); return true;
        case 12500:   *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_12_5KBITS(); return true;
        case 16000:   *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_16KBITS(); return true;
        case 20000:   *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_20KBITS(); return true;
#endif
        case 25000:   *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_25KBITS(); return true;
        case 50000:   *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_50KBITS(); return true;
        case 100000:  *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_100KBITS(); return true;
        case 125000:  *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_125KBITS(); return true;
        case 250000:  *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_250KBITS(); return true;
        case 500000:  *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_500KBITS(); return true;
        case 800000:  *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_800KBITS(); return true;
        case 1000000: *t = (twai_timing_config_t) TWAI_TIMING_CONFIG_1MBITS(); return true;
        default:      return false;
    }
}

static struct {
    struct arg_int *speed;
    struct arg_lit *filters;
    struct arg_lit *autorecover;
    struct arg_str *mode;
    struct arg_end *end;
} canup_args;

static int canup(int argc, char **argv) {
    const int nerrors = arg_parse(argc, argv, (void **) &canup_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, canup_args.end, argv[0]);
        return 1;
    }

    twai_timing_config_t t_config;
    if (!timing_for_speed(canup_args.speed->ival[0], &t_config)) {
        print_w_clr_time("Unsupported speed!", CLR_RED, true);
        return 1;
    }

    size_t mode = 0;
    if (canup_args.mode->count) {
        const char *mode_str = canup_args.mode->sval[0];
        for (mode = 0; mode < sizeof(can_modes) / sizeof(can_modes[0]); mode++) {
            if (strcmp(mode_str, can_modes[mode].name) == 0) break;
        }
        if (mode == sizeof(can_modes) / sizeof(can_modes[0])) {
            print_w_clr_time("Unsupported mode!", CLR_RED, true);
            return 1;
        }
    }
    twai_general_config_t g_config = default_g_config;
    g_config.mode = can_modes[mode].mode;

    const bool use_filters = canup_args.filters->count > 0;
    twai_filter_config_t f_config = use_filters ? hw_filter : (twai_filter_config_t) TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (use_filters) {
        printf("Using %s filters.\n", adv_filters.configured ? "smart" : "basic hw");
    } else {
        printf("Using accept all filters.\n");
    }
    print_w_clr_time_f(CLR_BLUE, true, "Starting CAN in %s Mode...", can_modes[mode].description);

    int ret = 0;
    xSemaphoreTake(can_mutex, portMAX_DELAY);
    // The driver logs a warning about the strapping pins on some boards; not interesting here.
    const esp_log_level_t prev_gpio_lvl = esp_log_level_get("gpio");
    esp_log_level_set("gpio", ESP_LOG_ERROR);
    esp_err_t res = twai_driver_install(&g_config, &t_config, &f_config);
    esp_log_level_set("gpio", prev_gpio_lvl);

    if (res == ESP_ERR_INVALID_STATE) {
        print_w_clr_time("Driver is already installed! Use candown first.", CLR_YELLOW, true);
        ret = 1;
    } else if (res != ESP_OK) {
        print_w_clr_time_f(CLR_RED, true, "Couldn't install CAN driver: %s", esp_err_to_name(res));
        ret = 1;
    } else {
        print_w_clr_time("CAN driver installed", CLR_BLUE, true);
        auto_recovery = canup_args.autorecover->count > 0;
        if (auto_recovery) print_w_clr_time("Auto recovery is enabled!", CLR_PURPLE, true);
        adv_filters.sw_filtering = use_filters && adv_filters.configured && adv_filters.needs_sw;
        if (adv_filters.sw_filtering) print_w_clr_time("Software filtering is active.", CLR_PURPLE, true);
        res = twai_start();
        if (res == ESP_OK) {
            is_error_passive = false;
            print_w_clr_time("CAN driver started", CLR_BLUE, true);
        } else {
            print_w_clr_time_f(CLR_RED, true, "Couldn't start CAN driver: %s", esp_err_to_name(res));
            ret = 1;
        }
    }
    xSemaphoreGive(can_mutex);
    return ret;
}

static void register_canup(void) {
    canup_args.speed = arg_int1(NULL, NULL, "<speed>", "CAN bus speed, in bps. See help for supported speeds.");
    canup_args.mode = arg_str0("m", "mode", "<normal|no_ack|listen_only>",
        "Set CAN mode. Normal (default), No Ack (for self-testing) or Listen Only (to prevent transmitting, for monitoring).");
    canup_args.filters = arg_lit0("f", NULL, "Use the filters configured with canfilter / cansmartfilter.");
    canup_args.autorecover = arg_lit0("r", "auto-recovery", "Automatically recover the bus after a bus-off event.");
    canup_args.end = arg_end(4);
    const esp_console_cmd_t cmd = {
        .command = "canup",
        .help = "Install the CAN driver and start the interface. Use it right after boot, or after candown to change the configuration. "
                "Supported speeds: 1mbits, 800kbits, 500kbits, 250kbits, 125kbits, 100kbits, 50kbits, 25kbits"
#ifdef TWAI_TIMING_CONFIG_1KBITS
                ", 20kbits, 16kbits, 12.5kbits, 10kbits, 5kbits, 1kbits"
#endif
                ".",
        .hint = NULL,
        .func = &canup,
        .argtable = &canup_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ------------------------------------------------------------------------- canstart / candown / canrecover */

static int canstart(int argc, char **argv) {
    int ret = 0;
    xSemaphoreTake(can_mutex, portMAX_DELAY);
    const esp_err_t res = twai_start();
    if (res == ESP_OK) {
        is_error_passive = false;
        print_w_clr_time("CAN driver started", CLR_GREEN, true);
    } else {
        print_w_clr_time("Driver is not in stopped state, or is not installed.", CLR_RED, true);
        ret = 1;
    }
    xSemaphoreGive(can_mutex);
    return ret;
}

static int candown(int argc, char **argv) {
    int ret = 1;
    xSemaphoreTake(can_mutex, portMAX_DELAY);
    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK) {
        print_w_clr_time("CAN driver is not installed!", CLR_RED, true);
        goto exit;
    }
    if (status.state == TWAI_STATE_RUNNING) {
        const esp_err_t res = twai_stop();
        if (res != ESP_OK) {
            print_w_clr_time_f(CLR_RED, true, "Couldn't stop CAN: %s", esp_err_to_name(res));
            goto exit;
        }
        print_w_clr_time("CAN was stopped.", CLR_GREEN, true);
    }
    // Uninstall is allowed from STOPPED and BUS_OFF, but not while recovering.
    const esp_err_t res = twai_driver_uninstall();
    if (res != ESP_OK) {
        print_w_clr_time_f(CLR_RED, true, "Couldn't uninstall CAN driver: %s", esp_err_to_name(res));
        goto exit;
    }
    adv_filters.sw_filtering = false;
    print_w_clr_time("CAN driver uninstalled.", CLR_GREEN, true);
    ret = 0;
exit:
    xSemaphoreGive(can_mutex);
    return ret;
}

static int canrecover(int argc, char **argv) {
    int ret = 0;
    xSemaphoreTake(can_mutex, portMAX_DELAY);
    const esp_err_t res = twai_initiate_recovery();
    if (res == ESP_OK) {
        print_w_clr_time("Started CAN recovery.", CLR_GREEN, true);
    } else if (can_read_status().state == CAN_NOT_INSTALLED) {
        print_w_clr_time("CAN driver is not installed!", CLR_RED, true);
        ret = 1;
    } else {
        print_w_clr_time("Can't start recovery - not in bus-off state!", CLR_RED, true);
        ret = 1;
    }
    xSemaphoreGive(can_mutex);
    return ret;
}

static void register_simple(const char *command, const char *help, esp_console_cmd_func_t func) {
    const esp_console_cmd_t cmd = {
        .command = command,
        .help = help,
        .hint = NULL,
        .func = func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ------------------------------------------------------------------------- canstats */

static int canstats(int argc, char **argv) {
    xSemaphoreTake(can_mutex, portMAX_DELAY);
    const can_status_t st = can_read_status();
    xSemaphoreGive(can_mutex);
    if (st.state == CAN_NOT_INSTALLED) {
        print_w_clr_time("CAN driver is not installed!", CLR_RED, true);
        return 1;
    }
    printf("status: %s\n", can_state_str(st.state));
    printf("TX Err Counter: %" PRIu32 "\n", st.tx_error_counter);
    printf("RX Err Counter: %" PRIu32 "\n", st.rx_error_counter);
    printf("Failed transmit: %" PRIu32 "\n", st.tx_failed_count);
    printf("Arbitration lost times: %" PRIu32 "\n", st.arb_lost_count);
    printf("Bus error count: %" PRIu32 "\n", st.bus_error_count);
    printf("RX missed (queue full): %" PRIu32 "\n", st.rx_missed_count);
    printf("RX overrun (FIFO): %" PRIu32 "\n", st.rx_overrun_count);
    printf("Queued TX / RX: %" PRIu32 " / %" PRIu32 "\n", st.msgs_to_tx, st.msgs_to_rx);
    printf("Software filtering: %s\n", adv_filters.sw_filtering ? "on" : "off");
    return 0;
}

/* ------------------------------------------------------------------------- canfilter */

static struct {
    struct arg_lit *dual_arg;
    struct arg_str *code_arg;
    struct arg_str *mask_arg;
    struct arg_end *end;
} canfilter_args;

static int canfilter(int argc, char **argv) {
    const int nerrors = arg_parse(argc, argv, (void **) &canfilter_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, canfilter_args.end, argv[0]);
        return 1;
    }
    const char *mask_s = canfilter_args.mask_arg->sval[0];
    const char *code_s = canfilter_args.code_arg->sval[0];
    uint32_t mask, code;
    if (!parse_hex_u32(mask_s, strlen(mask_s), &mask) || !parse_hex_u32(code_s, strlen(code_s), &code)) {
        return invalid_args();
    }
    xSemaphoreTake(can_mutex, portMAX_DELAY);
    hw_filter.single_filter = canfilter_args.dual_arg->count == 0;
    hw_filter.acceptance_code = code;
    hw_filter.acceptance_mask = mask;
    adv_filters.configured = false;
    adv_filters.sw_filtering = false;
    xSemaphoreGive(can_mutex);
    print_w_clr_time_f(CLR_GREEN, true, "Hardware filter set in %s mode.", hw_filter.single_filter ? "single" : "dual");
    printf("mask: %08" PRIX32 ", code: %08" PRIX32 "\n", mask, code);
    printf("Apply it with: canup <speed> -f\n");
    return 0;
}

static void register_canfilter(void) {
    canfilter_args.mask_arg = arg_str1("m", "mask", "<mask>", "Acceptance mask (as in esp-idf docs), uint32_t in hex, up to 8 digits.");
    canfilter_args.code_arg = arg_str1("c", "code", "<code>", "Acceptance code (as in esp-idf docs), uint32_t in hex, up to 8 digits.");
    canfilter_args.dual_arg = arg_lit0("d", NULL, "Use Dual Filter Mode.");
    canfilter_args.end = arg_end(4);
    const esp_console_cmd_t cmd = {
        .command = "canfilter",
        .help = "Manually set up basic hardware filtering. Takes effect on the next `canup -f`.",
        .hint = NULL,
        .func = &canfilter,
        .argtable = &canfilter_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ------------------------------------------------------------------------- cansmartfilter */

static struct {
    struct arg_str *filters;
    struct arg_end *end;
} cansmart_args;

static int cansmartfilter(int argc, char **argv) {
    const int nerrors = arg_parse(argc, argv, (void **) &cansmart_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cansmart_args.end, argv[0]);
        return 1;
    }
    // Parse everything into a local copy first so that a bad argument leaves the current
    // configuration untouched.
    smart_filt_element_t items[CONFIG_CAN_MAX_SMARTFILTERS_NUM];
    const size_t count = (size_t) cansmart_args.filters->count;
    if (count == 0 || count > CONFIG_CAN_MAX_SMARTFILTERS_NUM) return invalid_args();
    for (size_t i = 0; i < count; i++) {
        const char *code_s, *mask_s;
        size_t code_len, mask_len;
        if (!split_hash(cansmart_args.filters->sval[i], &code_s, &code_len, &mask_s, &mask_len) ||
            !parse_hex_u32(code_s, code_len, &items[i].code) ||
            !parse_hex_u32(mask_s, mask_len, &items[i].mask)) {
            return invalid_args();
        }
    }

    // Derive the widest hardware filter that passes every entry: keep only the mask bits every
    // entry cares about *and* agrees on. If that is wider than any single entry, the frames it
    // lets through must also be matched in software.
    uint32_t hw_mask = items[0].mask;
    uint32_t hw_code = items[0].code & items[0].mask;
    bool needs_sw = false;
    for (size_t i = 1; i < count; i++) {
        const uint32_t mask = items[i].mask;
        const uint32_t code = items[i].code & mask;
        const uint32_t common = hw_mask & mask;
        const uint32_t differing = (hw_code ^ code) & common;
        const uint32_t new_mask = common & ~differing;
        if (new_mask != hw_mask || new_mask != mask) needs_sw = true;
        hw_mask = new_mask;
        hw_code &= hw_mask;
    }

    xSemaphoreTake(can_mutex, portMAX_DELAY);
    memcpy(adv_filters.items, items, count * sizeof(items[0]));
    adv_filters.count = count;
    adv_filters.configured = true;
    adv_filters.needs_sw = needs_sw;
    adv_filters.sw_filtering = false;  // activated by canup -f
    // Extended-frame layout of the acceptance registers: the 29-bit ID sits in bits 31..3.
    hw_filter.single_filter = true;
    hw_filter.acceptance_code = hw_code << 3;
    hw_filter.acceptance_mask = ~(hw_mask << 3);
    xSemaphoreGive(can_mutex);

    print_w_clr_time("Smart filters were set.", CLR_GREEN, true);
    printf("Number of filters: %zu, hardware mask: %08" PRIX32 ", code: %08" PRIX32 ", software filtering %s.\n",
           count, hw_mask, hw_code, needs_sw ? "needed" : "not needed");
    printf("Apply them with: canup <speed> -f\n");
    return 0;
}

static void register_cansmartfilter(void) {
    cansmart_args.filters = arg_strn(NULL, NULL, "<code#mask>", 1, CONFIG_CAN_MAX_SMARTFILTERS_NUM,
        "Filters in the form code#mask, both uint32_t in hex (up to 8 digits). A frame passes if (ID & mask) == (code & mask). Example: 0000FF00#0000FFFF");
    cansmart_args.end = arg_end(2);
    const esp_console_cmd_t cmd = {
        .command = "cansmartfilter",
        .help = "Set up smart mixed filters (hardware + software). Only extended-frame IDs are supported; "
                "standard frames are not filtered correctly. Takes effect on the next `canup -f`.",
        .hint = NULL,
        .func = &cansmartfilter,
        .argtable = &cansmart_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ------------------------------------------------------------------------- registration */

void register_can_commands(void) {
    register_cansend();
    register_canup();
    register_simple("candown", "Stop the CAN interface and uninstall the driver, e.g. to canup with different parameters/filters.", &candown);
    register_simple("canstats", "Print CAN statistics.", &canstats);
    register_simple("canstart", "Start the CAN interface, used after bus recovery; otherwise see canup.", &canstart);
    register_simple("canrecover", "Recover CAN after bus-off. Used when auto-recovery is off.", &canrecover);
    register_canfilter();
    register_cansmartfilter();
}
