#include "app_tests.h"

#include "esp_app_desc.h"
#include "esp_log.h"

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char elf_sha256[65];
    esp_app_get_elf_sha256(elf_sha256, sizeof(elf_sha256));
    ESP_LOGI("firmware", "09 resource_checks=v3 built=%s %s IDF=%s ELF=%s",
             app->date, app->time, app->idf_ver, elf_sha256);
    app_runtime_start();
}
