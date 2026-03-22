#include "app.h"
#include "esp_log.h"
#include "esp_err.h"

extern "C" void app_main(void)
{
    static Application app;
    app.run();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    ESP_LOGE("STACK", "Stack overflow in task: %s", pcTaskName);
    abort();
}