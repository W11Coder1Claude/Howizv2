/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <lvgl.h>
#include <stdint.h>

/**
 * @brief Wizard-themed splash screen for Howizard
 * Dark mystical background with animated title and sparkle effects
 */
class AppWizardSplash : public mooncake::AppAbility {
public:
    AppWizardSplash();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    // UI elements
    lv_obj_t* _bg           = nullptr;
    lv_obj_t* _title_label  = nullptr;
    lv_obj_t* _sub_label    = nullptr;
    lv_obj_t* _version_label = nullptr;

    // Stars / sparkle particles
    static constexpr int NUM_STARS = 40;
    struct Star {
        lv_obj_t* obj = nullptr;
        int16_t baseX;
        int16_t baseY;
        uint8_t baseOpa;
        uint8_t speed;      // twinkle speed factor
        uint16_t phase;     // phase offset for animation
    };
    Star _stars[NUM_STARS];

    // Animation state
    uint32_t _start_time    = 0;
    uint32_t _frame_count   = 0;
    uint8_t  _title_opa     = 0;
    bool     _title_visible = false;
    bool     _closing       = false;

    static constexpr uint32_t SPLASH_DURATION_MS = 3000;  // Auto-close after 3s

    void create_starfield();
    void create_title();
    void update_starfield();
    void update_title_glow();
};
