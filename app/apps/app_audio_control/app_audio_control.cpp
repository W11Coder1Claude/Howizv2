/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_audio_control.h"
#include "view/wizard_ui.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>

#ifdef ESP_PLATFORM
#include "hal/components/audio_engine.h"
#include "hal/components/profile_manager.h"
#endif

using namespace mooncake;

static const std::string _tag = "AudioControl";

AppAudioControl::AppAudioControl()
{
    setAppInfo().name = "AppAudioControl";
}

void AppAudioControl::onCreate()
{
    mclog::tagInfo(_tag, "on create");
    // Open immediately - UI will appear behind splash screen,
    // becoming visible when splash closes and deletes its full-screen bg
    open();
}

void AppAudioControl::onOpen()
{
    mclog::tagInfo(_tag, "on open");

#ifdef ESP_PLATFORM
    // Start the audio engine
    AudioEngine::getInstance().start();
    mclog::tagInfo(_tag, "audio engine started");

    // Try to load default profile from SD card
    {
        AudioEngineParams params = AudioEngine::getInstance().getParams();
        if (ProfileManager::loadDefaultProfile(params)) {
            AudioEngine::getInstance().setParams(params);
            mclog::tagInfo(_tag, "default profile loaded from SD");
        }
    }
#endif

    // Create the wizard UI
    {
        LvglLockGuard lock;
        _ui = new WizardUI();
        _ui->create(lv_screen_active());
    }

    _frameCount = 0;
    mclog::tagInfo(_tag, "audio control app opened");
}

void AppAudioControl::onRunning()
{
    _frameCount++;

    // Update UI meters at ~15fps (every 4th frame at ~60fps)
    if (_frameCount % 4 != 0) return;

    LvglLockGuard lock;
    if (_ui) {
        _ui->update();
    }
}

void AppAudioControl::onClose()
{
    mclog::tagInfo(_tag, "on close");

#ifdef ESP_PLATFORM
    // Stop the audio engine
    AudioEngine::getInstance().stop();
#endif

    LvglLockGuard lock;
    if (_ui) {
        _ui->destroy();
        delete _ui;
        _ui = nullptr;
    }
}
