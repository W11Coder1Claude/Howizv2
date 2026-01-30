/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <memory>
#include <hal/hal.h>
#include "app_audio_control/app_audio_control.h"

/**
 * @brief Install and launch the Howizard apps
 */
inline void on_startup_anim()
{
    // Splash screen removed for faster boot and lower memory usage
}

inline void on_install_apps()
{
    // Direct launch to audio control - no splash screen
    mooncake::GetMooncake().installApp(std::make_unique<AppAudioControl>());
}
