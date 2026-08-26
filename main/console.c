#include "console.h"
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"
#include "sdkconfig.h"
#include "can.h"
#include "cmd_can.h"
#include "cmd_system.h"
#include "cmd_utils.h"
#include "fs.h"
#include "xvprintf.h"

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "soc/soc_caps.h"
#else
#error "Unsupported console device: select USB Serial/JTAG or UART under Component config -> ESP System Settings -> Channel for console output"
#endif

#ifdef CONFIG_LOG_COLORS
#define USE_COLORS true
#else
#define USE_COLORS false
#endif

// How often the prompt (CAN state, error counters) is refreshed while idle.
#define PROMPT_REFRESH_TICKS pdMS_TO_TICKS(200)

static char prompt_buf[96];
static esp_console_config_t console_config;
static struct linenoiseState ls;

// Owned by whichever task currently drives the terminal: the interactive task while it runs a
// command, the TX task while it prints. Lock order: console_taken_sem, then linenoiseOutputLock().
static SemaphoreHandle_t console_taken_sem;

// Rebuilds the prompt from the current CAN state. Returns true if it changed.
// Call with console_taken_sem held (it writes ls.prompt / ls.plen).
static bool update_prompt(void) {
    const can_status_t st = curr_can_state;
    const char *color;
    switch (st.state) {
        case CAN_ERROR_ACTIVE:  color = CLR_GREEN; break;
        case CAN_ERROR_PASSIVE: color = CLR_YELLOW; break;
        default:                color = CLR_RED; break;
    }
    char text[64];
    size_t n = (size_t) snprintf(text, sizeof(text), "%s", can_state_str(st.state));
    if (st.state != CAN_NOT_INSTALLED && n < sizeof(text)) {
        n += (size_t) snprintf(text + n, sizeof(text) - n, " [TEC: %" PRIu32 "][REC: %" PRIu32 "]",
                               st.tx_error_counter, st.rx_error_counter);
    }
    if (n < sizeof(text)) snprintf(text + n, sizeof(text) - n, " > ");

    char new_prompt[sizeof(prompt_buf)];
    if (USE_COLORS) {
        snprintf(new_prompt, sizeof(new_prompt), ANSI_COLOR("%s") "%s" ANSI_RESET, color, text);
    } else {
        snprintf(new_prompt, sizeof(new_prompt), "%s", text);
    }
    const bool changed = strcmp(new_prompt, prompt_buf) != 0;
    memcpy(prompt_buf, new_prompt, sizeof(prompt_buf));
    ls.prompt = prompt_buf;
    ls.plen = strlen(text);  // printable width: escape sequences excluded
    return changed;
}

// Drains uart_tx_ringbuf to the terminal, hiding the edited line around each message,
// and keeps the prompt up to date.
static void console_task_tx(void *arg) {
    (void) arg;
    const int fd = fileno(stdout);
    for (;;) {
        size_t size = 0;
        char *msg = xRingbufferReceive(uart_tx_ringbuf, &size, PROMPT_REFRESH_TICKS);
        xSemaphoreTake(console_taken_sem, portMAX_DELAY);
        linenoiseOutputLock();
        const bool prompt_changed = update_prompt();
        if (msg != NULL || prompt_changed) {
            linenoiseHide(&ls);
            if (msg != NULL) {
                // Items are sent with their NUL terminator; don't print it.
                if (size > 1) {
                    write(fd, msg, size - 1);
                    flushWrite();
                }
                vRingbufferReturnItem(uart_tx_ringbuf, msg);
            }
            linenoiseShow(&ls);
        }
        linenoiseOutputUnlock();
        xSemaphoreGive(console_taken_sem);
    }
}

static void run_command(const char *line) {
    int ret = 0;
    const esp_err_t err = esp_console_run(line, &ret);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("Unrecognized command\n");
    } else if (err == ESP_ERR_INVALID_ARG) {
        // empty command line
    } else if (err != ESP_OK) {
        printf("Internal error: %s\n", esp_err_to_name(err));
    } else if (ret != 0) {
        printf("Command returned error code %d\n", ret);
    }
}

void console_task_interactive(void *arg) {
    (void) arg;
    char *buf = calloc(1, console_config.max_cmdline_length);
    assert(buf != NULL);

    printf("Testing your console...\n");
    if (linenoiseProbe() != 0) {
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using PuTTY instead.\n");
        linenoiseSetDumbMode(1);
    }
    printf("\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n"
           "Ctrl+C restarts the device.\n");

    ls.buf = buf;
    ls.buflen = console_config.max_cmdline_length;
    update_prompt();
    linenoiseEditStart(&ls);
    xTaskCreate(console_task_tx, "console tsk tx", 5000, NULL, CONFIG_CONSOLE_TX_PRIORITY, NULL);
    // From here on ESP_LOG output goes through the ring buffer / TX task.
    esp_log_set_vprintf(&vxprintf);

    for (;;) {
        char *line = linenoiseEditFeed(&ls);
        if (line == linenoiseEditMore) continue;
        xSemaphoreTake(console_taken_sem, portMAX_DELAY);
        linenoiseEditStop(&ls);
        if (line == NULL) break;  // Ctrl+C, Ctrl+D or I/O error

        if (line[0] != '\0') {
            linenoiseHistoryAdd(line);
            linenoiseHistorySave(HISTORY_PATH);
        }
        run_command(line);
        linenoiseFree(line);
        update_prompt();
        linenoiseEditStart(&ls);
        xSemaphoreGive(console_taken_sem);
    }

    printf("\nConsole terminated. Restarting...\n");
    fflush(stdout);
    esp_console_deinit();
    esp_restart();
}

void initialize_console(void) {
    console_taken_sem = xSemaphoreCreateMutex();
    assert(console_taken_sem != NULL);

    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_serial_jtag_config));
    usb_serial_jtag_vfs_use_driver();
#else
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
    const uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if SOC_UART_SUPPORT_REF_TICK
        .source_clk = UART_SCLK_REF_TICK,
#elif SOC_UART_SUPPORT_XTAL_CLK
        .source_clk = UART_SCLK_XTAL,
#endif
    };
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif

    /* Blocking mode on stdin and stdout (linenoise reads byte by byte) */
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin), F_SETFL, 0);

    console_config.max_cmdline_args = CONFIG_CONSOLE_MAX_CMDLINE_ARGS;
    console_config.max_cmdline_length = CONFIG_CONSOLE_MAX_CMDLINE_LENGTH;
    console_config.hint_color = USE_COLORS ? atoi(CLR_CYAN) : 0;
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback *) &esp_console_get_hint);
    linenoiseHistorySetMaxLen(30);
    linenoiseSetMaxLineLen(console_config.max_cmdline_length);
    linenoiseHistoryLoad(HISTORY_PATH);

    esp_console_register_help_command();
    register_system();
    register_can_commands();
    register_utils_commands();
}
