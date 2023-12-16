#include "cmd_can.h"
#include "can.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hal/twai_types.h"
#include "inttypes.h"
#include "list.h"
#include "sdkconfig.h"
#include "freertos/projdefs.h"
#include "string.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "xvprintf.h"
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>

twai_filter_config_t my_filters = TWAI_FILTER_CONFIG_ACCEPT_ALL();

static void register_cansend(void);
static void register_canup(void);
static void register_candown(void);
static void register_canstats(void);
static void register_canstart(void);
static void register_canrecover(void);
static void register_canfilter(void);
static void register_cansmartfilter(void);

void register_can_commands(void) {
    register_cansend();
    register_canup();
    register_candown();
    register_canstats();
    register_canstart();
    register_canrecover();
    register_canfilter();
    register_cansmartfilter();
}

static struct {
    struct arg_str *message;
    struct arg_end *end;
} cansend_args;

static int send_can_frame(int argc, char **argv) {
    twai_message_t msg = { 0 };
    char printf_str[70];
    const int nerrors = arg_parse(argc, argv, (void **) &cansend_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cansend_args.end, argv[0]);
        return 1;
    }
    const char *can_msg_ptr = cansend_args.message->sval[0];
    char* can_msg_str_buf = strdup(can_msg_ptr);
    const char* id_substr = strtok(can_msg_str_buf, "#");
    const char *data_substr = strtok(NULL, "#");
    if ((id_substr == NULL) || (strtok(NULL, "#") != NULL)) goto invalid_args;
    const int id_l = strlen(id_substr);
    const int dt_l = data_substr == NULL ? 0 : strlen(data_substr);
    if ((id_l > 8) || (dt_l > 16) || (dt_l % 2)) goto invalid_args;
    for (int i = 0; i < id_l; i++) if(!isxdigit((int) id_substr[i])) goto invalid_args;
    for (int i = 0; i < dt_l; i++) if(!isxdigit((int) data_substr[i])) goto invalid_args;
    int msg_id;
    if (sscanf(id_substr, "%X", &msg_id) < 1) goto invalid_args;
    for (int i = 0; i < (dt_l / 2); i++) {
        char* byte_to_parse = malloc(3);
        // ReSharper disable once CppDFANullDereference (if dt_l == 0 we skip this loop)
        strncpy(byte_to_parse, i * 2 + data_substr, 2);
        int num;
        const int res = sscanf(byte_to_parse, "%X", &num);
        free(byte_to_parse);
        if (res < 1) goto invalid_args;
        msg.data[i] = num;
    }
    msg.data_length_code = dt_l / 2;
    msg.identifier = msg_id;
    msg.extd = (id_l > 3);
    const esp_err_t res = twai_transmit(&msg, pdMS_TO_TICKS(1000));
    switch(res) {
        case ESP_OK:
            can_msg_to_str(&msg, "sent ", printf_str);
            print_w_clr_time(printf_str, NULL, true);
            break;
        case ESP_ERR_TIMEOUT:
            print_w_clr_time("Timeout!", LOG_COLOR_RED, true);
            break;
        case ESP_ERR_NOT_SUPPORTED:
            print_w_clr_time("Can't sent in Listen-Only mode!", LOG_COLOR_RED, true);
            break;
        default:
            print_w_clr_time("Invalid state!", LOG_COLOR_RED, true);
            break;
    }
    free(can_msg_str_buf);
    return 0;
invalid_args:
    print_w_clr_time("Invalid arguments!", LOG_COLOR_RED, true);
    free(can_msg_str_buf);
    return 1;
}

static int canrecover(int argc, char **argv) {
    const esp_err_t res = twai_initiate_recovery();
    if (res == ESP_OK) print_w_clr_time("Started CAN recovery.", LOG_COLOR_GREEN, true);
    else if (curr_can_state.state == CAN_NOT_INSTALLED) print_w_clr_time("CAN driver is not installed!", LOG_COLOR_RED, true);
    else print_w_clr_time("Can't start recovery - not in bus-off state!", LOG_COLOR_RED, true);
    return 0;
}

static const char* can_states_str[] = {"not installed", "stopped", "error active", "error passive", "bus off", "recovering"};

