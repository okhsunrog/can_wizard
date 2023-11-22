#include "fs.h"
#include "esp_vfs_fat.h"

static const char* TAG = "fs";

/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */

void initialize_filesystem(void) {
    static wl_handle_t wl_handle;
    const esp_vfs_fat_mount_config_t mount_config = {.max_files = 4, .format_if_mount_failed = true};
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_PATH, "storage", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
}

