/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <cstdint>

/**
 * @brief Wizard-themed audio control UI
 *
 * Layout (1280x720):
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  ◆ HOWIZARD AUDIO ENCHANTMENT ◆                  [MUTE]  v0.1 │
 * ├────────┬────────────────────────────────────────────────────────┤
 * │ FILTER │  Swipeable content panels:                            │
 * │ ══════ │  Panel 1: FILTERS - HPF/LPF controls                 │
 * │ EQ     │  Panel 2: EQUALIZER - 3-band parametric              │
 * │ ══════ │  Panel 3: OUTPUT - Volume, Gain, VU meters            │
 * │ OUTPUT │  Panel 4: VOICE - Voice Exclusion (NLMS)              │
 * │ ══════ │                                                       │
 * │ VOICE  │                                                       │
 * ├────────┴────────────────────────────────────────────────────────┤
 * │  ◇ HP: --- ◇  ◇ 48kHz ◇  ◇ Block: 480 ◇                     │
 * └─────────────────────────────────────────────────────────────────┘
 */
class WizardUI {
public:
    void create(lv_obj_t* parent);
    void update();       // Called periodically for meter animation
    void destroy();

private:
    // Color palette
    static constexpr uint32_t BG_DARK      = 0x0A0A1A;
    static constexpr uint32_t BG_PANEL     = 0x12102A;
    static constexpr uint32_t GOLD         = 0xE8D5B5;
    static constexpr uint32_t GOLD_BRIGHT  = 0xFFD700;
    static constexpr uint32_t LAVENDER     = 0x8B7EC8;
    static constexpr uint32_t CYAN_GLOW    = 0x4488FF;
    static constexpr uint32_t DARK_BORDER  = 0x2A2050;
    static constexpr uint32_t METER_GREEN  = 0x44CC66;
    static constexpr uint32_t METER_YELLOW = 0xCCAA33;
    static constexpr uint32_t METER_RED    = 0xCC4444;
    static constexpr uint32_t MUTED_TEXT   = 0x4A4A6A;

    // Layout constants
    static constexpr int SCREEN_W    = 1280;
    static constexpr int SCREEN_H    = 720;
    static constexpr int HEADER_H    = 60;
    static constexpr int FOOTER_H    = 40;
    static constexpr int NAV_W       = 120;
    static constexpr int CONTENT_X   = NAV_W;
    static constexpr int CONTENT_Y   = HEADER_H;
    static constexpr int CONTENT_W   = SCREEN_W - NAV_W;
    static constexpr int CONTENT_H   = SCREEN_H - HEADER_H - FOOTER_H;

    // Root container
    lv_obj_t* _root = nullptr;

    // Header
    lv_obj_t* _headerBar = nullptr;
    lv_obj_t* _titleLabel = nullptr;
    lv_obj_t* _muteBtn = nullptr;
    lv_obj_t* _muteBtnLabel = nullptr;
    lv_obj_t* _versionLabel = nullptr;

    // Navigation sidebar
    lv_obj_t* _navPanel = nullptr;
    lv_obj_t* _navBtnFilter = nullptr;
    lv_obj_t* _navBtnEq = nullptr;
    lv_obj_t* _navBtnOutput = nullptr;
    lv_obj_t* _navBtnVoice = nullptr;
    int _activePanel = 0;

    // Content panels
    lv_obj_t* _contentArea = nullptr;
    lv_obj_t* _panelFilter = nullptr;
    lv_obj_t* _panelEq = nullptr;
    lv_obj_t* _panelOutput = nullptr;
    lv_obj_t* _panelVoice = nullptr;

    // Filter panel controls
    lv_obj_t* _hpfToggle = nullptr;
    lv_obj_t* _hpfSlider = nullptr;
    lv_obj_t* _hpfValueLabel = nullptr;
    lv_obj_t* _lpfToggle = nullptr;
    lv_obj_t* _lpfSlider = nullptr;
    lv_obj_t* _lpfValueLabel = nullptr;

    // NS controls (on filter panel)
    lv_obj_t* _nsToggle = nullptr;
    lv_obj_t* _nsModeBtn0 = nullptr;
    lv_obj_t* _nsModeBtn1 = nullptr;
    lv_obj_t* _nsModeBtn2 = nullptr;
    lv_obj_t* _nsModeLabel = nullptr;
    int _nsActiveMode = 1;