// ReSharper disable once CppDFAConstantFunctionResult
static int canstats(int argc, char **argv) {
    if (curr_can_state.state == CAN_NOT_INSTALLED) {
        print_w_clr_time("CAN driver is not installed!", LOG_COLOR_RED, true);
        return 0;
    } else {
        const char *state_str = can_states_str[curr_can_state.state];
        printf("status: %s\n", state_str);
        printf("TX Err Counter: %" PRIu32 "\n", curr_can_state.tx_error_counter);
        printf("RX Err Counter: %" PRIu32 "\n", curr_can_state.rx_error_counter);
        printf("Failed transmit: %" PRIu32 "\n", curr_can_state.tx_failed_count);
        printf("Arbitration lost times: %" PRIu32 "\n", curr_can_state.arb_lost_count);
        printf("Bus-off count: %" PRIu32 "\n", curr_can_state.bus_error_count);
    }
    return 0;
}

static const char* can_modes[] = {
    "normal",
    "no_ack",
    "listen_only",
};

static struct {
    struct arg_int *speed;
    struct arg_lit *filters;
    struct arg_lit *autorecover;
    struct arg_str *mode;
    struct arg_end *end;
} canup_args;

static int canup(int argc, char **argv) {
    static twai_timing_config_t t_config;
    twai_general_config_t gen_cfg = default_g_config;
    twai_filter_config_t f_config;
    const int nerrors = arg_parse(argc, argv, (void **) &canup_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, canup_args.end, argv[0]);
        return 1;
    }
    if (canup_args.filters->count) {
        f_config = my_filters;
        printf("Using %s filters.\n", adv_filters.enabled ? "smart" : "basic hw");
    } else {
        adv_filters.enabled = false;
        adv_filters.sw_filtering = false;
        f_config = (twai_filter_config_t) TWAI_FILTER_CONFIG_ACCEPT_ALL();
        printf("Using accept all filters.\n");
    }
    const esp_log_level_t prev_gpio_lvl = esp_log_level_get("gpio");
    int mode = 0;
    if (canup_args.mode->count) {
        const char* mode_str = canup_args.mode->sval[0];
        while (mode < 4) {
            if (mode == 3) {
                print_w_clr_time("Unsupported mode!", LOG_COLOR_RED, true);
                return 1;
            }
            if (memcmp(mode_str, can_modes[mode], strlen(mode_str)) == 0) break;
            mode++;
        }
    }
    switch(mode) {
        case 1:
            gen_cfg.mode = TWAI_MODE_NO_ACK;
            print_w_clr_time("Starting CAN in No Ack Mode...", LOG_COLOR_BLUE, true);
            break;
        case 2:
            gen_cfg.mode = TWAI_MODE_LISTEN_ONLY;
            print_w_clr_time("Starting CAN in Listen Only Mode...", LOG_COLOR_BLUE, true);
            break;
        default: //0
            print_w_clr_time("Starting CAN in Normal Mode...", LOG_COLOR_BLUE, true);
            break;
    }
    switch (canup_args.speed->ival[0]) {
        case 1000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_1KBITS();
            break;
        case 5000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_5KBITS();
            break;
        case 10000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_10KBITS();
            break;
        case 12500:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_12_5KBITS();
            break;
        case 16000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_16KBITS();
            break;
        case 20000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_20KBITS();
            break;
        case 25000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_25KBITS();
            break;
        case 50000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_50KBITS();
            break;
        case 100000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_100KBITS();
            break;
        case 125000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_125KBITS();
            break;
        case 250000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_250KBITS();
            break;
        case 500000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_500KBITS();
            break;
        case 800000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_800KBITS();
            break;
        case 1000000:
            t_config = (twai_timing_config_t) TWAI_TIMING_CONFIG_1MBITS();
            break;
        default:
            print_w_clr_time("Unsupported speed!", LOG_COLOR_RED, true);
            return 1;
    }
    xSemaphoreTake(can_mutex, portMAX_DELAY);
    esp_log_level_set("gpio", ESP_LOG_ERROR);
    const esp_err_t res = twai_driver_install(&gen_cfg, &t_config, &f_config);
    if (res == ESP_OK) {
        print_w_clr_time("CAN driver installed", LOG_COLOR_BLUE, true);
        if (canup_args.autorecover->count) {
            print_w_clr_time("Auto recovery is enabled!", LOG_COLOR_PURPLE, true);
            auto_recovery = true;
        } else auto_recovery = false;
    } else if (res == ESP_ERR_INVALID_STATE) {
        print_w_clr_time("Driver is already installed!", LOG_COLOR_BROWN, true);
        goto free_exit;
    } else {
        print_w_clr_time("Couldn't install CAN driver! Rebooting...", LOG_COLOR_RED, true);
        esp_restart();
    }
    ESP_ERROR_CHECK(twai_start());
    is_error_passive = false;
    print_w_clr_time("CAN driver started", LOG_COLOR_BLUE, true);
