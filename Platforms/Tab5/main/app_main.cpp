/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/hal_esp32.h"
#include <app.h>
#include <hal/hal.h>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" void app_main(void)
{
    // HAL injection callback
    app::InitCallback_t callback;

    callback.onHalInjection = []() {
        hal::Inject(std::make_unique<HalEsp32>());
    };

    // Launch Howizard
    app::Init(callback);
    while (!app::IsDone()) {
        app::Update();
        vTaskDelay(1);
    }
    app::Destroy();
}
