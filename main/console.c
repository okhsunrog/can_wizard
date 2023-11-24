#include "console.h"
#include <fcntl.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "freertos/projdefs.h"
#include "linenoise/linenoise.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_usb_serial_jtag.h"
#include "driver/usb_serial_jtag.h"
#include "cmd_system.h"
#include "fs.h"
#include "xvprintf.h"

#if CONFIG_LOG_COLORS
static const bool use_colors = true;
#else
static const bool use_colors = true;
#endif

static const char* TAG = "console task";
esp_console_config_t console_config;
struct linenoiseState ls;

char prompt[50];

static void get_prompt(char* prompt_buf) {
    static const char* text = "can_wizard > ";
    static const char* prompt_color = LOG_COLOR_E;
    memset(prompt_buf,0,strlen(prompt_buf));
    if (use_colors) {
        strcat(prompt_buf, prompt_color);
        strcat(prompt_buf, text);
        strcat(prompt_buf, LOG_RESET_COLOR);
    } else {
        strcat(prompt_buf, text);
    }
}

void console_task_tx(void* arg) {
    const int fd = fileno(stdout);
    char *msg_to_print;
    size_t msg_to_print_size;
    vTaskDelay(pdMS_TO_TICKS(200));
    while(1) {
        msg_to_print = (char *)xRingbufferReceive(can_messages, &msg_to_print_size, portMAX_DELAY);
        if (msg_to_print != NULL) {
            xSemaphoreTake(stdout_taken_sem, portMAX_DELAY);
            linenoiseHide(&ls);
            write(fd, msg_to_print, msg_to_print_size);
            flushWrite();
            linenoiseShow(&ls);
            xSemaphoreGive(stdout_taken_sem);
            vRingbufferReturnItem(can_messages, (void *) msg_to_print);
        }
    }
}

void console_task_interactive(void* arg) {
    stdout_taken_sem = xSemaphoreCreateMutex();
    char *buf = calloc(1, console_config.max_cmdline_length);
    char *line;
    vTaskDelay(pdMS_TO_TICKS(15)); //give some time for app_main to finish
    /* Figure out if the terminal supports escape sequences */
    printf("Testing your console...");
    int probe_status = linenoiseProbe();
    // int probe_status = 1;
    if (probe_status) { /* zero indicates success */
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead.\n"
               "Your terminal is in Dumb Mode from now! \n");
        linenoiseSetDumbMode(1);
    }
    printf("\n"
       "Type 'help' to get the list of commands.\n"
       "Use UP/DOWN arrows to navigate through command history.\n"
       "Press TAB when typing command name to auto-complete.\n"
       "Ctrl+C will terminate the console environment.\n");
    get_prompt(prompt);
    linenoiseEditStart(&ls, buf, console_config.max_cmdline_length, prompt);
    while (true) {
        line = linenoiseEditFeed(&ls);
        if (line == linenoiseEditMore) continue;
        linenoiseEditStop(&ls);
        if (line == NULL) { /* Break on EOF or error */
            ESP_LOGE(TAG, "Ctrl+C???");
            break;
        }
        /* Add the command to the history if not empty*/
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
            /* Save command history to filesystem */
            linenoiseHistorySave(HISTORY_PATH);
        }

        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
        linenoiseEditStart(&ls, buf, console_config.max_cmdline_length, prompt);
    }

    ESP_LOGE(TAG, "Terminating console");
    esp_console_deinit();
    while (1) {
        vTaskDelay(100);
    }
}

void initialize_console(void) {
    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);
    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_usb_serial_jtag_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_usb_serial_jtag_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    /* Enable non-blocking mode on stdin and stdout */
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin), F_SETFL, 0);
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    /* Install USB-SERIAL-JTAG driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_serial_jtag_config));
    /* Tell vfs to use usb-serial-jtag driver */
    esp_vfs_usb_serial_jtag_use_driver();
    console_config.max_cmdline_args = 8;
    console_config.max_cmdline_length = 256;
    if (use_colors) console_config.hint_color = atoi(LOG_COLOR_CYAN);
    ESP_ERROR_CHECK(esp_console_init(&console_config));
    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);
    linenoiseHistorySetMaxLen(100);
    linenoiseSetMaxLineLen(console_config.max_cmdline_length);
    linenoiseHistoryLoad(HISTORY_PATH);
    /* Register commands */
    esp_console_register_help_command();
    register_system();
}
