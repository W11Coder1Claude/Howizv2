/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <memory>
#include <hal/hal.h>
#include "app_wizard_splash/app_wizard_splash.h"
#include "app_audio_control/app_audio_control.h"

/**
 * @brief Install and launch the Howizard apps
 */
inline void on_startup_anim()
{
    // No separate startup animation - splash screen handles it
}

inline void on_install_apps()
{
    mooncake::GetMooncake().installApp(std::make_unique<AppWizardSplash>());
    mooncake::GetMooncake().installApp(std::make_unique<AppAudioControl>());
}
