/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_wizard_splash.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <cstdlib>
#include <cmath>

using namespace mooncake;

static const std::string _tag = "WizardSplash";

// Color palette - deep mystical purple/blue theme
static constexpr uint32_t COLOR_BG_DARK     = 0x0A0A1A;  // Near-black with blue tint
static constexpr uint32_t COLOR_BG_GRAD     = 0x1A0A2E;  // Deep purple
static constexpr uint32_t COLOR_TITLE       = 0xE8D5B5;  // Warm gold/parchment
static constexpr uint32_t COLOR_TITLE_GLOW  = 0xFFD700;  // Bright gold
static constexpr uint32_t COLOR_SUBTITLE    = 0x8B7EC8;  // Soft lavender
static constexpr uint32_t COLOR_VERSION     = 0x4A4A6A;  // Muted blue-gray
static constexpr uint32_t COLOR_STAR_BRIGHT = 0xCCCCFF;  // Cool white-blue
static constexpr uint32_t COLOR_STAR_DIM    = 0x6666AA;  // Dim purple
static constexpr uint32_t COLOR_STAR_WARM   = 0xFFCC88;  // Warm amber star

#define FIRMWARE_VERSION "v0.1.0"

// Simple pseudo-random for star placement (deterministic seed)
static uint32_t _rng_state = 42;
static uint32_t fast_rand()
{
    _rng_state ^= _rng_state << 13;
    _rng_state ^= _rng_state >> 17;
    _rng_state ^= _rng_state << 5;
    return _rng_state;
}

AppWizardSplash::AppWizardSplash()
{
    setAppInfo().name = "AppWizardSplash";
}

void AppWizardSplash::onCreate()
{
    mclog::tagInfo(_tag, "on create");
    open();
}