free_exit:
    xSemaphoreGive(can_mutex);
    esp_log_level_set("gpio", prev_gpio_lvl);
    return 0;
}

static int canstart(int argc, char **argv) {
    xSemaphoreTake(can_mutex, portMAX_DELAY);
    const esp_err_t res = twai_start();
    if (res == ESP_OK) {
        print_w_clr_time("CAN driver started", LOG_COLOR_GREEN, true);
        is_error_passive = false;
    } else print_w_clr_time("Driver is not in stopped state, or is not installed.", LOG_COLOR_RED, true);
    xSemaphoreGive(can_mutex);
    return 0;
}

static int candown(int argc, char **argv) {
    xSemaphoreTake(can_mutex, portMAX_DELAY);
    if (curr_can_state.state != CAN_BUF_OFF) {
        const esp_err_t res = twai_stop();
        if (res == ESP_OK) print_w_clr_time("CAN was stopped.", LOG_COLOR_GREEN, true);
        else {
            print_w_clr_time("Driver is not in running state, or is not installed.", LOG_COLOR_RED, true);
            xSemaphoreGive(can_mutex);
            return 1;
        }
    }
    ESP_ERROR_CHECK(twai_driver_uninstall());
    xSemaphoreGive(can_mutex);
    return 0;
}

static void register_cansend(void) {
    
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

    canup_args.speed = arg_int1(NULL, NULL, "<speed>", "CAN bus speed, in bps. See help for supported speeds.");
    canup_args.mode = arg_str0("m", "mode", "<normal|no_ack|listen_only>", "Set CAN mode. Normal (default), No Ack (for self-testing) or Listen Only (to prevent transmitting, for monitoring).");
    canup_args.filters = arg_lit0("f", NULL, "Use predefined CAN filters.");
    canup_args.autorecover = arg_lit0("r", "auto-recovery", "Set to enable auto-recovery of CAN bus if case of bus-off event");
    canup_args.end = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command = "canup",
        .help = "Install can drivers and start can interface. Used right after board start or during runtime for changing CAN configuration. Supported speeds: 1mbits, 800kbits, 500kbits, 250kbits, 125kbits, 100kbits, 50kbits, 25kbits, 20kbits, 16kbits, 12.5kbits, 10kbits, 5kbits, 1kbits.",
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
        .help = "Start CAN interface, used after bus recovery, otherwise see canup command.",
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
    const char* mask_s = canfilter_args.mask_arg->sval[0];
    const char* code_s = canfilter_args.code_arg->sval[0];
    const int m_l = strlen(mask_s);
    const int c_l = strlen(code_s);
    if (m_l != 8 || c_l != 8) goto invalid_args;
    for (int i = 0; i < m_l; i++) if(!isxdigit((int) mask_s[i])) goto invalid_args;
    for (int i = 0; i < c_l; i++) if(!isxdigit((int) code_s[i])) goto invalid_args;
    uint32_t mask = 0;
    uint32_t code = 0;
    if (sscanf(mask_s, "%" PRIX32, &mask) < 1) goto invalid_args;
    if (sscanf(code_s, "%" PRIX32, &code) < 1) goto invalid_args;
    if (canfilter_args.dual_arg->count) {
        my_filters.single_filter = false;
        print_w_clr_time("Setting hw filters in dual mode.", LOG_COLOR_GREEN, true);
    } else {
        my_filters.single_filter = true;
        print_w_clr_time("Setting hw filters in single mode.", LOG_COLOR_GREEN, true);
    }
    printf("mask: %" PRIX32 ", code: %" PRIX32 "\n", mask, code);
    my_filters.acceptance_code = code;
    my_filters.acceptance_mask = mask;
    adv_filters.enabled = false;
    adv_filters.sw_filtering = false;
    return 0;
invalid_args:
    print_w_clr_time("Invalid arguments!", LOG_COLOR_RED, true);
    return 1;
}

