#include "chest_controller.h"
#include "esp_err.h"

void app_main(void)
{
    ESP_ERROR_CHECK(chest_controller_start());
}
