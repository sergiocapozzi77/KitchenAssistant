#pragma once

#include "freertos/FreeRTOS.h" // MUST be first FreeRTOS header
#include "freertos/task.h"
#include "freertos/queue.h"

#include "WiFiManager.h"

class Application
{
private:
    // Initialization steps
    void initNVS();
    void initHardware();
    void initQueues();
    void initTasks();
    void mainLoop();

public:
    Application() = default;
    ~Application() = default; // Not strictly needed in embedded reset model

    void run();
};
