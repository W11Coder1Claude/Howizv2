/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <memory>
#include <string>
#include <mooncake_log.h>

/* -------------------------------------------------------------------------- */
/*                                  Singleton                                 */
/* -------------------------------------------------------------------------- */

static std::unique_ptr<hal::HalBase> _hal_instance;
static const std::string _tag = "hal";

hal::HalBase* hal::Get()
{
    if (!_hal_instance) {
        mclog::tagWarn(_tag, "getting null hal, auto inject base");
        _hal_instance = std::make_unique<HalBase>();
    }
    return _hal_instance.get();
}

void hal::Inject(std::unique_ptr<HalBase> hal)
{
    if (!hal) {
        mclog::tagError(_tag, "pass null hal");
        return;
    }

    Destroy();
    _hal_instance = std::move(hal);

    mclog::tagInfo(_tag, "injecting hal type: {}", _hal_instance->type());

    mclog::tagInfo(_tag, "invoke init");
    _hal_instance->init();
    mclog::tagInfo(_tag, "hal injected");
}

void hal::Destroy()
{
    _hal_instance.reset();
}

bool hal::Check()
{
    if (_hal_instance) {
        return true;
    }
    return false;
}
