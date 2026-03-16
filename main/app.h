#pragma once

#include "freertos/FreeRTOS.h" // MUST be first FreeRTOS header
#include "freertos/task.h"
#include "freertos/queue.h"

#include "WiFiManager.h"

class Application
{
private:
    TaskHandle_t fetchTaskHandle;
    // Initialization steps
    void initNVS();
    void initHardware();
    void initQueues();
    void initTasks();

    static void fetchProductsTask(void *param);
    void mainLoop();

public:
    Application() = default;
    ~Application() = default; // Not strictly needed in embedded reset model

    void run();
};