static void register_canfilter(void) {

    canfilter_args.mask_arg = arg_str1("m", "mask", "<mask>", "Acceptance mask (as in esp-idf docs), uint32_t in hex form, 8 symbols.");
    canfilter_args.code_arg = arg_str1("c", "code", "<code>", "Acceptance code (as in esp-idf docs), uint32_t in hex form, 8 symbols.");
    canfilter_args.dual_arg = arg_lit0("d", NULL, "Use Dual Filter Mode.");
    canfilter_args.end = arg_end(4);


    const esp_console_cmd_t cmd = {
        .command = "canfilter",
        .help = "Manually setup basic hardware filtering.",
        .hint = NULL,
        .func = &canfilter,
        .argtable = &canfilter_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_str *filters;
    struct arg_end *end;
} cansmart_args;


void smartfilters_destroy(List** head) {
    while (*head != NULL) {
        List* tmp_cursor = *head;
        *head = (*head)->next;
        free((smart_filt_element_t *) tmp_cursor->data);
        free(tmp_cursor);
    }
}

static int cansmartfilter(int argc, char **argv) {
    char *filter_str_buf = NULL;
    smart_filt_element_t* filt_element = NULL;
    const int nerrors = arg_parse(argc, argv, (void **) &cansmart_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cansmart_args.end, argv[0]);
        return 1;
    }
    smartfilters_destroy(&adv_filters.filters);
    adv_filters.sw_filtering = false;
    adv_filters.enabled = false;
    bool tmp_sw = false;
    uint32_t hwfilt_code = 0;
    uint32_t hwfilt_mask = 0;
    for (int i = 0; i < cansmart_args.filters->count; i++) {
        filt_element = malloc(sizeof(smart_filt_element_t));
        const char *filter_str_ptr = cansmart_args.filters->sval[i];
        filter_str_buf = strdup(filter_str_ptr);
        const char* code_substr = strtok(filter_str_buf, "#");
        const char *mask_substr = strtok(NULL, "#");
        if (code_substr == NULL || mask_substr == NULL || strtok(NULL, "#") != NULL) goto invalid_args;
        const int m_l = strlen(mask_substr);
        const int c_l = strlen(code_substr);
        if (m_l > 8 || c_l > 8) goto invalid_args;
        for (int j = 0; j < m_l; j++) if (!isxdigit((int) mask_substr[j])) goto invalid_args;
        for (int j = 0; j < c_l; j++) if (!isxdigit((int) code_substr[j])) goto invalid_args;
        if (sscanf(code_substr, "%" PRIX32, &filt_element->filt) < 1) goto invalid_args;
        if (sscanf(mask_substr, "%" PRIX32, &filt_element->mask) < 1) goto invalid_args;
        free(filter_str_buf);
        list_push(&adv_filters.filters, (void *) filt_element);
        if (i == 0) {
            hwfilt_mask = filt_element->mask;
            hwfilt_code = filt_element->filt;
        } else {
            const uint32_t common_bits = filt_element->mask & hwfilt_mask;
            const uint32_t new_bits = filt_element->mask - common_bits;
            const uint32_t missing_bits = hwfilt_mask - common_bits;
            hwfilt_mask &= filt_element->mask;
            const uint32_t bit_to_delete = (hwfilt_code ^ filt_element->filt) & hwfilt_mask;
            hwfilt_mask -= bit_to_delete;
            if (new_bits || missing_bits || bit_to_delete) tmp_sw = true;
        }
        filt_element = NULL;
    }
    my_filters.single_filter = true;
    my_filters.acceptance_mask = ~(hwfilt_mask << 3);
    my_filters.acceptance_code = hwfilt_code << 3;
    adv_filters.sw_filtering = tmp_sw;
    adv_filters.enabled = true;
    print_w_clr_time("Smart filters were set.", LOG_COLOR_GREEN, true);
    printf("Num of smart filters: %d\n", list_sizeof(adv_filters.filters));
    return 0;
invalid_args:
    free(filter_str_buf);
    free(filt_element);
    print_w_clr_time("Invalid arguments!", LOG_COLOR_RED, true);
    smartfilters_destroy(&adv_filters.filters);
    return 1;
}

static void register_cansmartfilter(void) {

    cansmart_args.filters = arg_strn(NULL, NULL, "<code#mask>", 1, CONFIG_CAN_MAX_SMARTFILTERS_NUM, "Filters, each one contains mask and code in format code#mask. Both mask and code are uint32_t numbers in hex format. Example: 0000FF00#0000FFFF");
    cansmart_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "cansmartfilter",
        .help = "Setup smart mixed filters (hardware + software). Num of filters can be up to the value in config. Supportd only ID filtering of extended frames, standart frames aren't supported for now.",
        .hint = NULL,
        .func = &cansmartfilter,
        .argtable = &cansmart_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