void AppWizardSplash::onOpen()
{
    mclog::tagInfo(_tag, "on open");

    LvglLockGuard lock;

    // Dark background with gradient feel
    _bg = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(_bg);
    lv_obj_set_size(_bg, 1280, 720);
    lv_obj_set_style_bg_color(_bg, lv_color_hex(COLOR_BG_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(_bg, lv_color_hex(COLOR_BG_GRAD), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(_bg, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_bg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(_bg, LV_OBJ_FLAG_SCROLLABLE);

    // Create star field first (behind text)
    create_starfield();

    // Create title elements
    create_title();

    _start_time  = GetHAL()->millis();
    _frame_count = 0;
    _title_opa   = 0;
    _title_visible = false;

    mclog::tagInfo(_tag, "Howizard splash screen initialized");
}

void AppWizardSplash::create_starfield()
{
    _rng_state = 42;  // Reset for deterministic placement

    for (int i = 0; i < NUM_STARS; i++) {
        Star& s = _stars[i];

        // Random position across the screen
        s.baseX   = fast_rand() % 1280;
        s.baseY   = fast_rand() % 720;
        s.baseOpa = 40 + (fast_rand() % 180);   // Varying base brightness
        s.speed   = 1 + (fast_rand() % 4);       // Twinkle speed
        s.phase   = fast_rand() % 360;            // Phase offset

        // Create a small square for each star (no radius = less memory)
        s.obj = lv_obj_create(_bg);
        lv_obj_remove_style_all(s.obj);

        // Star size: mostly small, a few larger
        int size = (fast_rand() % 10 < 7) ? 2 : 4;
        lv_obj_set_size(s.obj, size, size);

        // Star color varies
        uint32_t color;
        uint8_t colorRoll = fast_rand() % 10;
        if (colorRoll < 5) {
            color = COLOR_STAR_BRIGHT;
        } else if (colorRoll < 8) {
            color = COLOR_STAR_DIM;
        } else {
            color = COLOR_STAR_WARM;
        }
        lv_obj_set_style_bg_color(s.obj, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s.obj, s.baseOpa, LV_PART_MAIN);

        lv_obj_set_pos(s.obj, s.baseX, s.baseY);
        lv_obj_remove_flag(s.obj, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void AppWizardSplash::create_title()
{
    // Main title: "HOWIZARD"
    _title_label = lv_label_create(_bg);
    lv_label_set_text(_title_label, "HOWIZARD");
    lv_obj_set_style_text_font(_title_label, &lv_font_montserrat_44, LV_PART_MAIN);
    lv_obj_set_style_text_color(_title_label, lv_color_hex(COLOR_TITLE), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(_title_label, 12, LV_PART_MAIN);
    lv_obj_set_style_text_opa(_title_label, 0, LV_PART_MAIN);
    lv_obj_align(_title_label, LV_ALIGN_CENTER, 0, -40);

    // Subtitle line
    _sub_label = lv_label_create(_bg);
    lv_label_set_text(_sub_label, "- conjuring audio magic -");
    lv_obj_set_style_text_font(_sub_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(_sub_label, lv_color_hex(COLOR_SUBTITLE), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(_sub_label, 4, LV_PART_MAIN);
    lv_obj_set_style_text_opa(_sub_label, 0, LV_PART_MAIN);
    lv_obj_align(_sub_label, LV_ALIGN_CENTER, 0, 30);

    // Version at bottom
    _version_label = lv_label_create(_bg);
    lv_label_set_text(_version_label, FIRMWARE_VERSION);
    lv_obj_set_style_text_font(_version_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(_version_label, lv_color_hex(COLOR_VERSION), LV_PART_MAIN);
    lv_obj_set_style_text_opa(_version_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(_version_label, LV_ALIGN_BOTTOM_RIGHT, -30, -20);

    // Decorative lines above and below title
    lv_obj_t* line_top = lv_obj_create(_bg);
    lv_obj_remove_style_all(line_top);
    lv_obj_set_size(line_top, 400, 2);
    lv_obj_set_style_bg_color(line_top, lv_color_hex(COLOR_SUBTITLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line_top, 100, LV_PART_MAIN);
    lv_obj_align(line_top, LV_ALIGN_CENTER, 0, -80);

    lv_obj_t* line_bot = lv_obj_create(_bg);
    lv_obj_remove_style_all(line_bot);
    lv_obj_set_size(line_bot, 300, 2);
    lv_obj_set_style_bg_color(line_bot, lv_color_hex(COLOR_SUBTITLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line_bot, 100, LV_PART_MAIN);
    lv_obj_align(line_bot, LV_ALIGN_CENTER, 0, 65);

    // Diamond decorations removed to save memory (rotation requires extra render buffer)
}

void AppWizardSplash::update_starfield()
{
    uint32_t elapsed = GetHAL()->millis() - _start_time;

    for (int i = 0; i < NUM_STARS; i++) {
        Star& s = _stars[i];

        // Sinusoidal twinkle: opacity oscillates around base value
        // Each star has different speed and phase for organic look
        float angle = (float)(elapsed * s.speed + s.phase * 100) * 0.003f;
        float wave  = sinf(angle);

        // Map wave [-1, 1] to opacity variation
        int opa = (int)s.baseOpa + (int)(wave * 60.0f);
        if (opa < 10) opa = 10;
        if (opa > 255) opa = 255;

        lv_obj_set_style_bg_opa(s.obj, (uint8_t)opa, LV_PART_MAIN);
    }
}

void AppWizardSplash::update_title_glow()
{
    uint32_t elapsed = GetHAL()->millis() - _start_time;

    // Phase 1: Fade in title after 500ms (over ~1500ms)
    if (elapsed > 500 && _title_opa < 255) {
        int progress = (int)(elapsed - 500);
        int opa = (progress * 255) / 1500;
        if (opa > 255) opa = 255;
        _title_opa = (uint8_t)opa;

        lv_obj_set_style_text_opa(_title_label, _title_opa, LV_PART_MAIN);

        // Subtitle fades in slightly delayed
        int sub_opa = ((progress - 400) * 255) / 1500;
        if (sub_opa < 0) sub_opa = 0;
        if (sub_opa > 255) sub_opa = 255;
        lv_obj_set_style_text_opa(_sub_label, (uint8_t)sub_opa, LV_PART_MAIN);
    }

    // Phase 2: Gentle golden pulse on the title (after fully visible)
    if (_title_opa >= 255) {
        float pulse = sinf((float)elapsed * 0.002f);
        // Oscillate between warm gold and bright gold
        uint8_t r = 232 + (uint8_t)(pulse * 23.0f);
        uint8_t g = 213 + (uint8_t)(pulse * 20.0f);
        uint8_t b = 181 - (uint8_t)(pulse * 20.0f);
        lv_obj_set_style_text_color(_title_label, lv_color_make(r, g, b), LV_PART_MAIN);
    }
}

void AppWizardSplash::onRunning()
{
    _frame_count++;

    // Auto-close after splash duration
    if (!_closing) {
        uint32_t elapsed = GetHAL()->millis() - _start_time;
        if (elapsed >= SPLASH_DURATION_MS) {
            _closing = true;
            mclog::tagInfo(_tag, "splash duration reached, transitioning to audio control");
            close();
            return;
        }
    }

    // Update every other frame to reduce CPU load
    if (_frame_count % 2 != 0) {
        return;
    }

    LvglLockGuard lock;

    update_starfield();
    update_title_glow();
}

void AppWizardSplash::onClose()
{
    mclog::tagInfo(_tag, "on close");

    LvglLockGuard lock;

    // Clean up all stars
    for (int i = 0; i < NUM_STARS; i++) {
        if (_stars[i].obj) {
            lv_obj_delete(_stars[i].obj);
            _stars[i].obj = nullptr;
        }
    }

    if (_bg) {
        lv_obj_delete(_bg);
        _bg = nullptr;
    }

    _title_label   = nullptr;
    _sub_label     = nullptr;
    _version_label = nullptr;
}
