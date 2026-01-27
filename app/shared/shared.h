/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <smooth_ui_toolkit.h>

namespace shared_data {

struct SharedData_t {
    smooth_ui_toolkit::Signal<std::string> systemStateEvents;
    smooth_ui_toolkit::Signal<std::string> inputEvents;
};

SharedData_t* Get();
void Destroy();

}  // namespace shared_data

inline shared_data::SharedData_t* GetSharedData()
{
    return shared_data::Get();
}

inline smooth_ui_toolkit::Signal<std::string>& GetSystemStateEvents()
{
    return GetSharedData()->systemStateEvents;
}

inline smooth_ui_toolkit::Signal<std::string>& GetInputEvents()
{
    return GetSharedData()->inputEvents;
}
