/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#include "wizard_ui.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdio>
#include <cmath>

#ifdef ESP_PLATFORM
#include "hal/components/audio_engine.h"
#include "hal/components/profile_manager.h"
#endif

static const char* TAG = "WizardUI";

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::create(lv_obj_t* parent)
{
    mclog::tagInfo(TAG, "create() starting");

    // Root container - full screen dark background
    _root = lv_obj_create(parent);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(_root, lv_color_hex(BG_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    mclog::tagInfo(TAG, "creating header...");
    createHeader();
    mclog::tagInfo(TAG, "creating nav sidebar...");
    createNavSidebar();
    mclog::tagInfo(TAG, "creating content area...");
    createContentArea();
    mclog::tagInfo(TAG, "creating footer...");
    createFooter();

    mclog::tagInfo(TAG, "syncing UI to params...");
    // Sync UI controls to current engine params (handles profile autoload)
    syncUiToParams();

    mclog::tagInfo(TAG, "showing panel 0...");
    // Show filter panel by default
    showPanel(0);
    mclog::tagInfo(TAG, "updating mute button...");
    updateMuteButton();

    mclog::tagInfo(TAG, "wizard UI created");
}

void WizardUI::update()
{
    updateMeters();

    // Update headphone status
    bool hp = false;
    if (_hpStatusLabel) {
        hp = GetHAL()->headPhoneDetect();
        lv_label_set_text(_hpStatusLabel,
            hp ? "HP: Connected" : "HP: ---");
        lv_obj_set_style_text_color(_hpStatusLabel,
            lv_color_hex(hp ? GOLD_BRIGHT : MUTED_TEXT), LV_PART_MAIN);
    }

    // Update HP mic status on voice panel
    if (_veHpStatusLabel) {
        lv_label_set_text(_veHpStatusLabel,
            hp ? "HP MIC: Available" : "HP MIC: Not Available");
        lv_obj_set_style_text_color(_veHpStatusLabel,
            lv_color_hex(hp ? METER_GREEN : METER_RED), LV_PART_MAIN);
    }

    // Update HP mic level meter and VAD status
#ifdef ESP_PLATFORM
    {
        AudioLevels levels = AudioEngine::getInstance().getLevels();
        constexpr int HP_METER_MAX_W = 546;  // sliderW(550) - 4px borders
        constexpr float DB_MIN = -60.0f;

        auto levelToWidth = [](float level, float dbMin, int maxWidth) -> int {
            if (level < 0.00001f) return 0;
            float db = 20.0f * log10f(level);
            if (db < dbMin) return 0;
            float norm = (db - dbMin) / (0.0f - dbMin);
            return (int)(norm * maxWidth);
        };

        int wHP = levelToWidth(levels.rmsHP, DB_MIN, HP_METER_MAX_W);
        int pHP = levelToWidth(levels.peakHP, DB_MIN, HP_METER_MAX_W);

        if (_veHpMeterBar) {
            lv_obj_set_width(_veHpMeterBar, wHP > 0 ? wHP : 1);
            float db = 20.0f * log10f(levels.rmsHP + 0.00001f);
            uint32_t color = (db > -3.0f) ? METER_RED : (db > -20.0f) ? METER_GREEN : MUTED_TEXT;
            lv_obj_set_style_bg_color(_veHpMeterBar, lv_color_hex(color), LV_PART_MAIN);
        }
        if (_veHpMeterPeak) {
            lv_obj_set_x(_veHpMeterPeak, pHP > 2 ? pHP : 2);
        }

        // Update VAD status indicator
        if (_veVadStatusLabel) {
            if (levels.vadSpeechDetected) {
                lv_label_set_text(_veVadStatusLabel, "SPEECH");
                lv_obj_set_style_text_color(_veVadStatusLabel, lv_color_hex(METER_GREEN), LV_PART_MAIN);
            } else {
                lv_label_set_text(_veVadStatusLabel, "SILENCE");
                lv_obj_set_style_text_color(_veVadStatusLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
            }
        }

        // Update level match indicator (HP mic vs Main mic ratio)
        // Helps user calibrate ref gain for optimal VE performance
        if (_veLevelMatchIndicator && _veLevelMatchLabel) {
            // Calculate average RMS of main mics
            float mainRms = (levels.rmsLeft + levels.rmsRight) * 0.5f;
            float hpRms = levels.rmsHP;

            // Calculate ratio (HP/Main) - ideal is ~1.0
            float ratio = 0.0f;
            uint32_t indicatorColor = MUTED_TEXT;
            char ratioText[16] = "---";

            if (mainRms > 0.001f && hpRms > 0.001f) {
                ratio = hpRms / mainRms;

                // Determine color based on ratio
                if (ratio >= 0.8f && ratio <= 1.2f) {
                    // Good match
                    indicatorColor = METER_GREEN;
                } else if ((ratio >= 0.5f && ratio < 0.8f) || (ratio > 1.2f && ratio <= 2.0f)) {
                    // Needs adjustment
                    indicatorColor = METER_YELLOW;
                } else {
                    // Severe mismatch
                    indicatorColor = METER_RED;
                }

                snprintf(ratioText, sizeof(ratioText), "%.1fx", ratio);
            }

            lv_obj_set_style_bg_color(_veLevelMatchIndicator, lv_color_hex(indicatorColor), LV_PART_MAIN);
            lv_label_set_text(_veLevelMatchLabel, ratioText);
            lv_obj_set_style_text_color(_veLevelMatchLabel, lv_color_hex(indicatorColor), LV_PART_MAIN);
        }
    }
#endif
}

void WizardUI::destroy()
{
    if (_root) {
        lv_obj_delete(_root);
        _root = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Header bar
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createHeader()
{
    _headerBar = lv_obj_create(_root);
    lv_obj_remove_style_all(_headerBar);
    lv_obj_set_size(_headerBar, SCREEN_W, HEADER_H);
    lv_obj_set_pos(_headerBar, 0, 0);
    lv_obj_set_style_bg_color(_headerBar, lv_color_hex(BG_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_headerBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(_headerBar, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(_headerBar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(_headerBar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_remove_flag(_headerBar, LV_OBJ_FLAG_SCROLLABLE);

    // Title with diamond accents
    _titleLabel = lv_label_create(_headerBar);
    lv_label_set_text(_titleLabel, "HOWIZARD AUDIO ENCHANTMENT");
    lv_obj_set_style_text_font(_titleLabel, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(_titleLabel, lv_color_hex(GOLD), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(_titleLabel, 3, LV_PART_MAIN);
    lv_obj_align(_titleLabel, LV_ALIGN_LEFT_MID, 20, 0);

    // Mute button
    _muteBtn = lv_btn_create(_headerBar);
    lv_obj_set_size(_muteBtn, 110, 40);
    lv_obj_align(_muteBtn, LV_ALIGN_RIGHT_MID, -120, 0);
    lv_obj_set_style_bg_color(_muteBtn, lv_color_hex(METER_RED), LV_PART_MAIN);
    lv_obj_set_style_radius(_muteBtn, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(_muteBtn, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_set_style_border_width(_muteBtn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(_muteBtn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(_muteBtn, onMuteBtnClicked, LV_EVENT_CLICKED, this);

    _muteBtnLabel = lv_label_create(_muteBtn);
    lv_obj_set_style_text_font(_muteBtnLabel, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(_muteBtnLabel);

    // Version
    _versionLabel = lv_label_create(_headerBar);
    lv_label_set_text(_versionLabel, "v0.3");
    lv_obj_set_style_text_font(_versionLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_versionLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_align(_versionLabel, LV_ALIGN_RIGHT_MID, -20, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation sidebar
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createNavSidebar()
{
    _navPanel = lv_obj_create(_root);
    lv_obj_remove_style_all(_navPanel);
    lv_obj_set_size(_navPanel, NAV_W, CONTENT_H);
    lv_obj_set_pos(_navPanel, 0, HEADER_H);
    lv_obj_set_style_bg_color(_navPanel, lv_color_hex(BG_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_navPanel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(_navPanel, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(_navPanel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(_navPanel, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN);
    lv_obj_remove_flag(_navPanel, LV_OBJ_FLAG_SCROLLABLE);

    auto makeNavBtn = [&](const char* label, int y, int panelIdx) -> lv_obj_t* {
        lv_obj_t* btn = lv_btn_create(_navPanel);
        lv_obj_set_size(btn, NAV_W - 16, 58);
        lv_obj_set_pos(btn, 8, y);
        lv_obj_set_style_bg_color(btn, lv_color_hex(BG_DARK), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(LAVENDER), LV_PART_MAIN);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, onNavBtnClicked, LV_EVENT_CLICKED, this);
        // Store panel index in user_data of the label
        lv_obj_set_user_data(btn, (void*)(intptr_t)panelIdx);

        return btn;
    };

    int startY = 10;
    int btnStep = 68;  // Reduced to fit 6 buttons
    _navBtnFilter   = makeNavBtn("FILTER", startY, 0);
    _navBtnEq       = makeNavBtn("EQ", startY + btnStep, 1);
    _navBtnOutput   = makeNavBtn("OUTPUT", startY + btnStep * 2, 2);
    _navBtnVoice    = makeNavBtn("VOICE", startY + btnStep * 3, 3);
    _navBtnProfiles = makeNavBtn("PROF", startY + btnStep * 4, 4);
    _navBtnTinnitus = makeNavBtn("TINNI", startY + btnStep * 5, 5);

    // Decorative dividers between buttons
    for (int i = 0; i < 5; i++) {
        lv_obj_t* div = lv_obj_create(_navPanel);
        lv_obj_remove_style_all(div);
        lv_obj_set_size(div, NAV_W - 30, 1);
        lv_obj_set_pos(div, 15, startY + 62 + i * btnStep);
        lv_obj_set_style_bg_color(div, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(div, LV_OPA_COVER, LV_PART_MAIN);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Content area with three panels
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createContentArea()
{
    mclog::tagInfo(TAG, "  createContentArea: creating container...");
    _contentArea = lv_obj_create(_root);
    lv_obj_remove_style_all(_contentArea);
    lv_obj_set_size(_contentArea, CONTENT_W, CONTENT_H);
    lv_obj_set_pos(_contentArea, CONTENT_X, CONTENT_Y);
    lv_obj_set_style_bg_color(_contentArea, lv_color_hex(BG_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_contentArea, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(_contentArea, LV_OBJ_FLAG_SCROLLABLE);

    // Create all panels (same size, same position - visibility toggled)
    auto makePanel = [&]() -> lv_obj_t* {
        lv_obj_t* panel = lv_obj_create(_contentArea);
        lv_obj_remove_style_all(panel);
        lv_obj_set_size(panel, CONTENT_W, CONTENT_H);
        lv_obj_set_pos(panel, 0, 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
        return panel;
    };

    mclog::tagInfo(TAG, "  createContentArea: creating panel containers...");
    _panelFilter   = makePanel();
    _panelEq       = makePanel();
    _panelOutput   = makePanel();
    _panelVoice    = makePanel();
    _panelProfiles = makePanel();
    _panelTinnitus = makePanel();

    mclog::tagInfo(TAG, "  createContentArea: createFilterPanel...");
    createFilterPanel();
    mclog::tagInfo(TAG, "  createContentArea: createEqPanel...");
    createEqPanel();
    mclog::tagInfo(TAG, "  createContentArea: createOutputPanel...");
    createOutputPanel();
    mclog::tagInfo(TAG, "  createContentArea: createVoicePanel...");
    createVoicePanel();
    mclog::tagInfo(TAG, "  createContentArea: createProfilesPanel...");
    createProfilesPanel();
    mclog::tagInfo(TAG, "  createContentArea: createTinnitusPanel...");
    createTinnitusPanel();
    mclog::tagInfo(TAG, "  createContentArea: done");
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel 1: FILTERS (HPF + LPF)
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createFilterPanel()
{
    int cx = CONTENT_W / 2;  // Center x of content area

    // Panel title
    createSectionLabel(_panelFilter, "AUDIO FILTERS", cx - 80, 20);

    // Diamond divider
    createDiamondDivider(_panelFilter, 55, 400);

    // ── HPF Section ──
    createSectionLabel(_panelFilter, "HIGH-PASS FILTER", 80, 85);

    // HPF Toggle button
    _hpfToggle = lv_btn_create(_panelFilter);
    lv_obj_set_size(_hpfToggle, 130, 50);
    lv_obj_set_pos(_hpfToggle, 80, 120);
    styleToggleWizard(_hpfToggle);
    lv_obj_add_event_cb(_hpfToggle, onHpfToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* hpfToggleLbl = lv_label_create(_hpfToggle);
    lv_label_set_text(hpfToggleLbl, "HPF ON");
    lv_obj_set_style_text_font(hpfToggleLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(hpfToggleLbl);

    // HPF Frequency slider
    lv_obj_t* hpfFreqLabel = createValueLabel(_panelFilter, "Frequency:", 280, 130);
    (void)hpfFreqLabel;

    _hpfSlider = lv_slider_create(_panelFilter);
    lv_obj_set_size(_hpfSlider, 550, 30);
    lv_obj_set_pos(_hpfSlider, 400, 130);
    lv_slider_set_range(_hpfSlider, 20, 2000);
    lv_slider_set_value(_hpfSlider, 80, LV_ANIM_OFF);
    styleSliderWizard(_hpfSlider);
    lv_obj_add_event_cb(_hpfSlider, onHpfSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    _hpfValueLabel = createValueLabel(_panelFilter, "80 Hz", 970, 130);

    // ── Divider ──
    createDiamondDivider(_panelFilter, 210, 800);

    // ── LPF Section ──
    createSectionLabel(_panelFilter, "LOW-PASS FILTER", 80, 240);

    // LPF Toggle button
    _lpfToggle = lv_btn_create(_panelFilter);
    lv_obj_set_size(_lpfToggle, 130, 50);
    lv_obj_set_pos(_lpfToggle, 80, 275);
    styleToggleWizard(_lpfToggle);
    lv_obj_add_event_cb(_lpfToggle, onLpfToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* lpfToggleLbl = lv_label_create(_lpfToggle);
    lv_label_set_text(lpfToggleLbl, "LPF OFF");
    lv_obj_set_style_text_font(lpfToggleLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(lpfToggleLbl);

    // LPF Frequency slider
    lv_obj_t* lpfFreqLabel = createValueLabel(_panelFilter, "Frequency:", 280, 285);
    (void)lpfFreqLabel;

    _lpfSlider = lv_slider_create(_panelFilter);
    lv_obj_set_size(_lpfSlider, 550, 30);
    lv_obj_set_pos(_lpfSlider, 400, 285);
    lv_slider_set_range(_lpfSlider, 500, 20000);
    lv_slider_set_value(_lpfSlider, 18000, LV_ANIM_OFF);
    styleSliderWizard(_lpfSlider);
    lv_obj_add_event_cb(_lpfSlider, onLpfSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    _lpfValueLabel = createValueLabel(_panelFilter, "18000 Hz", 970, 285);

    // ── Divider ──
    createDiamondDivider(_panelFilter, 365, 800);

    // ── NS Section ──
    createSectionLabel(_panelFilter, "NOISE SUPPRESSION", 80, 390);

    // NS Toggle button
    _nsToggle = lv_btn_create(_panelFilter);
    lv_obj_set_size(_nsToggle, 130, 50);
    lv_obj_set_pos(_nsToggle, 80, 425);
    styleToggleWizard(_nsToggle);
    lv_obj_add_event_cb(_nsToggle, onNsToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* nsToggleLbl = lv_label_create(_nsToggle);
    lv_label_set_text(nsToggleLbl, "NS OFF");
    lv_obj_set_style_text_font(nsToggleLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(nsToggleLbl);

    // NS Mode label
    createValueLabel(_panelFilter, "Mode:", 280, 435);

    // NS Mode buttons (MILD / MEDIUM / AGGRESSIVE)
    auto makeNsModeBtn = [&](const char* label, int x, int modeIdx) -> lv_obj_t* {
        lv_obj_t* btn = lv_btn_create(_panelFilter);
        lv_obj_set_size(btn, 160, 50);
        lv_obj_set_pos(btn, x, 425);
        styleToggleWizard(btn);
        lv_obj_set_user_data(btn, (void*)(intptr_t)modeIdx);
        lv_obj_add_event_cb(btn, onNsModeClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(LAVENDER), LV_PART_MAIN);
        lv_obj_center(lbl);

        return btn;
    };

    _nsModeBtn0 = makeNsModeBtn("MILD", 380, 0);
    _nsModeBtn1 = makeNsModeBtn("MEDIUM", 560, 1);
    _nsModeBtn2 = makeNsModeBtn("AGGRESSIVE", 740, 2);

    // Highlight default mode (MEDIUM)
    _nsActiveMode = 1;
    lv_obj_set_style_border_color(_nsModeBtn1, lv_color_hex(CYAN_GLOW), LV_PART_MAIN);
    lv_obj_t* midChild = lv_obj_get_child(_nsModeBtn1, 0);
    if (midChild) lv_obj_set_style_text_color(midChild, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);

    // Decorative note at bottom
    lv_obj_t* noteLabel = lv_label_create(_panelFilter);
    lv_label_set_text(noteLabel, "Butterworth filters (Q = 0.707)  |  NS: ESP-SR WebRTC @ 16kHz");
    lv_obj_set_style_text_font(noteLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(noteLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(noteLabel, cx - 250, CONTENT_H - 60);
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel 2: EQUALIZER (3-band parametric)
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createEqPanel()
{
    int cx = CONTENT_W / 2;

    // Panel title
    createSectionLabel(_panelEq, "3-BAND PARAMETRIC EQ", cx - 120, 20);
    createDiamondDivider(_panelEq, 55, 400);

    // Layout: three vertical sliders spaced evenly
    struct EqBand {
        const char* freqLabel;
        const char* nameLabel;
        int xCenter;
        lv_obj_t** slider;
        lv_obj_t** valueLabel;
    };

    EqBand bands[] = {
        {"250 Hz",  "LOW",  cx - 300, &_eqLowSlider,  &_eqLowLabel},
        {"1000 Hz", "MID",  cx,       &_eqMidSlider,  &_eqMidLabel},
        {"4000 Hz", "HIGH", cx + 300, &_eqHighSlider,  &_eqHighLabel},
    };

    for (auto& band : bands) {
        // Band name
        lv_obj_t* nameLabel = lv_label_create(_panelEq);
        lv_label_set_text(nameLabel, band.nameLabel);
        lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(nameLabel, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
        lv_obj_set_style_text_letter_space(nameLabel, 3, LV_PART_MAIN);
        lv_obj_set_pos(nameLabel, band.xCenter - 25, 75);

        // +12dB label
        lv_obj_t* topLabel = lv_label_create(_panelEq);
        lv_label_set_text(topLabel, "+12");
        lv_obj_set_style_text_font(topLabel, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(topLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
        lv_obj_set_pos(topLabel, band.xCenter + 30, 115);

        // Vertical slider (range: -120 to +120, representing -12.0 to +12.0 dB)
        *(band.slider) = lv_slider_create(_panelEq);
        lv_obj_set_size(*(band.slider), 30, 350);
        lv_obj_set_pos(*(band.slider), band.xCenter - 15, 110);
        lv_slider_set_range(*(band.slider), -120, 120);
        lv_slider_set_value(*(band.slider), 0, LV_ANIM_OFF);
        styleSliderWizard(*(band.slider));
        lv_obj_add_event_cb(*(band.slider), onEqSliderChanged, LV_EVENT_VALUE_CHANGED, this);

        // -12dB label
        lv_obj_t* botLabel = lv_label_create(_panelEq);
        lv_label_set_text(botLabel, "-12");
        lv_obj_set_style_text_font(botLabel, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(botLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
        lv_obj_set_pos(botLabel, band.xCenter + 30, 448);

        // 0dB center line indicator
        lv_obj_t* centerLine = lv_obj_create(_panelEq);
        lv_obj_remove_style_all(centerLine);
        lv_obj_set_size(centerLine, 50, 1);
        lv_obj_set_pos(centerLine, band.xCenter - 25, 285);
        lv_obj_set_style_bg_color(centerLine, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(centerLine, LV_OPA_COVER, LV_PART_MAIN);

        // Value display
        *(band.valueLabel) = lv_label_create(_panelEq);
        lv_label_set_text(*(band.valueLabel), "0.0 dB");
        lv_obj_set_style_text_font(*(band.valueLabel), &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(*(band.valueLabel), lv_color_hex(GOLD), LV_PART_MAIN);
        lv_obj_set_pos(*(band.valueLabel), band.xCenter - 30, 475);

        // Frequency label
        lv_obj_t* freqLabel = lv_label_create(_panelEq);
        lv_label_set_text(freqLabel, band.freqLabel);
        lv_obj_set_style_text_font(freqLabel, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(freqLabel, lv_color_hex(LAVENDER), LV_PART_MAIN);
        lv_obj_set_pos(freqLabel, band.xCenter - 28, 500);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel 3: OUTPUT (Volume, Gain, Meters)
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createOutputPanel()
{
    int cx = CONTENT_W / 2;

    createSectionLabel(_panelOutput, "OUTPUT CONTROLS", cx - 90, 20);
    createDiamondDivider(_panelOutput, 55, 400);

    // ── Master Volume ──
    createSectionLabel(_panelOutput, "MASTER VOLUME", 60, 75);

    _volumeSlider = lv_slider_create(_panelOutput);
    lv_obj_set_size(_volumeSlider, 600, 28);
    lv_obj_set_pos(_volumeSlider, 250, 78);
    lv_slider_set_range(_volumeSlider, 0, 100);
    lv_slider_set_value(_volumeSlider, 100, LV_ANIM_OFF);
    styleSliderWizard(_volumeSlider);
    lv_obj_add_event_cb(_volumeSlider, onVolumeSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    _volumeValueLabel = createValueLabel(_panelOutput, "100", 870, 78);

    // ── Output Gain ──
    createSectionLabel(_panelOutput, "OUTPUT GAIN", 60, 120);

    _gainSlider = lv_slider_create(_panelOutput);
    lv_obj_set_size(_gainSlider, 500, 28);
    lv_obj_set_pos(_gainSlider, 250, 123);
    lv_slider_set_range(_gainSlider, 0, 600);  // 0.0 to 6.0 (extended for boost)
    lv_slider_set_value(_gainSlider, 150, LV_ANIM_OFF);
    styleSliderWizard(_gainSlider);
    lv_obj_add_event_cb(_gainSlider, onGainSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    _gainValueLabel = createValueLabel(_panelOutput, "1.50x", 770, 123);

    // Boost toggle
    _boostToggle = lv_btn_create(_panelOutput);
    lv_obj_set_size(_boostToggle, 100, 36);
    lv_obj_set_pos(_boostToggle, 870, 118);
    styleToggleWizard(_boostToggle);
    lv_obj_add_event_cb(_boostToggle, onBoostToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* boostLbl = lv_label_create(_boostToggle);
    lv_label_set_text(boostLbl, "BOOST");
    lv_obj_set_style_text_font(boostLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(boostLbl, lv_color_hex(LAVENDER), LV_PART_MAIN);
    lv_obj_center(boostLbl);

    // Boost warning label (hidden by default)
    _boostWarningLabel = lv_label_create(_panelOutput);
    lv_label_set_text(_boostWarningLabel, "Soft limiting active");
    lv_obj_set_style_text_font(_boostWarningLabel, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(_boostWarningLabel, lv_color_hex(METER_YELLOW), LV_PART_MAIN);
    lv_obj_set_pos(_boostWarningLabel, 980, 123);
    lv_obj_add_flag(_boostWarningLabel, LV_OBJ_FLAG_HIDDEN);

    // ── Mic Gain ──
    createSectionLabel(_panelOutput, "MIC GAIN", 60, 165);

    _micGainSlider = lv_slider_create(_panelOutput);
    lv_obj_set_size(_micGainSlider, 600, 28);
    lv_obj_set_pos(_micGainSlider, 250, 168);
    lv_slider_set_range(_micGainSlider, 0, 240);
    lv_slider_set_value(_micGainSlider, 180, LV_ANIM_OFF);
    styleSliderWizard(_micGainSlider);
    lv_obj_add_event_cb(_micGainSlider, onMicGainSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    _micGainValueLabel = createValueLabel(_panelOutput, "180", 870, 168);

    // ── AGC Divider ──
    createDiamondDivider(_panelOutput, 210, 800);

    // ══════════════════════════════════════════════════════════════════════
    // AGC Section
    // ══════════════════════════════════════════════════════════════════════
    createSectionLabel(_panelOutput, "AUTO GAIN CONTROL", 60, 225);

    // AGC Toggle button
    _agcToggle = lv_btn_create(_panelOutput);
    lv_obj_set_size(_agcToggle, 130, 42);
    lv_obj_set_pos(_agcToggle, 60, 255);
    styleToggleWizard(_agcToggle);
    lv_obj_add_event_cb(_agcToggle, onAgcToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* agcToggleLbl = lv_label_create(_agcToggle);
    lv_label_set_text(agcToggleLbl, "AGC OFF");
    lv_obj_set_style_text_font(agcToggleLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(agcToggleLbl);

    // Mode label
    createValueLabel(_panelOutput, "Mode:", 220, 265);

    // AGC Mode buttons (SAT / ANA / DIG / FIX)
    auto makeAgcModeBtn = [&](const char* label, int x, int modeIdx) -> lv_obj_t* {
        lv_obj_t* btn = lv_btn_create(_panelOutput);
        lv_obj_set_size(btn, 110, 42);
        lv_obj_set_pos(btn, x, 255);
        styleToggleWizard(btn);
        lv_obj_set_user_data(btn, (void*)(intptr_t)modeIdx);
        lv_obj_add_event_cb(btn, onAgcModeClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(LAVENDER), LV_PART_MAIN);
        lv_obj_center(lbl);

        return btn;
    };

    _agcModeBtn0 = makeAgcModeBtn("SAT", 300, 0);
    _agcModeBtn1 = makeAgcModeBtn("ANA", 420, 1);
    _agcModeBtn2 = makeAgcModeBtn("DIG", 540, 2);
    _agcModeBtn3 = makeAgcModeBtn("FIX", 660, 3);

    // Highlight default mode (DIG)
    _agcActiveMode = 2;
    lv_obj_set_style_border_color(_agcModeBtn2, lv_color_hex(CYAN_GLOW), LV_PART_MAIN);
    lv_obj_t* digChild = lv_obj_get_child(_agcModeBtn2, 0);
    if (digChild) lv_obj_set_style_text_color(digChild, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);

    // Compression Gain slider
    createValueLabel(_panelOutput, "Gain:", 60, 310);

    _agcGainSlider = lv_slider_create(_panelOutput);
    lv_obj_set_size(_agcGainSlider, 550, 26);
    lv_obj_set_pos(_agcGainSlider, 250, 312);
    lv_slider_set_range(_agcGainSlider, 0, 90);
    lv_slider_set_value(_agcGainSlider, 9, LV_ANIM_OFF);
    styleSliderWizard(_agcGainSlider);
    lv_obj_add_event_cb(_agcGainSlider, onAgcGainChanged, LV_EVENT_VALUE_CHANGED, this);

    _agcGainValueLabel = createValueLabel(_panelOutput, "9 dB", 820, 310);

    // Target Level slider
    createValueLabel(_panelOutput, "Target:", 60, 350);

    _agcTargetSlider = lv_slider_create(_panelOutput);
    lv_obj_set_size(_agcTargetSlider, 550, 26);
    lv_obj_set_pos(_agcTargetSlider, 250, 352);
    lv_slider_set_range(_agcTargetSlider, -31, 0);
    lv_slider_set_value(_agcTargetSlider, -3, LV_ANIM_OFF);
    styleSliderWizard(_agcTargetSlider);
    lv_obj_add_event_cb(_agcTargetSlider, onAgcTargetChanged, LV_EVENT_VALUE_CHANGED, this);

    _agcTargetValueLabel = createValueLabel(_panelOutput, "-3 dBFS", 820, 350);

    // Limiter toggle
    _agcLimiterToggle = lv_btn_create(_panelOutput);
    lv_obj_set_size(_agcLimiterToggle, 120, 40);
    lv_obj_set_pos(_agcLimiterToggle, 870, 345);
    styleToggleWizard(_agcLimiterToggle);
    lv_obj_set_style_border_color(_agcLimiterToggle, lv_color_hex(CYAN_GLOW), LV_PART_MAIN);
    lv_obj_add_event_cb(_agcLimiterToggle, onAgcLimiterToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* limToggleLbl = lv_label_create(_agcLimiterToggle);
    lv_label_set_text(limToggleLbl, "LIM ON");
    lv_obj_set_style_text_font(limToggleLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(limToggleLbl, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_center(limToggleLbl);

    // ── VU Meters Divider ──
    createDiamondDivider(_panelOutput, 395, 800);

    // ── VU Meters ──
    createSectionLabel(_panelOutput, "LEVEL METERS", 60, 410);

    // Left meter label
    lv_obj_t* lblL = lv_label_create(_panelOutput);
    lv_label_set_text(lblL, "L");
    lv_obj_set_style_text_font(lblL, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lblL, lv_color_hex(GOLD), LV_PART_MAIN);
    lv_obj_set_pos(lblL, 60, 440);

    // Left meter bar background
    lv_obj_t* meterBgL = lv_obj_create(_panelOutput);
    lv_obj_remove_style_all(meterBgL);
    lv_obj_set_size(meterBgL, 700, 30);
    lv_obj_set_pos(meterBgL, 90, 437);
    lv_obj_set_style_bg_color(meterBgL, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(meterBgL, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(meterBgL, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(meterBgL, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(meterBgL, 1, LV_PART_MAIN);
    lv_obj_remove_flag(meterBgL, LV_OBJ_FLAG_SCROLLABLE);

    _meterBarL = lv_obj_create(meterBgL);
    lv_obj_remove_style_all(_meterBarL);
    lv_obj_set_size(_meterBarL, 0, 26);
    lv_obj_set_pos(_meterBarL, 2, 2);
    lv_obj_set_style_bg_color(_meterBarL, lv_color_hex(METER_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_meterBarL, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_meterBarL, 2, LV_PART_MAIN);

    _meterPeakL = lv_obj_create(meterBgL);
    lv_obj_remove_style_all(_meterPeakL);
    lv_obj_set_size(_meterPeakL, 3, 26);
    lv_obj_set_pos(_meterPeakL, 2, 2);
    lv_obj_set_style_bg_color(_meterPeakL, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_meterPeakL, LV_OPA_COVER, LV_PART_MAIN);

    // Right meter label
    lv_obj_t* lblR = lv_label_create(_panelOutput);
    lv_label_set_text(lblR, "R");
    lv_obj_set_style_text_font(lblR, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lblR, lv_color_hex(GOLD), LV_PART_MAIN);
    lv_obj_set_pos(lblR, 60, 478);

    // Right meter bar background
    lv_obj_t* meterBgR = lv_obj_create(_panelOutput);
    lv_obj_remove_style_all(meterBgR);
    lv_obj_set_size(meterBgR, 700, 30);
    lv_obj_set_pos(meterBgR, 90, 475);
    lv_obj_set_style_bg_color(meterBgR, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(meterBgR, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(meterBgR, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(meterBgR, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(meterBgR, 1, LV_PART_MAIN);
    lv_obj_remove_flag(meterBgR, LV_OBJ_FLAG_SCROLLABLE);

    _meterBarR = lv_obj_create(meterBgR);
    lv_obj_remove_style_all(_meterBarR);
    lv_obj_set_size(_meterBarR, 0, 26);
    lv_obj_set_pos(_meterBarR, 2, 2);
    lv_obj_set_style_bg_color(_meterBarR, lv_color_hex(METER_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_meterBarR, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_meterBarR, 2, LV_PART_MAIN);

    _meterPeakR = lv_obj_create(meterBgR);
    lv_obj_remove_style_all(_meterPeakR);
    lv_obj_set_size(_meterPeakR, 3, 26);
    lv_obj_set_pos(_meterPeakR, 2, 2);
    lv_obj_set_style_bg_color(_meterPeakR, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_meterPeakR, LV_OPA_COVER, LV_PART_MAIN);

    // Meter scale labels
    const char* scaleLabels[] = {"-60", "-40", "-20", "-10", "-6", "-3", "0"};
    int scalePositions[] = {90, 190, 340, 460, 545, 640, 700};
    for (int i = 0; i < 7; i++) {
        lv_obj_t* sl = lv_label_create(_panelOutput);
        lv_label_set_text(sl, scaleLabels[i]);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(sl, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
        lv_obj_set_pos(sl, scalePositions[i], 510);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel 4: VOICE EXCLUSION (NLMS only - AEC removed to save LVGL memory)
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createVoicePanel()
{
    int sliderX = 300;
    int sliderW = 550;
    int valX = 870;

    // ── Row 1: VE Toggle + HP Status ──
    _veToggle = lv_btn_create(_panelVoice);
    lv_obj_set_size(_veToggle, 100, 36);
    lv_obj_set_pos(_veToggle, 60, 8);
    styleToggleWizard(_veToggle);
    lv_obj_add_event_cb(_veToggle, onVeToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* veToggleLbl = lv_label_create(_veToggle);
    lv_label_set_text(veToggleLbl, "VE OFF");
    lv_obj_set_style_text_font(veToggleLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(veToggleLbl);

    _veHpStatusLabel = lv_label_create(_panelVoice);
    lv_label_set_text(_veHpStatusLabel, "HP MIC: ---");
    lv_obj_set_style_text_font(_veHpStatusLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_veHpStatusLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(_veHpStatusLabel, 700, 15);

    createDiamondDivider(_panelVoice, 50, 800);

    // ══════════════════════════════════════════════════════════════════════
    // Reference Signal Controls
    // ══════════════════════════════════════════════════════════════════════
    createSectionLabel(_panelVoice, "REFERENCE SIGNAL (HP MIC)", 60, 58);

    createValueLabel(_panelVoice, "Gain:", 60, 88);
    _veRefGainSlider = lv_slider_create(_panelVoice);
    lv_obj_set_size(_veRefGainSlider, sliderW, 24);
    lv_obj_set_pos(_veRefGainSlider, sliderX, 88);
    lv_slider_set_range(_veRefGainSlider, 1, 50);
    lv_slider_set_value(_veRefGainSlider, 10, LV_ANIM_OFF);
    styleSliderWizard(_veRefGainSlider);
    lv_obj_add_event_cb(_veRefGainSlider, onVeRefGainChanged, LV_EVENT_VALUE_CHANGED, this);
    _veRefGainValueLabel = createValueLabel(_panelVoice, "1.0x", valX, 88);

    createValueLabel(_panelVoice, "HPF:", 60, 120);
    _veRefHpfSlider = lv_slider_create(_panelVoice);
    lv_obj_set_size(_veRefHpfSlider, sliderW, 24);
    lv_obj_set_pos(_veRefHpfSlider, sliderX, 120);
    lv_slider_set_range(_veRefHpfSlider, 20, 500);
    lv_slider_set_value(_veRefHpfSlider, 80, LV_ANIM_OFF);
    styleSliderWizard(_veRefHpfSlider);
    lv_obj_add_event_cb(_veRefHpfSlider, onVeRefHpfChanged, LV_EVENT_VALUE_CHANGED, this);
    _veRefHpfValueLabel = createValueLabel(_panelVoice, "80 Hz", valX, 120);

    createValueLabel(_panelVoice, "LPF:", 60, 152);
    _veRefLpfSlider = lv_slider_create(_panelVoice);
    lv_obj_set_size(_veRefLpfSlider, sliderW, 24);
    lv_obj_set_pos(_veRefLpfSlider, sliderX, 152);
    lv_slider_set_range(_veRefLpfSlider, 1000, 8000);
    lv_slider_set_value(_veRefLpfSlider, 4000, LV_ANIM_OFF);
    styleSliderWizard(_veRefLpfSlider);
    lv_obj_add_event_cb(_veRefLpfSlider, onVeRefLpfChanged, LV_EVENT_VALUE_CHANGED, this);
    _veRefLpfValueLabel = createValueLabel(_panelVoice, "4000 Hz", valX, 152);

    // HP Mic Level Meter
    createValueLabel(_panelVoice, "Level:", 60, 184);
    lv_obj_t* hpMeterBg = lv_obj_create(_panelVoice);
    lv_obj_remove_style_all(hpMeterBg);
    lv_obj_set_size(hpMeterBg, sliderW, 22);
    lv_obj_set_pos(hpMeterBg, sliderX, 184);
    lv_obj_set_style_bg_color(hpMeterBg, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hpMeterBg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(hpMeterBg, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(hpMeterBg, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(hpMeterBg, 1, LV_PART_MAIN);
    lv_obj_remove_flag(hpMeterBg, LV_OBJ_FLAG_SCROLLABLE);

    _veHpMeterBar = lv_obj_create(hpMeterBg);
    lv_obj_remove_style_all(_veHpMeterBar);
    lv_obj_set_size(_veHpMeterBar, 1, 18);
    lv_obj_set_pos(_veHpMeterBar, 2, 2);
    lv_obj_set_style_bg_color(_veHpMeterBar, lv_color_hex(METER_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_veHpMeterBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_veHpMeterBar, 2, LV_PART_MAIN);

    _veHpMeterPeak = lv_obj_create(hpMeterBg);
    lv_obj_remove_style_all(_veHpMeterPeak);
    lv_obj_set_size(_veHpMeterPeak, 3, 18);
    lv_obj_set_pos(_veHpMeterPeak, 2, 2);
    lv_obj_set_style_bg_color(_veHpMeterPeak, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_veHpMeterPeak, LV_OPA_COVER, LV_PART_MAIN);

    // Level Match Indicator (HP mic vs Main mic ratio)
    // Green = good match (0.8-1.2), Yellow = adjust needed (0.5-0.8 or 1.2-2.0), Red = severe mismatch
    _veLevelMatchIndicator = lv_obj_create(_panelVoice);
    lv_obj_remove_style_all(_veLevelMatchIndicator);
    lv_obj_set_size(_veLevelMatchIndicator, 16, 16);
    lv_obj_set_pos(_veLevelMatchIndicator, valX + 80, 186);
    lv_obj_set_style_bg_color(_veLevelMatchIndicator, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_veLevelMatchIndicator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_veLevelMatchIndicator, 8, LV_PART_MAIN);  // Circle

    _veLevelMatchLabel = lv_label_create(_panelVoice);
    lv_label_set_text(_veLevelMatchLabel, "---");
    lv_obj_set_style_text_font(_veLevelMatchLabel, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(_veLevelMatchLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(_veLevelMatchLabel, valX + 100, 187);

    // Blend
    createValueLabel(_panelVoice, "Blend:", 60, 216);
    _veBlendSlider = lv_slider_create(_panelVoice);
    lv_obj_set_size(_veBlendSlider, sliderW, 24);
    lv_obj_set_pos(_veBlendSlider, sliderX, 216);
    lv_slider_set_range(_veBlendSlider, 0, 100);
    lv_slider_set_value(_veBlendSlider, 70, LV_ANIM_OFF);
    styleSliderWizard(_veBlendSlider);
    lv_obj_add_event_cb(_veBlendSlider, onVeBlendChanged, LV_EVENT_VALUE_CHANGED, this);
    _veBlendValueLabel = createValueLabel(_panelVoice, "70%", valX, 216);

    createDiamondDivider(_panelVoice, 252, 800);

    // ══════════════════════════════════════════════════════════════════════
    // NLMS Adaptive Filter Controls (flattened - no container)
    // ══════════════════════════════════════════════════════════════════════
    createSectionLabel(_panelVoice, "NLMS ADAPTIVE FILTER", 60, 260);

    createValueLabel(_panelVoice, "Step:", 60, 292);
    _veStepSlider = lv_slider_create(_panelVoice);
    lv_obj_set_size(_veStepSlider, sliderW, 24);
    lv_obj_set_pos(_veStepSlider, sliderX, 292);
    lv_slider_set_range(_veStepSlider, 1, 100);
    lv_slider_set_value(_veStepSlider, 10, LV_ANIM_OFF);
    styleSliderWizard(_veStepSlider);
    lv_obj_add_event_cb(_veStepSlider, onVeStepChanged, LV_EVENT_VALUE_CHANGED, this);
    _veStepValueLabel = createValueLabel(_panelVoice, "0.10", valX, 292);

    createValueLabel(_panelVoice, "Max Atten:", 60, 324);
    _veAttenSlider = lv_slider_create(_panelVoice);
    lv_obj_set_size(_veAttenSlider, sliderW, 24);
    lv_obj_set_pos(_veAttenSlider, sliderX, 324);
    lv_slider_set_range(_veAttenSlider, 0, 100);
    lv_slider_set_value(_veAttenSlider, 80, LV_ANIM_OFF);
    styleSliderWizard(_veAttenSlider);
    lv_obj_add_event_cb(_veAttenSlider, onVeAttenChanged, LV_EVENT_VALUE_CHANGED, this);
    _veAttenValueLabel = createValueLabel(_panelVoice, "80%", valX, 324);

    createValueLabel(_panelVoice, "Taps:", 60, 360);

    auto makeFilterBtn = [&](const char* label, int x, int taps) -> lv_obj_t* {
        lv_obj_t* btn = lv_btn_create(_panelVoice);
        lv_obj_set_size(btn, 130, 38);
        lv_obj_set_pos(btn, x, 356);
        styleToggleWizard(btn);
        lv_obj_set_user_data(btn, (void*)(intptr_t)taps);
        lv_obj_add_event_cb(btn, onVeFilterLenClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(LAVENDER), LV_PART_MAIN);
        lv_obj_center(lbl);

        return btn;
    };

    _veFilterBtn32  = makeFilterBtn("64", sliderX, 64);
    _veFilterBtn64  = makeFilterBtn("128", sliderX + 150, 128);
    _veFilterBtn128 = makeFilterBtn("256", sliderX + 300, 256);

    _veActiveFilterLen = 128;
    lv_obj_set_style_border_color(_veFilterBtn64, lv_color_hex(CYAN_GLOW), LV_PART_MAIN);
    lv_obj_t* defChild = lv_obj_get_child(_veFilterBtn64, 0);
    if (defChild) lv_obj_set_style_text_color(defChild, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);

    createDiamondDivider(_panelVoice, 400, 800);

    // ══════════════════════════════════════════════════════════════════════
    // VAD Gating Controls (attenuate during non-speech, works with both modes)
    // ══════════════════════════════════════════════════════════════════════
    createSectionLabel(_panelVoice, "VAD GATING", 60, 410);

    _veVadGateToggle = lv_btn_create(_panelVoice);
    lv_obj_set_size(_veVadGateToggle, 100, 36);
    lv_obj_set_pos(_veVadGateToggle, 60, 445);
    styleToggleWizard(_veVadGateToggle);
    lv_obj_set_style_border_color(_veVadGateToggle, lv_color_hex(CYAN_GLOW), LV_PART_MAIN);
    lv_obj_add_event_cb(_veVadGateToggle, onVeVadGateToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* vadGateLbl = lv_label_create(_veVadGateToggle);
    lv_label_set_text(vadGateLbl, "GATE ON");
    lv_obj_set_style_text_font(vadGateLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(vadGateLbl, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_center(vadGateLbl);

    createValueLabel(_panelVoice, "Silence Atten:", 200, 453);
    _veVadGateAttenSlider = lv_slider_create(_panelVoice);
    lv_obj_set_size(_veVadGateAttenSlider, 400, 24);
    lv_obj_set_pos(_veVadGateAttenSlider, sliderX, 450);
    lv_slider_set_range(_veVadGateAttenSlider, 0, 50);  // 0-50% (0.0-0.5)
    lv_slider_set_value(_veVadGateAttenSlider, 15, LV_ANIM_OFF);  // 15% = -16dB
    styleSliderWizard(_veVadGateAttenSlider);
    lv_obj_add_event_cb(_veVadGateAttenSlider, onVeVadGateAttenChanged, LV_EVENT_VALUE_CHANGED, this);
    _veVadGateAttenValueLabel = createValueLabel(_panelVoice, "15% (-16dB)", 720, 450);

    // VAD status indicator
    _veVadStatusLabel = lv_label_create(_panelVoice);
    lv_label_set_text(_veVadStatusLabel, "SILENCE");
    lv_obj_set_style_text_font(_veVadStatusLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_veVadStatusLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(_veVadStatusLabel, 850, 453);

    lv_obj_t* nlmsNote = lv_label_create(_panelVoice);
    lv_label_set_text(nlmsNote, "VAD gate works with both NLMS & AEC  |  Reduces transients during silence  |  Match indicator: aim for green");
    lv_obj_set_style_text_font(nlmsNote, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(nlmsNote, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(nlmsNote, 60, 495);
}

void WizardUI::updateVoiceModeVisibility()
{
    // AEC mode UI removed to save LVGL memory - only NLMS mode available
    // Engine still supports AEC, but no UI controls for it
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel 5: PROFILES (SD Card)
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createProfilesPanel()
{
    // Minimal Profiles panel - just status and refresh button to save LVGL memory
    int cx = CONTENT_W / 2;

    createSectionLabel(_panelProfiles, "SD CARD / PROFILES", cx - 120, 20);

    // Status label - shows SD card and profile status
    _profileStatusLabel = lv_label_create(_panelProfiles);
    lv_label_set_text(_profileStatusLabel, "Checking SD card...");
    lv_obj_set_style_text_font(_profileStatusLabel, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(_profileStatusLabel, lv_color_hex(GOLD), LV_PART_MAIN);
    lv_obj_set_pos(_profileStatusLabel, 60, 80);

    // Default profile indicator
    _profileDefaultLabel = lv_label_create(_panelProfiles);
    lv_label_set_text(_profileDefaultLabel, "Default: (none)");
    lv_obj_set_style_text_font(_profileDefaultLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_profileDefaultLabel, lv_color_hex(LAVENDER), LV_PART_MAIN);
    lv_obj_set_pos(_profileDefaultLabel, 60, 120);

    // Refresh button - check SD card status
    _profileLoadBtn = lv_btn_create(_panelProfiles);
    lv_obj_set_size(_profileLoadBtn, 180, 50);
    lv_obj_set_pos(_profileLoadBtn, 60, 180);
    styleToggleWizard(_profileLoadBtn);
    lv_obj_set_style_border_color(_profileLoadBtn, lv_color_hex(GOLD), LV_PART_MAIN);
    lv_obj_add_event_cb(_profileLoadBtn, onProfileLoad, LV_EVENT_CLICKED, this);

    lv_obj_t* refreshLbl = lv_label_create(_profileLoadBtn);
    lv_label_set_text(refreshLbl, "REFRESH");
    lv_obj_set_style_text_font(refreshLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(refreshLbl, lv_color_hex(LAVENDER), LV_PART_MAIN);
    lv_obj_center(refreshLbl);

    // Note: Full profile management (save/load/delete) temporarily disabled
    // to conserve LVGL memory. Profiles are auto-loaded on boot if set.
    _profileRoller = nullptr;
    _profileNameInput = nullptr;
    _profileSaveBtn = nullptr;
    _profileDeleteBtn = nullptr;
    _profileSetDefaultBtn = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel 6: TINNITUS RELIEF
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createTinnitusPanel()
{
    int sliderX = 280;  // Standard slider X position

    // ══════════════════════════════════════════════════════════════════════
    // Section 1: Notch Filters (2 visible, 6 available in engine)
    // ══════════════════════════════════════════════════════════════════════
    createSectionLabel(_panelTinnitus, "NOTCH FILTERS (Tinnitus Suppression)", 60, 10);

    for (int n = 0; n < 2; n++) {
        int baseY = 40 + n * 70;

        // Toggle
        _notchToggle[n] = lv_btn_create(_panelTinnitus);
        lv_obj_set_size(_notchToggle[n], 80, 30);
        lv_obj_set_pos(_notchToggle[n], 60, baseY);
        styleToggleWizard(_notchToggle[n]);
        lv_obj_set_user_data(_notchToggle[n], (void*)(intptr_t)n);
        lv_obj_add_event_cb(_notchToggle[n], onNotchToggle, LV_EVENT_CLICKED, this);

        lv_obj_t* togLbl = lv_label_create(_notchToggle[n]);
        lv_label_set_text(togLbl, "OFF");
        lv_obj_set_style_text_font(togLbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_center(togLbl);

        // Frequency slider
        createValueLabel(_panelTinnitus, "Freq:", 160, baseY + 5);
        _notchFreqSlider[n] = lv_slider_create(_panelTinnitus);
        lv_obj_set_size(_notchFreqSlider[n], 350, 22);
        lv_obj_set_pos(_notchFreqSlider[n], sliderX, baseY);
        lv_slider_set_range(_notchFreqSlider[n], 500, 12000);
        lv_slider_set_value(_notchFreqSlider[n], 4000 + n * 2000, LV_ANIM_OFF);
        styleSliderWizard(_notchFreqSlider[n]);
        lv_obj_set_user_data(_notchFreqSlider[n], (void*)(intptr_t)n);
        lv_obj_add_event_cb(_notchFreqSlider[n], onNotchFreqChanged, LV_EVENT_VALUE_CHANGED, this);

        char freqBuf[16];
        snprintf(freqBuf, sizeof(freqBuf), "%d Hz", 4000 + n * 2000);
        _notchFreqLabel[n] = createValueLabel(_panelTinnitus, freqBuf, 650, baseY + 5);

        // Q slider
        createValueLabel(_panelTinnitus, "Q:", 720, baseY + 5);
        _notchQSlider[n] = lv_slider_create(_panelTinnitus);
        lv_obj_set_size(_notchQSlider[n], 200, 22);
        lv_obj_set_pos(_notchQSlider[n], 760, baseY);
        lv_slider_set_range(_notchQSlider[n], 10, 160);  // 1.0 - 16.0
        lv_slider_set_value(_notchQSlider[n], 80, LV_ANIM_OFF);  // Q=8
        styleSliderWizard(_notchQSlider[n]);
        lv_obj_set_user_data(_notchQSlider[n], (void*)(intptr_t)n);
        lv_obj_add_event_cb(_notchQSlider[n], onNotchQChanged, LV_EVENT_VALUE_CHANGED, this);

        _notchQLabel[n] = createValueLabel(_panelTinnitus, "8.0", 980, baseY + 5);
    }

    createDiamondDivider(_panelTinnitus, 178, 900);

    // ══════════════════════════════════════════════════════════════════════
    // Section 2: Masking Noise Generator
    // ══════════════════════════════════════════════════════════════════════
    createSectionLabel(_panelTinnitus, "MASKING NOISE", 60, 185);

    // Noise type buttons
    const char* noiseLabels[] = {"OFF", "WHITE", "PINK", "BROWN"};
    for (int t = 0; t < 4; t++) {
        _noiseTypeBtns[t] = lv_btn_create(_panelTinnitus);
        lv_obj_set_size(_noiseTypeBtns[t], 110, 32);
        lv_obj_set_pos(_noiseTypeBtns[t], 60 + t * 120, 215);
        styleToggleWizard(_noiseTypeBtns[t]);
        lv_obj_set_user_data(_noiseTypeBtns[t], (void*)(intptr_t)t);
        lv_obj_add_event_cb(_noiseTypeBtns[t], onNoiseTypeClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* lbl = lv_label_create(_noiseTypeBtns[t]);
        lv_label_set_text(lbl, noiseLabels[t]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(LAVENDER), LV_PART_MAIN);
        lv_obj_center(lbl);
    }
    // Highlight OFF
    lv_obj_set_style_border_color(_noiseTypeBtns[0], lv_color_hex(CYAN_GLOW), LV_PART_MAIN);

    // Level slider
    createValueLabel(_panelTinnitus, "Level:", 560, 222);
    _noiseLevelSlider = lv_slider_create(_panelTinnitus);
    lv_obj_set_size(_noiseLevelSlider, 280, 22);
    lv_obj_set_pos(_noiseLevelSlider, 620, 218);
    lv_slider_set_range(_noiseLevelSlider, 0, 100);
    lv_slider_set_value(_noiseLevelSlider, 30, LV_ANIM_OFF);
    styleSliderWizard(_noiseLevelSlider);
    lv_obj_add_event_cb(_noiseLevelSlider, onNoiseLevelChanged, LV_EVENT_VALUE_CHANGED, this);
    _noiseLevelLabel = createValueLabel(_panelTinnitus, "30%", 920, 222);

    // Band limiting
    createValueLabel(_panelTinnitus, "Low Cut:", 60, 260);
    _noiseLowCutSlider = lv_slider_create(_panelTinnitus);
    lv_obj_set_size(_noiseLowCutSlider, 350, 22);
    lv_obj_set_pos(_noiseLowCutSlider, 160, 258);
    lv_slider_set_range(_noiseLowCutSlider, 20, 2000);
    lv_slider_set_value(_noiseLowCutSlider, 100, LV_ANIM_OFF);
    styleSliderWizard(_noiseLowCutSlider);
    lv_obj_add_event_cb(_noiseLowCutSlider, onNoiseLowCutChanged, LV_EVENT_VALUE_CHANGED, this);
    _noiseLowCutLabel = createValueLabel(_panelTinnitus, "100 Hz", 530, 260);

    createValueLabel(_panelTinnitus, "High Cut:", 620, 260);
    _noiseHighCutSlider = lv_slider_create(_panelTinnitus);
    lv_obj_set_size(_noiseHighCutSlider, 250, 22);
    lv_obj_set_pos(_noiseHighCutSlider, 720, 258);
    lv_slider_set_range(_noiseHighCutSlider, 1000, 16000);
    lv_slider_set_value(_noiseHighCutSlider, 8000, LV_ANIM_OFF);
    styleSliderWizard(_noiseHighCutSlider);
    lv_obj_add_event_cb(_noiseHighCutSlider, onNoiseHighCutChanged, LV_EVENT_VALUE_CHANGED, this);
    _noiseHighCutLabel = createValueLabel(_panelTinnitus, "8000 Hz", 980, 260);

    createDiamondDivider(_panelTinnitus, 295, 900);

    // ══════════════════════════════════════════════════════════════════════
    // Section 3: Tone Finder
    // ══════════════════════════════════════════════════════════════════════
    createSectionLabel(_panelTinnitus, "TONE FINDER (Pitch Matching)", 60, 302);

    _toneFinderToggle = lv_btn_create(_panelTinnitus);
    lv_obj_set_size(_toneFinderToggle, 80, 30);
    lv_obj_set_pos(_toneFinderToggle, 60, 332);
    styleToggleWizard(_toneFinderToggle);
    lv_obj_add_event_cb(_toneFinderToggle, onToneFinderToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* toneTogLbl = lv_label_create(_toneFinderToggle);
    lv_label_set_text(toneTogLbl, "OFF");
    lv_obj_set_style_text_font(toneTogLbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_center(toneTogLbl);

    createValueLabel(_panelTinnitus, "Freq:", 160, 337);
    _toneFinderFreqSlider = lv_slider_create(_panelTinnitus);
    lv_obj_set_size(_toneFinderFreqSlider, 420, 22);
    lv_obj_set_pos(_toneFinderFreqSlider, 220, 333);
    lv_slider_set_range(_toneFinderFreqSlider, 200, 12000);
    lv_slider_set_value(_toneFinderFreqSlider, 4000, LV_ANIM_OFF);
    styleSliderWizard(_toneFinderFreqSlider);
    lv_obj_add_event_cb(_toneFinderFreqSlider, onToneFinderFreqChanged, LV_EVENT_VALUE_CHANGED, this);
    _toneFinderFreqLabel = createValueLabel(_panelTinnitus, "4000 Hz", 660, 337);

    createValueLabel(_panelTinnitus, "Level:", 760, 337);
    _toneFinderLevelSlider = lv_slider_create(_panelTinnitus);
    lv_obj_set_size(_toneFinderLevelSlider, 150, 22);
    lv_obj_set_pos(_toneFinderLevelSlider, 820, 333);
    lv_slider_set_range(_toneFinderLevelSlider, 0, 100);
    lv_slider_set_value(_toneFinderLevelSlider, 30, LV_ANIM_OFF);
    styleSliderWizard(_toneFinderLevelSlider);
    lv_obj_add_event_cb(_toneFinderLevelSlider, onToneFinderLevelChanged, LV_EVENT_VALUE_CHANGED, this);
    _toneFinderLevelLabel = createValueLabel(_panelTinnitus, "30%", 980, 337);

    // Transfer to notch button
    _toneFinderTransferBtn = lv_btn_create(_panelTinnitus);
    lv_obj_set_size(_toneFinderTransferBtn, 160, 30);
    lv_obj_set_pos(_toneFinderTransferBtn, 60, 370);
    styleToggleWizard(_toneFinderTransferBtn);
    lv_obj_set_style_border_color(_toneFinderTransferBtn, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_add_event_cb(_toneFinderTransferBtn, onToneFinderTransfer, LV_EVENT_CLICKED, this);

    lv_obj_t* transLbl = lv_label_create(_toneFinderTransferBtn);
    lv_label_set_text(transLbl, "Copy to Notch 1");
    lv_obj_set_style_text_font(transLbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(transLbl, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_center(transLbl);

    createDiamondDivider(_panelTinnitus, 408, 900);

    // ══════════════════════════════════════════════════════════════════════
    // Section 4: HF Extension + Binaural Beats
    // ══════════════════════════════════════════════════════════════════════
    createSectionLabel(_panelTinnitus, "HIGH-FREQ EXTENSION", 60, 415);

    _hfExtToggle = lv_btn_create(_panelTinnitus);
    lv_obj_set_size(_hfExtToggle, 70, 28);
    lv_obj_set_pos(_hfExtToggle, 60, 442);
    styleToggleWizard(_hfExtToggle);
    lv_obj_add_event_cb(_hfExtToggle, onHfExtToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* hfTogLbl = lv_label_create(_hfExtToggle);
    lv_label_set_text(hfTogLbl, "OFF");
    lv_obj_set_style_text_font(hfTogLbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_center(hfTogLbl);

    createValueLabel(_panelTinnitus, "Freq:", 140, 448);
    _hfExtFreqSlider = lv_slider_create(_panelTinnitus);
    lv_obj_set_size(_hfExtFreqSlider, 150, 20);
    lv_obj_set_pos(_hfExtFreqSlider, 190, 445);
    lv_slider_set_range(_hfExtFreqSlider, 4000, 12000);
    lv_slider_set_value(_hfExtFreqSlider, 8000, LV_ANIM_OFF);
    styleSliderWizard(_hfExtFreqSlider);
    lv_obj_add_event_cb(_hfExtFreqSlider, onHfExtFreqChanged, LV_EVENT_VALUE_CHANGED, this);
    _hfExtFreqLabel = createValueLabel(_panelTinnitus, "8k", 350, 448);

    createValueLabel(_panelTinnitus, "Boost:", 400, 448);
    _hfExtGainSlider = lv_slider_create(_panelTinnitus);
    lv_obj_set_size(_hfExtGainSlider, 100, 20);
    lv_obj_set_pos(_hfExtGainSlider, 460, 445);
    lv_slider_set_range(_hfExtGainSlider, 0, 120);  // 0-12 dB
    lv_slider_set_value(_hfExtGainSlider, 60, LV_ANIM_OFF);
    styleSliderWizard(_hfExtGainSlider);
    lv_obj_add_event_cb(_hfExtGainSlider, onHfExtGainChanged, LV_EVENT_VALUE_CHANGED, this);
    _hfExtGainLabel = createValueLabel(_panelTinnitus, "6dB", 575, 448);

    // Binaural beats section (same row)
    createSectionLabel(_panelTinnitus, "BINAURAL BEATS", 630, 415);

    _binauralToggle = lv_btn_create(_panelTinnitus);
    lv_obj_set_size(_binauralToggle, 70, 28);
    lv_obj_set_pos(_binauralToggle, 630, 442);
    styleToggleWizard(_binauralToggle);
    lv_obj_add_event_cb(_binauralToggle, onBinauralToggle, LV_EVENT_CLICKED, this);

    lv_obj_t* binTogLbl = lv_label_create(_binauralToggle);
    lv_label_set_text(binTogLbl, "OFF");
    lv_obj_set_style_text_font(binTogLbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_center(binTogLbl);

    // Binaural presets (Delta/Theta/Alpha/Beta)
    const char* presetLabels[] = {"D", "T", "A", "B"};
    for (int p = 0; p < 4; p++) {
        _binauralPresetBtns[p] = lv_btn_create(_panelTinnitus);
        lv_obj_set_size(_binauralPresetBtns[p], 50, 28);
        lv_obj_set_pos(_binauralPresetBtns[p], 710 + p * 58, 442);
        styleToggleWizard(_binauralPresetBtns[p]);
        lv_obj_set_user_data(_binauralPresetBtns[p], (void*)(intptr_t)p);
        lv_obj_add_event_cb(_binauralPresetBtns[p], onBinauralPresetClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* lbl = lv_label_create(_binauralPresetBtns[p]);
        lv_label_set_text(lbl, presetLabels[p]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(LAVENDER), LV_PART_MAIN);
        lv_obj_center(lbl);
    }
    // Highlight Alpha by default
    lv_obj_set_style_border_color(_binauralPresetBtns[2], lv_color_hex(CYAN_GLOW), LV_PART_MAIN);

    // Binaural beat slider
    createValueLabel(_panelTinnitus, "Beat:", 630, 480);
    _binauralBeatSlider = lv_slider_create(_panelTinnitus);
    lv_obj_set_size(_binauralBeatSlider, 200, 20);
    lv_obj_set_pos(_binauralBeatSlider, 680, 478);
    lv_slider_set_range(_binauralBeatSlider, 1, 40);
    lv_slider_set_value(_binauralBeatSlider, 10, LV_ANIM_OFF);
    styleSliderWizard(_binauralBeatSlider);
    lv_obj_add_event_cb(_binauralBeatSlider, onBinauralBeatChanged, LV_EVENT_VALUE_CHANGED, this);
    _binauralBeatLabel = createValueLabel(_panelTinnitus, "10Hz", 900, 480);

    createValueLabel(_panelTinnitus, "Level:", 630, 510);
    _binauralLevelSlider = lv_slider_create(_panelTinnitus);
    lv_obj_set_size(_binauralLevelSlider, 200, 20);
    lv_obj_set_pos(_binauralLevelSlider, 690, 508);
    lv_slider_set_range(_binauralLevelSlider, 0, 100);
    lv_slider_set_value(_binauralLevelSlider, 30, LV_ANIM_OFF);
    styleSliderWizard(_binauralLevelSlider);
    lv_obj_add_event_cb(_binauralLevelSlider, onBinauralLevelChanged, LV_EVENT_VALUE_CHANGED, this);
    _binauralLevelLabel = createValueLabel(_panelTinnitus, "30%", 900, 510);

    // Info note
    lv_obj_t* tinNote = lv_label_create(_panelTinnitus);
    lv_label_set_text(tinNote, "Notched sound: suppresses tinnitus frequency | Pink/brown: relaxing masking | Binaural: entrainment");
    lv_obj_set_style_text_font(tinNote, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(tinNote, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(tinNote, 60, 545);
}

void WizardUI::refreshProfileList()
{
#ifdef ESP_PLATFORM
    // First check if SD card is accessible
    if (!ProfileManager::isSdCardAccessible()) {
        if (_profileStatusLabel) {
            lv_label_set_text(_profileStatusLabel, "SD Card: Not inserted or not formatted");
            lv_obj_set_style_text_color(_profileStatusLabel, lv_color_hex(METER_RED), LV_PART_MAIN);
        }
        if (_profileDefaultLabel) {
            lv_label_set_text(_profileDefaultLabel, "Insert FAT32 SD card to use profiles");
        }
        return;
    }

    auto names = ProfileManager::listProfiles();

    // Update status label with profile count
    if (_profileStatusLabel) {
        char buf[128];
        if (names.empty()) {
            snprintf(buf, sizeof(buf), "SD Card: OK | No profiles saved");
        } else {
            snprintf(buf, sizeof(buf), "SD Card: OK | %zu profile(s) found", names.size());
        }
        lv_label_set_text(_profileStatusLabel, buf);
        lv_obj_set_style_text_color(_profileStatusLabel, lv_color_hex(METER_GREEN), LV_PART_MAIN);
    }

    // Update default profile indicator
    auto defName = ProfileManager::getDefaultProfile();
    if (_profileDefaultLabel) {
        if (defName.empty()) {
            lv_label_set_text(_profileDefaultLabel, "Default: (none)");
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Default: %s", defName.c_str());
            lv_label_set_text(_profileDefaultLabel, buf);
        }
    }
#else
    if (_profileStatusLabel) {
        lv_label_set_text(_profileStatusLabel, "SD Card: Not available (simulator)");
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Footer bar
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createFooter()
{
    _footerBar = lv_obj_create(_root);
    lv_obj_remove_style_all(_footerBar);
    lv_obj_set_size(_footerBar, SCREEN_W, FOOTER_H);
    lv_obj_set_pos(_footerBar, 0, SCREEN_H - FOOTER_H);
    lv_obj_set_style_bg_color(_footerBar, lv_color_hex(BG_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_footerBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(_footerBar, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(_footerBar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(_footerBar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_remove_flag(_footerBar, LV_OBJ_FLAG_SCROLLABLE);

    // Headphone status
    _hpStatusLabel = lv_label_create(_footerBar);
    lv_label_set_text(_hpStatusLabel, "HP: ---");
    lv_obj_set_style_text_font(_hpStatusLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_hpStatusLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(_hpStatusLabel, 30, 10);

    // Sample rate
    lv_obj_t* srLabel = lv_label_create(_footerBar);
    lv_label_set_text(srLabel, "Sample: 48kHz");
    lv_obj_set_style_text_font(srLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(srLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(srLabel, 250, 10);

    // Block size
    lv_obj_t* bsLabel = lv_label_create(_footerBar);
    lv_label_set_text(bsLabel, "Block: 480");
    lv_obj_set_style_text_font(bsLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(bsLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(bsLabel, 450, 10);

    // Latency
    lv_obj_t* ltLabel = lv_label_create(_footerBar);
    lv_label_set_text(ltLabel, "Latency: ~10.0ms");
    lv_obj_set_style_text_font(ltLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ltLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(ltLabel, 620, 10);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync all UI controls to engine params (after profile load or init)
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::syncUiToParams()
{
#ifdef ESP_PLATFORM
    auto params = AudioEngine::getInstance().getParams();
    char buf[32];

    // ── Filter panel ──
    if (_hpfSlider) {
        lv_slider_set_value(_hpfSlider, (int)params.hpfFrequency, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d Hz", (int)params.hpfFrequency);
        if (_hpfValueLabel) lv_label_set_text(_hpfValueLabel, buf);
    }
    if (_hpfToggle) {
        lv_obj_t* lbl = lv_obj_get_child(_hpfToggle, 0);
        if (lbl) lv_label_set_text(lbl, params.hpfEnabled ? "HPF ON" : "HPF OFF");
        lv_obj_set_style_border_color(_hpfToggle,
            lv_color_hex(params.hpfEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
    }
    if (_lpfSlider) {
        lv_slider_set_value(_lpfSlider, (int)params.lpfFrequency, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d Hz", (int)params.lpfFrequency);
        if (_lpfValueLabel) lv_label_set_text(_lpfValueLabel, buf);
    }
    if (_lpfToggle) {
        lv_obj_t* lbl = lv_obj_get_child(_lpfToggle, 0);
        if (lbl) lv_label_set_text(lbl, params.lpfEnabled ? "LPF ON" : "LPF OFF");
        lv_obj_set_style_border_color(_lpfToggle,
            lv_color_hex(params.lpfEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
    }

    // NS
    if (_nsToggle) {
        lv_obj_t* lbl = lv_obj_get_child(_nsToggle, 0);
        if (lbl) lv_label_set_text(lbl, params.nsEnabled ? "NS ON" : "NS OFF");
        lv_obj_set_style_border_color(_nsToggle,
            lv_color_hex(params.nsEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
    }
    _nsActiveMode = params.nsMode;
    {
        lv_obj_t* nsBtns[] = {_nsModeBtn0, _nsModeBtn1, _nsModeBtn2};
        for (int i = 0; i < 3; i++) {
            if (!nsBtns[i]) continue;
            bool active = (i == params.nsMode);
            lv_obj_set_style_border_color(nsBtns[i], lv_color_hex(active ? CYAN_GLOW : GOLD), LV_PART_MAIN);
            lv_obj_t* c = lv_obj_get_child(nsBtns[i], 0);
            if (c) lv_obj_set_style_text_color(c, lv_color_hex(active ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
        }
    }

    // ── EQ panel ──
    auto setEqSlider = [&](lv_obj_t* slider, lv_obj_t* label, float db) {
        if (slider) lv_slider_set_value(slider, (int)(db * 10.0f), LV_ANIM_OFF);
        if (label) {
            snprintf(buf, sizeof(buf), "%+.1f dB", (double)db);
            lv_label_set_text(label, buf);
        }
    };
    setEqSlider(_eqLowSlider, _eqLowLabel, params.eqLowGain);
    setEqSlider(_eqMidSlider, _eqMidLabel, params.eqMidGain);
    setEqSlider(_eqHighSlider, _eqHighLabel, params.eqHighGain);

    // ── Output panel ──
    if (_volumeSlider) {
        lv_slider_set_value(_volumeSlider, params.outputVolume, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d", params.outputVolume);
        if (_volumeValueLabel) lv_label_set_text(_volumeValueLabel, buf);
    }
    if (_gainSlider) {
        lv_slider_set_value(_gainSlider, (int)(params.outputGain * 100.0f), LV_ANIM_OFF);
        if (params.outputGain > 1.0f) {
            snprintf(buf, sizeof(buf), "%.2fx (%d%%)", (double)params.outputGain, (int)(params.outputGain * 100.0f));
        } else {
            snprintf(buf, sizeof(buf), "%.2fx", (double)params.outputGain);
        }
        if (_gainValueLabel) lv_label_set_text(_gainValueLabel, buf);
    }
    if (_micGainSlider) {
        lv_slider_set_value(_micGainSlider, (int)params.micGain, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d", (int)params.micGain);
        if (_micGainValueLabel) lv_label_set_text(_micGainValueLabel, buf);
    }

    // Boost toggle
    if (_boostToggle) {
        lv_obj_t* lbl = lv_obj_get_child(_boostToggle, 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl,
                lv_color_hex(params.boostEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
        }
        lv_obj_set_style_border_color(_boostToggle,
            lv_color_hex(params.boostEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
    }
    if (_boostWarningLabel) {
        if (params.boostEnabled) {
            lv_obj_remove_flag(_boostWarningLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_boostWarningLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // AGC
    if (_agcToggle) {
        lv_obj_t* lbl = lv_obj_get_child(_agcToggle, 0);
        if (lbl) lv_label_set_text(lbl, params.agcEnabled ? "AGC ON" : "AGC OFF");
        lv_obj_set_style_border_color(_agcToggle,
            lv_color_hex(params.agcEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
    }
    _agcActiveMode = params.agcMode;
    {
        lv_obj_t* agcBtns[] = {_agcModeBtn0, _agcModeBtn1, _agcModeBtn2, _agcModeBtn3};
        for (int i = 0; i < 4; i++) {
            if (!agcBtns[i]) continue;
            bool active = (i == params.agcMode);
            lv_obj_set_style_border_color(agcBtns[i], lv_color_hex(active ? CYAN_GLOW : GOLD), LV_PART_MAIN);
            lv_obj_t* c = lv_obj_get_child(agcBtns[i], 0);
            if (c) lv_obj_set_style_text_color(c, lv_color_hex(active ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
        }
    }
    if (_agcGainSlider) {
        lv_slider_set_value(_agcGainSlider, params.agcCompressionGainDb, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d dB", params.agcCompressionGainDb);
        if (_agcGainValueLabel) lv_label_set_text(_agcGainValueLabel, buf);
    }
    if (_agcTargetSlider) {
        lv_slider_set_value(_agcTargetSlider, params.agcTargetLevelDbfs, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d dBFS", params.agcTargetLevelDbfs);
        if (_agcTargetValueLabel) lv_label_set_text(_agcTargetValueLabel, buf);
    }
    if (_agcLimiterToggle) {
        lv_obj_t* lbl = lv_obj_get_child(_agcLimiterToggle, 0);
        if (lbl) {
            lv_label_set_text(lbl, params.agcLimiterEnabled ? "LIM ON" : "LIM OFF");
            lv_obj_set_style_text_color(lbl,
                lv_color_hex(params.agcLimiterEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
        }
        lv_obj_set_style_border_color(_agcLimiterToggle,
            lv_color_hex(params.agcLimiterEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
    }

    // ── Voice panel ──
    if (_veToggle) {
        lv_obj_t* lbl = lv_obj_get_child(_veToggle, 0);
        if (lbl) lv_label_set_text(lbl, params.veEnabled ? "VE ON" : "VE OFF");
        lv_obj_set_style_border_color(_veToggle,
            lv_color_hex(params.veEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
    }
    if (_veRefGainSlider) {
        lv_slider_set_value(_veRefGainSlider, (int)(params.veRefGain * 10.0f), LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%.1fx", (double)params.veRefGain);
        if (_veRefGainValueLabel) lv_label_set_text(_veRefGainValueLabel, buf);
    }
    if (_veRefHpfSlider) {
        lv_slider_set_value(_veRefHpfSlider, (int)params.veRefHpf, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d Hz", (int)params.veRefHpf);
        if (_veRefHpfValueLabel) lv_label_set_text(_veRefHpfValueLabel, buf);
    }
    if (_veRefLpfSlider) {
        lv_slider_set_value(_veRefLpfSlider, (int)params.veRefLpf, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d Hz", (int)params.veRefLpf);
        if (_veRefLpfValueLabel) lv_label_set_text(_veRefLpfValueLabel, buf);
    }
    if (_veBlendSlider) {
        lv_slider_set_value(_veBlendSlider, (int)(params.veBlend * 100.0f), LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%%", (int)(params.veBlend * 100.0f));
        if (_veBlendValueLabel) lv_label_set_text(_veBlendValueLabel, buf);
    }
    if (_veStepSlider) {
        lv_slider_set_value(_veStepSlider, (int)(params.veStepSize * 100.0f), LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%.2f", (double)params.veStepSize);
        if (_veStepValueLabel) lv_label_set_text(_veStepValueLabel, buf);
    }
    if (_veAttenSlider) {
        lv_slider_set_value(_veAttenSlider, (int)(params.veMaxAttenuation * 100.0f), LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%%", (int)(params.veMaxAttenuation * 100.0f));
        if (_veAttenValueLabel) lv_label_set_text(_veAttenValueLabel, buf);
    }
    _veActiveFilterLen = params.veFilterLength;
    {
        lv_obj_t* fBtns[] = {_veFilterBtn32, _veFilterBtn64, _veFilterBtn128};
        int tapValues[] = {64, 128, 256};
        for (int i = 0; i < 3; i++) {
            if (!fBtns[i]) continue;
            bool active = (tapValues[i] == params.veFilterLength);
            lv_obj_set_style_border_color(fBtns[i], lv_color_hex(active ? CYAN_GLOW : GOLD), LV_PART_MAIN);
            lv_obj_t* c = lv_obj_get_child(fBtns[i], 0);
            if (c) lv_obj_set_style_text_color(c, lv_color_hex(active ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
        }
    }

    // VAD Gate controls
    if (_veVadGateToggle) {
        lv_obj_t* lbl = lv_obj_get_child(_veVadGateToggle, 0);
        if (lbl) {
            lv_label_set_text(lbl, params.veVadGateEnabled ? "GATE ON" : "GATE OFF");
            lv_obj_set_style_text_color(lbl,
                lv_color_hex(params.veVadGateEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
        }
        lv_obj_set_style_border_color(_veVadGateToggle,
            lv_color_hex(params.veVadGateEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
    }
    if (_veVadGateAttenSlider) {
        int attenPct = (int)(params.veVadGateAtten * 100.0f);
        lv_slider_set_value(_veVadGateAttenSlider, attenPct, LV_ANIM_OFF);
        if (_veVadGateAttenValueLabel) {
            float db = (attenPct > 0) ? 20.0f * log10f(params.veVadGateAtten) : -40.0f;
            snprintf(buf, sizeof(buf), "%d%% (%.0fdB)", attenPct, (double)db);
            lv_label_set_text(_veVadGateAttenValueLabel, buf);
        }
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel switching
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::showPanel(int index)
{
    _activePanel = index;

    lv_obj_t* panels[] = {_panelFilter, _panelEq, _panelOutput, _panelVoice, _panelProfiles, _panelTinnitus};
    for (int i = 0; i < NUM_PANELS; i++) {
        if (panels[i]) {
            if (i == index)
                lv_obj_remove_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Refresh profile list when entering profiles panel
    if (index == 4) {
        refreshProfileList();
    }

    updateNavHighlight();
}

void WizardUI::updateNavHighlight()
{
    lv_obj_t* btns[] = {_navBtnFilter, _navBtnEq, _navBtnOutput, _navBtnVoice, _navBtnProfiles, _navBtnTinnitus};
    for (int i = 0; i < NUM_PANELS; i++) {
        if (!btns[i]) continue;
        if (i == _activePanel) {
            lv_obj_set_style_bg_color(btns[i], lv_color_hex(0x1A1540), LV_PART_MAIN);
            lv_obj_set_style_border_color(btns[i], lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
            lv_obj_t* child = lv_obj_get_child(btns[i], 0);
            if (child) lv_obj_set_style_text_color(child, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(btns[i], lv_color_hex(BG_DARK), LV_PART_MAIN);
            lv_obj_set_style_border_color(btns[i], lv_color_hex(DARK_BORDER), LV_PART_MAIN);
            lv_obj_t* child = lv_obj_get_child(btns[i], 0);
            if (child) lv_obj_set_style_text_color(child, lv_color_hex(LAVENDER), LV_PART_MAIN);
        }
    }
}

void WizardUI::updateMuteButton()
{
    bool muted = true;
#ifdef ESP_PLATFORM
    muted = AudioEngine::getInstance().getParams().outputMute;
#endif

    if (_muteBtn) {
        lv_obj_set_style_bg_color(_muteBtn,
            lv_color_hex(muted ? METER_RED : METER_GREEN), LV_PART_MAIN);
    }
    if (_muteBtnLabel) {
        lv_label_set_text(_muteBtnLabel, muted ? "MUTED" : "LIVE");
        lv_obj_set_style_text_color(_muteBtnLabel,
            lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// VU Meter update
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::updateMeters()
{
    float rmsL = 0.0f, rmsR = 0.0f;
    float peakL = 0.0f, peakR = 0.0f;

#ifdef ESP_PLATFORM
    AudioLevels levels = AudioEngine::getInstance().getLevels();
    rmsL = levels.rmsLeft;
    rmsR = levels.rmsRight;
    peakL = levels.peakLeft;
    peakR = levels.peakRight;
#endif

    // Convert RMS to dB-ish scale for display (log scale, 0dB = 1.0)
    // Map to pixel width (max 696 pixels within the 700px background minus borders)
    constexpr int METER_MAX_W = 696;
    constexpr float DB_MIN = -60.0f;

    auto levelToWidth = [](float level, float dbMin, int maxWidth) -> int {
        if (level < 0.00001f) return 0;
        float db = 20.0f * log10f(level);
        if (db < dbMin) return 0;
        float norm = (db - dbMin) / (0.0f - dbMin);  // 0..1
        return (int)(norm * maxWidth);
    };

    auto levelToColor = [](float level) -> uint32_t {
        float db = 20.0f * log10f(level + 0.00001f);
        if (db > -3.0f) return 0xCC4444;      // Red
        if (db > -10.0f) return 0xCCAA33;     // Yellow
        return 0x44CC66;                        // Green
    };

    int wL = levelToWidth(rmsL, DB_MIN, METER_MAX_W);
    int wR = levelToWidth(rmsR, DB_MIN, METER_MAX_W);
    int pL = levelToWidth(peakL, DB_MIN, METER_MAX_W);
    int pR = levelToWidth(peakR, DB_MIN, METER_MAX_W);

    if (_meterBarL) {
        lv_obj_set_width(_meterBarL, wL > 0 ? wL : 1);
        lv_obj_set_style_bg_color(_meterBarL, lv_color_hex(levelToColor(rmsL)), LV_PART_MAIN);
    }
    if (_meterBarR) {
        lv_obj_set_width(_meterBarR, wR > 0 ? wR : 1);
        lv_obj_set_style_bg_color(_meterBarR, lv_color_hex(levelToColor(rmsR)), LV_PART_MAIN);
    }
    if (_meterPeakL) {
        lv_obj_set_x(_meterPeakL, pL > 2 ? pL : 2);
    }
    if (_meterPeakR) {
        lv_obj_set_x(_meterPeakR, pR > 2 ? pR : 2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Style helpers
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::styleSliderWizard(lv_obj_t* slider)
{
    // Main track (background)
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x1A1540), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(slider, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);

    // Indicator (filled portion)
    lv_obj_set_style_bg_color(slider, lv_color_hex(LAVENDER), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, 180, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 4, LV_PART_INDICATOR);

    // Knob
    lv_obj_set_style_bg_color(slider, lv_color_hex(GOLD_BRIGHT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(GOLD), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 8, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, lv_color_hex(GOLD_BRIGHT), LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, 80, LV_PART_KNOB);
}

void WizardUI::styleToggleWizard(lv_obj_t* btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(BG_DARK), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(GOLD), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
}

lv_obj_t* WizardUI::createSectionLabel(lv_obj_t* parent, const char* text, int x, int y)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(label, 2, LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    return label;
}

lv_obj_t* WizardUI::createValueLabel(lv_obj_t* parent, const char* text, int x, int y)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(GOLD), LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    return label;
}

lv_obj_t* WizardUI::createDiamondDivider(lv_obj_t* parent, int y, int width)
{
    // Simplified divider - just a single horizontal line (no rotation = less memory)
    int cx = CONTENT_W / 2;
    int halfW = width / 2;

    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, width, 1);
    lv_obj_set_pos(line, cx - halfW, y);
    lv_obj_set_style_bg_color(line, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);

    return line;
}

// ─────────────────────────────────────────────────────────────────────────────
// Event callbacks
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::onNavBtnClicked(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int panelIdx = (int)(intptr_t)lv_obj_get_user_data(btn);
    ui->showPanel(panelIdx);
}

void WizardUI::onMuteBtnClicked(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    bool currentMute = engine.getParams().outputMute;
    engine.setMute(!currentMute);
    mclog::tagInfo(TAG, "mute toggled: {}", !currentMute);
#endif
    ui->updateMuteButton();
}

void WizardUI::onHpfToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    engine.setHpf(!params.hpfEnabled, params.hpfFrequency);

    // Update button label
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, !params.hpfEnabled ? "HPF ON" : "HPF OFF");
    }
    // Update button style
    lv_obj_set_style_border_color(btn,
        lv_color_hex(!params.hpfEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

void WizardUI::onLpfToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    engine.setLpf(!params.lpfEnabled, params.lpfFrequency);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, !params.lpfEnabled ? "LPF ON" : "LPF OFF");
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(!params.lpfEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

void WizardUI::onHpfSliderChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    engine.setHpf(params.hpfEnabled, (float)val);
#endif

    if (ui->_hpfValueLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", val);
        lv_label_set_text(ui->_hpfValueLabel, buf);
    }
}

void WizardUI::onLpfSliderChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    engine.setLpf(params.lpfEnabled, (float)val);
#endif

    if (ui->_lpfValueLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", val);
        lv_label_set_text(ui->_lpfValueLabel, buf);
    }
}

void WizardUI::onEqSliderChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // -120 to 120
    float db = (float)val / 10.0f;          // -12.0 to 12.0

#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    if (slider == ui->_eqLowSlider) {
        engine.setEqLow(db);
    } else if (slider == ui->_eqMidSlider) {
        engine.setEqMid(db);
    } else if (slider == ui->_eqHighSlider) {
        engine.setEqHigh(db);
    }
#endif

    // Update the corresponding value label
    char buf[16];
    snprintf(buf, sizeof(buf), "%+.1f dB", (double)db);

    lv_obj_t* label = nullptr;
    if (slider == ui->_eqLowSlider) label = ui->_eqLowLabel;
    else if (slider == ui->_eqMidSlider) label = ui->_eqMidLabel;
    else if (slider == ui->_eqHighSlider) label = ui->_eqHighLabel;

    if (label) {
        lv_label_set_text(label, buf);
    }
}

void WizardUI::onVolumeSliderChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setOutputVolume(val);
#endif

    if (ui->_volumeValueLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", val);
        lv_label_set_text(ui->_volumeValueLabel, buf);
    }
}

void WizardUI::onGainSliderChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // 0-600 (extended)
    float gain = (float)val / 100.0f;       // 0.00-6.00

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setOutputGain(gain);
#endif

    if (ui->_gainValueLabel) {
        char buf[16];
        // Show percentage when above 1.0x for clarity
        if (gain > 1.0f) {
            snprintf(buf, sizeof(buf), "%.2fx (%d%%)", (double)gain, (int)(gain * 100.0f));
        } else {
            snprintf(buf, sizeof(buf), "%.2fx", (double)gain);
        }
        lv_label_set_text(ui->_gainValueLabel, buf);
    }
}

void WizardUI::onMicGainSliderChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setMicGain((float)val);
#endif

    if (ui->_micGainValueLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", val);
        lv_label_set_text(ui->_micGainValueLabel, buf);
    }
}

void WizardUI::onNsToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.nsEnabled;
    engine.setNsEnabled(newEnabled);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, newEnabled ? "NS ON" : "NS OFF");
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

void WizardUI::onNsModeClicked(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int mode = (int)(intptr_t)lv_obj_get_user_data(btn);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setNsMode(mode);
#endif

    ui->_nsActiveMode = mode;

    // Update button highlights
    lv_obj_t* btns[] = {ui->_nsModeBtn0, ui->_nsModeBtn1, ui->_nsModeBtn2};
    for (int i = 0; i < 3; i++) {
        if (!btns[i]) continue;
        if (i == mode) {
            lv_obj_set_style_border_color(btns[i], lv_color_hex(CYAN_GLOW), LV_PART_MAIN);
            lv_obj_t* child = lv_obj_get_child(btns[i], 0);
            if (child) lv_obj_set_style_text_color(child, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
        } else {
            lv_obj_set_style_border_color(btns[i], lv_color_hex(GOLD), LV_PART_MAIN);
            lv_obj_t* child = lv_obj_get_child(btns[i], 0);
            if (child) lv_obj_set_style_text_color(child, lv_color_hex(LAVENDER), LV_PART_MAIN);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AGC callbacks
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::onAgcToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.agcEnabled;
    engine.setAgcEnabled(newEnabled);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, newEnabled ? "AGC ON" : "AGC OFF");
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

void WizardUI::onAgcModeClicked(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int mode = (int)(intptr_t)lv_obj_get_user_data(btn);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setAgcMode(mode);
#endif

    ui->_agcActiveMode = mode;

    // Update button highlights
    lv_obj_t* btns[] = {ui->_agcModeBtn0, ui->_agcModeBtn1, ui->_agcModeBtn2, ui->_agcModeBtn3};
    for (int i = 0; i < 4; i++) {
        if (!btns[i]) continue;
        if (i == mode) {
            lv_obj_set_style_border_color(btns[i], lv_color_hex(CYAN_GLOW), LV_PART_MAIN);
            lv_obj_t* child = lv_obj_get_child(btns[i], 0);
            if (child) lv_obj_set_style_text_color(child, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
        } else {
            lv_obj_set_style_border_color(btns[i], lv_color_hex(GOLD), LV_PART_MAIN);
            lv_obj_t* child = lv_obj_get_child(btns[i], 0);
            if (child) lv_obj_set_style_text_color(child, lv_color_hex(LAVENDER), LV_PART_MAIN);
        }
    }
}

void WizardUI::onAgcGainChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // 0-90

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setAgcCompressionGain(val);
#endif

    if (ui->_agcGainValueLabel) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d dB", val);
        lv_label_set_text(ui->_agcGainValueLabel, buf);
    }
}

void WizardUI::onAgcTargetChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // -31 to 0

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setAgcTargetLevel(val);
#endif

    if (ui->_agcTargetValueLabel) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d dBFS", val);
        lv_label_set_text(ui->_agcTargetValueLabel, buf);
    }
}

void WizardUI::onAgcLimiterToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.agcLimiterEnabled;
    engine.setAgcLimiterEnabled(newEnabled);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, newEnabled ? "LIM ON" : "LIM OFF");
        lv_obj_set_style_text_color(label,
            lv_color_hex(newEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Voice Exclusion callbacks
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::onVeToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.veEnabled;
    engine.setVeEnabled(newEnabled);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, newEnabled ? "VE ON" : "VE OFF");
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

void WizardUI::onVeBlendChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // 0-100
    float blend = (float)val / 100.0f;      // 0.0-1.0

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setVeBlend(blend);
#endif

    if (ui->_veBlendValueLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(ui->_veBlendValueLabel, buf);
    }
}

void WizardUI::onVeStepChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);   // 1-100
    float step = (float)val / 100.0f;        // 0.01-1.0

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setVeStepSize(step);
#endif

    if (ui->_veStepValueLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.2f", (double)step);
        lv_label_set_text(ui->_veStepValueLabel, buf);
    }
}

void WizardUI::onVeFilterLenClicked(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int taps = (int)(intptr_t)lv_obj_get_user_data(btn);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setVeFilterLength(taps);
#endif

    ui->_veActiveFilterLen = taps;

    // Update button highlights
    lv_obj_t* btns[] = {ui->_veFilterBtn32, ui->_veFilterBtn64, ui->_veFilterBtn128};
    int tapValues[] = {64, 128, 256};
    for (int i = 0; i < 3; i++) {
        if (!btns[i]) continue;
        if (tapValues[i] == taps) {
            lv_obj_set_style_border_color(btns[i], lv_color_hex(CYAN_GLOW), LV_PART_MAIN);
            lv_obj_t* child = lv_obj_get_child(btns[i], 0);
            if (child) lv_obj_set_style_text_color(child, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
        } else {
            lv_obj_set_style_border_color(btns[i], lv_color_hex(GOLD), LV_PART_MAIN);
            lv_obj_t* child = lv_obj_get_child(btns[i], 0);
            if (child) lv_obj_set_style_text_color(child, lv_color_hex(LAVENDER), LV_PART_MAIN);
        }
    }
}

void WizardUI::onVeAttenChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);   // 0-100
    float atten = (float)val / 100.0f;       // 0.0-1.0

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setVeMaxAttenuation(atten);
#endif

    if (ui->_veAttenValueLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(ui->_veAttenValueLabel, buf);
    }
}

void WizardUI::onVeRefGainChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);   // 1-50
    float gain = (float)val / 10.0f;         // 0.1-5.0

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setVeRefGain(gain);
#endif

    if (ui->_veRefGainValueLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.1fx", (double)gain);
        lv_label_set_text(ui->_veRefGainValueLabel, buf);
    }
}

void WizardUI::onVeRefHpfChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);   // 20-500

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setVeRefHpf((float)val);
#endif

    if (ui->_veRefHpfValueLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", val);
        lv_label_set_text(ui->_veRefHpfValueLabel, buf);
    }
}

void WizardUI::onVeRefLpfChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);   // 1000-8000

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setVeRefLpf((float)val);
#endif

    if (ui->_veRefLpfValueLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", val);
        lv_label_set_text(ui->_veRefLpfValueLabel, buf);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Voice Exclusion mode switch callback
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::onVeModeClicked(lv_event_t* e)
{
    // AEC mode UI removed - stub to satisfy linker
    (void)e;
}

// ─────────────────────────────────────────────────────────────────────────────
// AEC callbacks (stubs - UI removed to save memory, engine still supports AEC)
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::onVeAecModeClicked(lv_event_t* e) { (void)e; }
void WizardUI::onVeAecFilterLenChanged(lv_event_t* e) { (void)e; }
void WizardUI::onVeVadToggle(lv_event_t* e) { (void)e; }
void WizardUI::onVeVadModeChanged(lv_event_t* e) { (void)e; }

void WizardUI::onBoostToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.boostEnabled;
    engine.setBoostEnabled(newEnabled);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_obj_set_style_text_color(label,
            lv_color_hex(newEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);

    // Show/hide warning label
    if (ui->_boostWarningLabel) {
        if (newEnabled) {
            lv_obj_remove_flag(ui->_boostWarningLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui->_boostWarningLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }
#else
    (void)ui;
#endif
}

void WizardUI::onVeVadGateToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.veVadGateEnabled;
    engine.setVeVadGateEnabled(newEnabled);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, newEnabled ? "GATE ON" : "GATE OFF");
        lv_obj_set_style_text_color(label,
            lv_color_hex(newEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

void WizardUI::onVeVadGateAttenChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);   // 0-50
    float atten = (float)val / 100.0f;       // 0.0-0.5

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setVeVadGateAtten(atten);
#endif

    if (ui->_veVadGateAttenValueLabel) {
        char buf[16];
        // Show approximate dB value
        float db = (val > 0) ? 20.0f * log10f(atten) : -40.0f;
        snprintf(buf, sizeof(buf), "%d%% (%.0fdB)", val, (double)db);
        lv_label_set_text(ui->_veVadGateAttenValueLabel, buf);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Profile callbacks
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::onProfileSave(lv_event_t* e)
{
    // Disabled in simplified panel
    (void)e;
}

void WizardUI::onProfileLoad(lv_event_t* e)
{
    // Now used as REFRESH button - just check SD card status
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    if (ui->_profileStatusLabel) {
        lv_label_set_text(ui->_profileStatusLabel, "Checking SD card...");
        lv_obj_set_style_text_color(ui->_profileStatusLabel,
            lv_color_hex(GOLD), LV_PART_MAIN);
    }
    ui->refreshProfileList();
}

void WizardUI::onProfileDelete(lv_event_t* e)
{
    // Disabled in simplified panel
    (void)e;
}

void WizardUI::onProfileSetDefault(lv_event_t* e)
{
    // Disabled in simplified panel
    (void)e;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tinnitus Relief Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::onNotchToggle(lv_event_t* e)
{
    (void)lv_event_get_user_data(e);  // ui not used in callback
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);

#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.tinnitus.notches[idx].enabled;
    engine.setNotchEnabled(idx, newEnabled);

    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, newEnabled ? "ON" : "OFF");
        lv_obj_set_style_text_color(label,
            lv_color_hex(newEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#else
    (void)idx;
#endif
}

void WizardUI::onNotchFreqChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int idx = (int)(intptr_t)lv_obj_get_user_data(slider);
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setNotchFrequency(idx, (float)val);
#endif

    if (idx < 2 && ui->_notchFreqLabel[idx]) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", val);
        lv_label_set_text(ui->_notchFreqLabel[idx], buf);
    }
}

void WizardUI::onNotchQChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int idx = (int)(intptr_t)lv_obj_get_user_data(slider);
    int val = lv_slider_get_value(slider);  // 10-160
    float Q = (float)val / 10.0f;           // 1.0-16.0

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setNotchQ(idx, Q);
#endif

    if (idx < 2 && ui->_notchQLabel[idx]) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.1f", (double)Q);
        lv_label_set_text(ui->_notchQLabel[idx], buf);
    }
}

void WizardUI::onNoiseTypeClicked(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int type = (int)(intptr_t)lv_obj_get_user_data(btn);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setNoiseType(type);
#endif

    ui->_noiseActiveType = type;

    // Update button highlights
    for (int i = 0; i < 4; i++) {
        if (!ui->_noiseTypeBtns[i]) continue;
        bool active = (i == type);
        lv_obj_set_style_border_color(ui->_noiseTypeBtns[i],
            lv_color_hex(active ? CYAN_GLOW : GOLD), LV_PART_MAIN);
        lv_obj_t* child = lv_obj_get_child(ui->_noiseTypeBtns[i], 0);
        if (child) {
            lv_obj_set_style_text_color(child,
                lv_color_hex(active ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
        }
    }
}

void WizardUI::onNoiseLevelChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // 0-100
    float level = (float)val / 100.0f;

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setNoiseLevel(level);
#endif

    if (ui->_noiseLevelLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(ui->_noiseLevelLabel, buf);
    }
}

void WizardUI::onNoiseLowCutChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setNoiseLowCut((float)val);
#endif

    if (ui->_noiseLowCutLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", val);
        lv_label_set_text(ui->_noiseLowCutLabel, buf);
    }
}

void WizardUI::onNoiseHighCutChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setNoiseHighCut((float)val);
#endif

    if (ui->_noiseHighCutLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", val);
        lv_label_set_text(ui->_noiseHighCutLabel, buf);
    }
}

void WizardUI::onToneFinderToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.tinnitus.toneFinderEnabled;
    engine.setToneFinderEnabled(newEnabled);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, newEnabled ? "ON" : "OFF");
        lv_obj_set_style_text_color(label,
            lv_color_hex(newEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

void WizardUI::onToneFinderFreqChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setToneFinderFreq((float)val);
#endif

    if (ui->_toneFinderFreqLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", val);
        lv_label_set_text(ui->_toneFinderFreqLabel, buf);
    }
}

void WizardUI::onToneFinderLevelChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // 0-100
    float level = (float)val / 100.0f;

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setToneFinderLevel(level);
#endif

    if (ui->_toneFinderLevelLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(ui->_toneFinderLevelLabel, buf);
    }
}

void WizardUI::onToneFinderTransfer(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    float freq = params.tinnitus.toneFinderFreq;

    // Copy tone finder frequency to notch 0 and enable it
    engine.setNotchFrequency(0, freq);
    engine.setNotchEnabled(0, true);

    // Update UI
    if (ui->_notchFreqSlider[0]) {
        lv_slider_set_value(ui->_notchFreqSlider[0], (int)freq, LV_ANIM_OFF);
    }
    if (ui->_notchFreqLabel[0]) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", (int)freq);
        lv_label_set_text(ui->_notchFreqLabel[0], buf);
    }
    if (ui->_notchToggle[0]) {
        lv_obj_set_style_border_color(ui->_notchToggle[0], lv_color_hex(CYAN_GLOW), LV_PART_MAIN);
        lv_obj_t* label = lv_obj_get_child(ui->_notchToggle[0], 0);
        if (label) {
            lv_label_set_text(label, "ON");
            lv_obj_set_style_text_color(label, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
        }
    }
#else
    (void)ui;
#endif
}

void WizardUI::onHfExtToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.tinnitus.hfExtEnabled;
    engine.setHfExtEnabled(newEnabled);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, newEnabled ? "ON" : "OFF");
        lv_obj_set_style_text_color(label,
            lv_color_hex(newEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

void WizardUI::onHfExtFreqChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setHfExtFreq((float)val);
#endif

    if (ui->_hfExtFreqLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%dk", val / 1000);
        lv_label_set_text(ui->_hfExtFreqLabel, buf);
    }
}

void WizardUI::onHfExtGainChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // 0-120
    float gainDb = (float)val / 10.0f;      // 0-12 dB

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setHfExtGainDb(gainDb);
#endif

    if (ui->_hfExtGainLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0fdB", (double)gainDb);
        lv_label_set_text(ui->_hfExtGainLabel, buf);
    }
}

void WizardUI::onBinauralToggle(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    (void)ui;
#ifdef ESP_PLATFORM
    auto& engine = AudioEngine::getInstance();
    auto params = engine.getParams();
    bool newEnabled = !params.tinnitus.binauralEnabled;
    engine.setBinauralEnabled(newEnabled);

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, newEnabled ? "ON" : "OFF");
        lv_obj_set_style_text_color(label,
            lv_color_hex(newEnabled ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
    }
    lv_obj_set_style_border_color(btn,
        lv_color_hex(newEnabled ? CYAN_GLOW : GOLD), LV_PART_MAIN);
#endif
}

void WizardUI::onBinauralCarrierChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setBinauralCarrier((float)val);
#endif

    if (ui->_binauralCarrierLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d Hz", val);
        lv_label_set_text(ui->_binauralCarrierLabel, buf);
    }
}

void WizardUI::onBinauralBeatChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // 1-40 Hz

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setBinauralBeat((float)val);
#endif

    if (ui->_binauralBeatLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%dHz", val);
        lv_label_set_text(ui->_binauralBeatLabel, buf);
    }
}

void WizardUI::onBinauralLevelChanged(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);  // 0-100
    float level = (float)val / 100.0f;

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setBinauralLevel(level);
#endif

    if (ui->_binauralLevelLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(ui->_binauralLevelLabel, buf);
    }
}

void WizardUI::onBinauralPresetClicked(lv_event_t* e)
{
    auto* ui = static_cast<WizardUI*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int preset = (int)(intptr_t)lv_obj_get_user_data(btn);

    // Preset beat frequencies: Delta=2, Theta=6, Alpha=10, Beta=20
    const float beatFreqs[] = {2.0f, 6.0f, 10.0f, 20.0f};

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setBinauralBeat(beatFreqs[preset]);
#endif

    ui->_binauralActivePreset = preset;

    // Update slider
    if (ui->_binauralBeatSlider) {
        lv_slider_set_value(ui->_binauralBeatSlider, (int)beatFreqs[preset], LV_ANIM_OFF);
    }
    if (ui->_binauralBeatLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0fHz", (double)beatFreqs[preset]);
        lv_label_set_text(ui->_binauralBeatLabel, buf);
    }

    // Update button highlights
    for (int i = 0; i < 4; i++) {
        if (!ui->_binauralPresetBtns[i]) continue;
        bool active = (i == preset);
        lv_obj_set_style_border_color(ui->_binauralPresetBtns[i],
            lv_color_hex(active ? CYAN_GLOW : GOLD), LV_PART_MAIN);
        lv_obj_t* child = lv_obj_get_child(ui->_binauralPresetBtns[i], 0);
        if (child) {
            lv_obj_set_style_text_color(child,
                lv_color_hex(active ? GOLD_BRIGHT : LAVENDER), LV_PART_MAIN);
        }
    }
}
