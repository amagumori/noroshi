// https://github.com/espressif/esp-idf/tree/master/examples/storage/sd_card/sdmmc

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "SD_CARD";

#define MOUNT_POINT "/sd"

void main ( void ) {
  esp_err_t err;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
    .format_if_mount_failed = true,
#else
    .format_if_mount_failed = false,
#endif
    .max_files = 5,
    .allocation_unit_size = 16 * 1024
  };

  sdmmc_card_t *sd_card;

  const char mount_point[] = MOUNT_POINT;
  ESP_LOGI(TAG, "initializing SD card.");

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 4;

  // MOUNT
  err = esp_vfs_fat_sdmmc_mount( mount_point, &host, &slot_config, &mount_config, &sd_card );

  if ( err != ESP_OK ) {
    if ( err != ESP_FAIL ) {
      ESP_LOGE(TAG, "failed to mount fs.");
    } else {
      ESP_LOGE(TAG, "failed to init card ( %s ) ", esp_err_to_name(err));
    }
    return;
  }
  ESP_LOGI(TAG, "fs mounted!");

  sdmmc_card_print_info(stdout, sd_card);

  // then do stuff.

  esp_vfs_fat_sdcard_unmount(mount_point, sd_card);

}
