// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "DeviceConfig.h"
#include "LedStripsDevice.h"
#include "SysfsDefs.h"

inline vendor::nukisystems::nanoglyph::impl::DeviceConfig makeConfig() {
    using vendor::nukisystems::nanoglyph::impl::DeviceConfig;
    using vendor::nukisystems::nanoglyph::impl::BrightnessFormat;
    using vendor::nukisystems::nanoglyph::impl::DeviceType;

    DeviceConfig cfg{};

#if MATRIX_LEDS_COUNT == MATRIXLEDS_FROGGERPRO_PIXEL_COUNT

    using vendor::nukisystems::nanoglyph::impl::LedMmapBufAwinic;

    cfg.deviceType = DeviceType::AW20144;
    cfg.name = "aw20144";
    cfg.pixelCount = MATRIXLEDS_FROGGERPRO_PIXEL_COUNT;
    cfg.bytesPerPixel = 2;

    cfg.mmapPageOrder = 1;
    cfg.mmapDataElements = 500;
    cfg.numSlots = 8;
    cfg.slotSize = sizeof(LedMmapBufAwinic);
    cfg.ringTotalSize = cfg.slotSize * cfg.numSlots;

    cfg.brightnessFormat = BrightnessFormat::U8;

    cfg.allBrightnessPath = kAllBrightnessPathAwinic.c_str();

#elif MATRIX_LEDS_COUNT == MATRIXLEDS_METROID_PIXEL_COUNT

    using vendor::nukisystems::nanoglyph::impl::LedMmapBufSPI;

    cfg.deviceType = DeviceType::SPI_MATRIX;
    cfg.name = "spi_matrix";
    cfg.pixelCount = MATRIXLEDS_METROID_PIXEL_COUNT;
    cfg.bytesPerPixel = 2;

    cfg.mmapPageOrder = 2;
    cfg.mmapDataElements = 1012;
    cfg.numSlots = 8;
    cfg.slotSize = sizeof(LedMmapBufSPI);
    cfg.ringTotalSize = cfg.slotSize * cfg.numSlots;

    cfg.brightnessFormat = BrightnessFormat::U16;

    cfg.allBrightnessPath = kAllBrightnessPathSPI.c_str();

#else

#error "Unsupported MATRIX_LEDS_COUNT"

#endif
    return cfg;
}