    // EQ panel controls
    lv_obj_t* _eqLowSlider = nullptr;
    lv_obj_t* _eqLowLabel = nullptr;
    lv_obj_t* _eqMidSlider = nullptr;
    lv_obj_t* _eqMidLabel = nullptr;
    lv_obj_t* _eqHighSlider = nullptr;
    lv_obj_t* _eqHighLabel = nullptr;

    // Output panel controls
    lv_obj_t* _volumeSlider = nullptr;
    lv_obj_t* _volumeValueLabel = nullptr;
    lv_obj_t* _gainSlider = nullptr;
    lv_obj_t* _gainValueLabel = nullptr;
    lv_obj_t* _micGainSlider = nullptr;
    lv_obj_t* _micGainValueLabel = nullptr;
    lv_obj_t* _meterBarL = nullptr;
    lv_obj_t* _meterBarR = nullptr;
    lv_obj_t* _meterPeakL = nullptr;
    lv_obj_t* _meterPeakR = nullptr;

    // Voice Exclusion panel controls
    lv_obj_t* _veToggle = nullptr;
    lv_obj_t* _veHpStatusLabel = nullptr;
    // Reference signal controls
    lv_obj_t* _veRefGainSlider = nullptr;
    lv_obj_t* _veRefGainValueLabel = nullptr;
    lv_obj_t* _veRefHpfSlider = nullptr;
    lv_obj_t* _veRefHpfValueLabel = nullptr;
    lv_obj_t* _veRefLpfSlider = nullptr;
    lv_obj_t* _veRefLpfValueLabel = nullptr;
    lv_obj_t* _veHpMeterBar = nullptr;
    lv_obj_t* _veHpMeterPeak = nullptr;
    // Cancellation controls
    lv_obj_t* _veBlendSlider = nullptr;
    lv_obj_t* _veBlendValueLabel = nullptr;
    lv_obj_t* _veStepSlider = nullptr;
    lv_obj_t* _veStepValueLabel = nullptr;
    lv_obj_t* _veFilterBtn32 = nullptr;
    lv_obj_t* _veFilterBtn64 = nullptr;
    lv_obj_t* _veFilterBtn128 = nullptr;
    int _veActiveFilterLen = 128;
    lv_obj_t* _veAttenSlider = nullptr;
    lv_obj_t* _veAttenValueLabel = nullptr;

    // Footer
    lv_obj_t* _footerBar = nullptr;
    lv_obj_t* _hpStatusLabel = nullptr;

    // Build helpers
    void createHeader();
    void createNavSidebar();
    void createContentArea();
    void createFilterPanel();
    void createEqPanel();
    void createOutputPanel();
    void createVoicePanel();
    void createFooter();
    void showPanel(int index);
    void updateNavHighlight();
    void updateMuteButton();
    void updateMeters();

    // Style helpers
    void styleSliderWizard(lv_obj_t* slider);
    void styleToggleWizard(lv_obj_t* btn);
    lv_obj_t* createSectionLabel(lv_obj_t* parent, const char* text, int x, int y);
    lv_obj_t* createValueLabel(lv_obj_t* parent, const char* text, int x, int y);
    lv_obj_t* createDiamondDivider(lv_obj_t* parent, int y, int width);

    // Callbacks (static with user_data = WizardUI*)
    static void onNavBtnClicked(lv_event_t* e);
    static void onMuteBtnClicked(lv_event_t* e);
    static void onHpfToggle(lv_event_t* e);
    static void onLpfToggle(lv_event_t* e);
    static void onHpfSliderChanged(lv_event_t* e);
    static void onLpfSliderChanged(lv_event_t* e);
    static void onEqSliderChanged(lv_event_t* e);
    static void onVolumeSliderChanged(lv_event_t* e);
    static void onGainSliderChanged(lv_event_t* e);
    static void onMicGainSliderChanged(lv_event_t* e);
    static void onNsToggle(lv_event_t* e);
    static void onNsModeClicked(lv_event_t* e);
    static void onVeToggle(lv_event_t* e);
    static void onVeRefGainChanged(lv_event_t* e);
    static void onVeRefHpfChanged(lv_event_t* e);
    static void onVeRefLpfChanged(lv_event_t* e);
    static void onVeBlendChanged(lv_event_t* e);
    static void onVeStepChanged(lv_event_t* e);
    static void onVeFilterLenClicked(lv_event_t* e);
    static void onVeAttenChanged(lv_event_t* e);
};
