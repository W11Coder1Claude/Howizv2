/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <lvgl.h>

// Forward declaration
class WizardUI;

/**
 * @brief Wizard-themed audio control application
 *
 * Manages the AudioEngine lifecycle and presents a wizard-themed UI
 * for controlling audio processing parameters (filters, EQ, output).
 */
class AppAudioControl : public mooncake::AppAbility {
public:
    AppAudioControl();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    WizardUI* _ui = nullptr;
    uint32_t _frameCount = 0;
};
