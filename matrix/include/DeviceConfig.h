// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

#include "LedMmap.h"
#include "SysfsDefs.h"

namespace vendor::nukisystems::nanoglyph::impl {

enum class BrightnessFormat {
    U8,
    U16,
};

enum class DeviceType {
    AW20144,
    SPI_MATRIX,
};

struct DeviceConfig {
    DeviceType deviceType;

    const char* name = nullptr;
    const char* devicePath = "/dev/matrix-leds";

    size_t pixelCount = 0;
    uint32_t bytesPerPixel = 2;

    uint32_t mmapPageOrder = 0;
    uint32_t mmapDataElements = 0;

    int32_t numSlots = kNumSlots;
    uint32_t slotSize = 0;
    uint32_t ringTotalSize = 0;

    BrightnessFormat brightnessFormat = BrightnessFormat::U8;

    uint32_t ioctlStreamMode = LED_STRIPS_STREAM_MODE;
    uint32_t ioctlStopMode = LED_STRIPS_STOP_MODE;
    uint32_t ioctlAlwaysOn = LED_STRIPS_ALWAYS_ON;
    uint32_t ioctlFreqSet = LED_STRIPS_FREQ_SET;
    const char* allBrightnessPath = nullptr;

    const char* singleBrightnessPath = kSingleBrightnessPath.c_str();
};

}  // namespace vendor::nukisystems::nanoglyph::impl
