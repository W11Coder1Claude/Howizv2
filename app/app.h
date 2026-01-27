/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <functional>

namespace app {

struct InitCallback_t {
    std::function<void()> onHalInjection = nullptr;
};

void Init(InitCallback_t callback);
void Update();
bool IsDone();
void Destroy();

}  // namespace app
