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
#endif

static const char* TAG = "WizardUI";

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::create(lv_obj_t* parent)
{
    // Root container - full screen dark background
    _root = lv_obj_create(parent);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(_root, lv_color_hex(BG_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    createHeader();
    createNavSidebar();
    createContentArea();
    createFooter();

    // Show filter panel by default
    showPanel(0);
    updateMuteButton();

    mclog::tagInfo(TAG, "wizard UI created");
}

void WizardUI::update()
{
    updateMeters();

    // Update headphone status
    if (_hpStatusLabel) {
        bool hp = GetHAL()->headPhoneDetect();
        lv_label_set_text(_hpStatusLabel,
            hp ? "HP: Connected" : "HP: ---");
        lv_obj_set_style_text_color(_hpStatusLabel,
            lv_color_hex(hp ? GOLD_BRIGHT : MUTED_TEXT), LV_PART_MAIN);
    }
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
    lv_label_set_text(_titleLabel, LV_SYMBOL_AUDIO "  HOWIZARD AUDIO ENCHANTMENT  " LV_SYMBOL_AUDIO);
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
    lv_label_set_text(_versionLabel, "v0.1");
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
        lv_obj_set_size(btn, NAV_W - 16, 70);
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

    int startY = 40;
    _navBtnFilter = makeNavBtn("FILTER", startY, 0);
    _navBtnEq     = makeNavBtn("EQ", startY + 100, 1);
    _navBtnOutput = makeNavBtn("OUTPUT", startY + 200, 2);

    // Decorative dividers between buttons
    for (int i = 0; i < 2; i++) {
        lv_obj_t* div = lv_obj_create(_navPanel);
        lv_obj_remove_style_all(div);
        lv_obj_set_size(div, NAV_W - 30, 1);
        lv_obj_set_pos(div, 15, startY + 82 + i * 100);
        lv_obj_set_style_bg_color(div, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(div, LV_OPA_COVER, LV_PART_MAIN);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Content area with three panels
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::createContentArea()
{
    _contentArea = lv_obj_create(_root);
    lv_obj_remove_style_all(_contentArea);
    lv_obj_set_size(_contentArea, CONTENT_W, CONTENT_H);
    lv_obj_set_pos(_contentArea, CONTENT_X, CONTENT_Y);
    lv_obj_set_style_bg_color(_contentArea, lv_color_hex(BG_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_contentArea, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(_contentArea, LV_OBJ_FLAG_SCROLLABLE);

    // Create all three panels (same size, same position - visibility toggled)
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

    _panelFilter = makePanel();
    _panelEq     = makePanel();
    _panelOutput = makePanel();

    createFilterPanel();
    createEqPanel();
    createOutputPanel();
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
    lv_slider_set_range(_hpfSlider, 20, 500);
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
    lv_slider_set_range(_lpfSlider, 2000, 20000);
    lv_slider_set_value(_lpfSlider, 18000, LV_ANIM_OFF);
    styleSliderWizard(_lpfSlider);
    lv_obj_add_event_cb(_lpfSlider, onLpfSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    _lpfValueLabel = createValueLabel(_panelFilter, "18000 Hz", 970, 285);

    // Decorative note at bottom
    lv_obj_t* noteLabel = lv_label_create(_panelFilter);
    lv_label_set_text(noteLabel, "Butterworth filters (Q = 0.707) for flat passband response");
    lv_obj_set_style_text_font(noteLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(noteLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(noteLabel, cx - 220, CONTENT_H - 60);
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
    createSectionLabel(_panelOutput, "MASTER VOLUME", 60, 80);

    _volumeSlider = lv_slider_create(_panelOutput);
    lv_obj_set_size(_volumeSlider, 600, 30);
    lv_obj_set_pos(_volumeSlider, 250, 85);
    lv_slider_set_range(_volumeSlider, 0, 100);
    lv_slider_set_value(_volumeSlider, 80, LV_ANIM_OFF);
    styleSliderWizard(_volumeSlider);
    lv_obj_add_event_cb(_volumeSlider, onVolumeSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    _volumeValueLabel = createValueLabel(_panelOutput, "80", 870, 85);

    // ── Output Gain ──
    createSectionLabel(_panelOutput, "OUTPUT GAIN", 60, 140);

    _gainSlider = lv_slider_create(_panelOutput);
    lv_obj_set_size(_gainSlider, 600, 30);
    lv_obj_set_pos(_gainSlider, 250, 145);
    lv_slider_set_range(_gainSlider, 0, 200);  // 0.0 to 2.0
    lv_slider_set_value(_gainSlider, 100, LV_ANIM_OFF);
    styleSliderWizard(_gainSlider);
    lv_obj_add_event_cb(_gainSlider, onGainSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    _gainValueLabel = createValueLabel(_panelOutput, "1.00x", 870, 145);

    // ── Mic Gain ──
    createSectionLabel(_panelOutput, "MIC GAIN", 60, 200);

    _micGainSlider = lv_slider_create(_panelOutput);
    lv_obj_set_size(_micGainSlider, 600, 30);
    lv_obj_set_pos(_micGainSlider, 250, 205);
    lv_slider_set_range(_micGainSlider, 0, 240);
    lv_slider_set_value(_micGainSlider, 180, LV_ANIM_OFF);
    styleSliderWizard(_micGainSlider);
    lv_obj_add_event_cb(_micGainSlider, onMicGainSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    _micGainValueLabel = createValueLabel(_panelOutput, "180", 870, 205);

    // ── Divider ──
    createDiamondDivider(_panelOutput, 260, 800);

    // ── VU Meters ──
    createSectionLabel(_panelOutput, "LEVEL METERS", 60, 285);

    // Left meter label
    lv_obj_t* lblL = lv_label_create(_panelOutput);
    lv_label_set_text(lblL, "L");
    lv_obj_set_style_text_font(lblL, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lblL, lv_color_hex(GOLD), LV_PART_MAIN);
    lv_obj_set_pos(lblL, 60, 320);

    // Left meter bar background
    lv_obj_t* meterBgL = lv_obj_create(_panelOutput);
    lv_obj_remove_style_all(meterBgL);
    lv_obj_set_size(meterBgL, 700, 32);
    lv_obj_set_pos(meterBgL, 90, 316);
    lv_obj_set_style_bg_color(meterBgL, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(meterBgL, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(meterBgL, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(meterBgL, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(meterBgL, 1, LV_PART_MAIN);
    lv_obj_remove_flag(meterBgL, LV_OBJ_FLAG_SCROLLABLE);

    _meterBarL = lv_obj_create(meterBgL);
    lv_obj_remove_style_all(_meterBarL);
    lv_obj_set_size(_meterBarL, 0, 28);
    lv_obj_set_pos(_meterBarL, 2, 2);
    lv_obj_set_style_bg_color(_meterBarL, lv_color_hex(METER_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_meterBarL, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_meterBarL, 2, LV_PART_MAIN);

    _meterPeakL = lv_obj_create(meterBgL);
    lv_obj_remove_style_all(_meterPeakL);
    lv_obj_set_size(_meterPeakL, 3, 28);
    lv_obj_set_pos(_meterPeakL, 2, 2);
    lv_obj_set_style_bg_color(_meterPeakL, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_meterPeakL, LV_OPA_COVER, LV_PART_MAIN);

    // Right meter label
    lv_obj_t* lblR = lv_label_create(_panelOutput);
    lv_label_set_text(lblR, "R");
    lv_obj_set_style_text_font(lblR, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lblR, lv_color_hex(GOLD), LV_PART_MAIN);
    lv_obj_set_pos(lblR, 60, 370);

    // Right meter bar background
    lv_obj_t* meterBgR = lv_obj_create(_panelOutput);
    lv_obj_remove_style_all(meterBgR);
    lv_obj_set_size(meterBgR, 700, 32);
    lv_obj_set_pos(meterBgR, 90, 366);
    lv_obj_set_style_bg_color(meterBgR, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(meterBgR, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(meterBgR, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(meterBgR, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(meterBgR, 1, LV_PART_MAIN);
    lv_obj_remove_flag(meterBgR, LV_OBJ_FLAG_SCROLLABLE);

    _meterBarR = lv_obj_create(meterBgR);
    lv_obj_remove_style_all(_meterBarR);
    lv_obj_set_size(_meterBarR, 0, 28);
    lv_obj_set_pos(_meterBarR, 2, 2);
    lv_obj_set_style_bg_color(_meterBarR, lv_color_hex(METER_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_meterBarR, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_meterBarR, 2, LV_PART_MAIN);

    _meterPeakR = lv_obj_create(meterBgR);
    lv_obj_remove_style_all(_meterPeakR);
    lv_obj_set_size(_meterPeakR, 3, 28);
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
        lv_obj_set_pos(sl, scalePositions[i], 402);
    }
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
    lv_label_set_text(bsLabel, "Block: 512");
    lv_obj_set_style_text_font(bsLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(bsLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(bsLabel, 450, 10);

    // Latency
    lv_obj_t* ltLabel = lv_label_create(_footerBar);
    lv_label_set_text(ltLabel, "Latency: ~10.7ms");
    lv_obj_set_style_text_font(ltLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ltLabel, lv_color_hex(MUTED_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(ltLabel, 620, 10);
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel switching
// ─────────────────────────────────────────────────────────────────────────────

void WizardUI::showPanel(int index)
{
    _activePanel = index;

    if (_panelFilter) lv_obj_add_flag(_panelFilter, LV_OBJ_FLAG_HIDDEN);
    if (_panelEq)     lv_obj_add_flag(_panelEq, LV_OBJ_FLAG_HIDDEN);
    if (_panelOutput) lv_obj_add_flag(_panelOutput, LV_OBJ_FLAG_HIDDEN);

    switch (index) {
        case 0: if (_panelFilter) lv_obj_remove_flag(_panelFilter, LV_OBJ_FLAG_HIDDEN); break;
        case 1: if (_panelEq)     lv_obj_remove_flag(_panelEq, LV_OBJ_FLAG_HIDDEN); break;
        case 2: if (_panelOutput) lv_obj_remove_flag(_panelOutput, LV_OBJ_FLAG_HIDDEN); break;
    }

    updateNavHighlight();
}

void WizardUI::updateNavHighlight()
{
    lv_obj_t* btns[] = {_navBtnFilter, _navBtnEq, _navBtnOutput};
    for (int i = 0; i < 3; i++) {
        if (!btns[i]) continue;
        if (i == _activePanel) {
            lv_obj_set_style_bg_color(btns[i], lv_color_hex(0x1A1540), LV_PART_MAIN);
            lv_obj_set_style_border_color(btns[i], lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
            // Update child label color
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
    int cx = CONTENT_W / 2;
    int halfW = width / 2;

    // Left line
    lv_obj_t* lineL = lv_obj_create(parent);
    lv_obj_remove_style_all(lineL);
    lv_obj_set_size(lineL, halfW - 10, 1);
    lv_obj_set_pos(lineL, cx - halfW, y);
    lv_obj_set_style_bg_color(lineL, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lineL, LV_OPA_COVER, LV_PART_MAIN);

    // Diamond
    lv_obj_t* diamond = lv_obj_create(parent);
    lv_obj_remove_style_all(diamond);
    lv_obj_set_size(diamond, 8, 8);
    lv_obj_set_pos(diamond, cx - 4, y - 3);
    lv_obj_set_style_bg_color(diamond, lv_color_hex(GOLD_BRIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(diamond, 200, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(diamond, 450, LV_PART_MAIN);

    // Right line
    lv_obj_t* lineR = lv_obj_create(parent);
    lv_obj_remove_style_all(lineR);
    lv_obj_set_size(lineR, halfW - 10, 1);
    lv_obj_set_pos(lineR, cx + 10, y);
    lv_obj_set_style_bg_color(lineR, lv_color_hex(DARK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lineR, LV_OPA_COVER, LV_PART_MAIN);

    return diamond;
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
    int val = lv_slider_get_value(slider);  // 0-200
    float gain = (float)val / 100.0f;       // 0.00-2.00

#ifdef ESP_PLATFORM
    AudioEngine::getInstance().setOutputGain(gain);
#endif

    if (ui->_gainValueLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2fx", (double)gain);
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
