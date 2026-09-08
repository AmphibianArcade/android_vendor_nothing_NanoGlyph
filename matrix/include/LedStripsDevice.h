// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <utility>

#include "DeviceConfig.h"

namespace vendor::nukisystems::nanoglyph::impl {

class LedStripsDevice {
public:
    explicit LedStripsDevice(DeviceConfig config)
        : mConfig(std::move(config)) {}

    ~LedStripsDevice();

    LedStripsDevice(const LedStripsDevice&) = delete;
    LedStripsDevice& operator=(const LedStripsDevice&) = delete;

    bool open();
    void close();

    bool isOpen() const {
        return mFd >= 0 && mRing != nullptr;
    }

    const DeviceConfig& config() const {
        return mConfig;
    }

    int slotCount() const {
        return mConfig.numSlots;
    }

    size_t maxPixelsPerSlot() const {
        return mConfig.mmapDataElements;
    }

    bool writeSlot(
            int slotIndex,
            const uint8_t* frameData,
            size_t pixelCount,
            uint8_t brightness);

    void resetSlots();

    bool startStream(int pixelCount);
    bool stopStream();

    bool setAlwaysOn(bool on);

    bool waitFrameEvent(char* outEventCode);
    void interruptWait();

    int lastErrno() const {
        return mLastErrno;
    }

private:
    bool writeSlotAw20144(
            int slotIndex,
            const uint8_t* frameData,
            size_t pixelCount,
            uint8_t brightness);

    bool writeSlotSpiMatrix(
            int slotIndex,
            const uint8_t* frameData,
            size_t pixelCount,
            uint8_t brightness);

    void linkAndInvalidateSlot(int index);

    DeviceConfig mConfig;

    int mFd = -1;
    void* mRing = nullptr;

    std::atomic<int> mLastErrno{0};
    std::atomic<bool> mWaitInterrupted{false};

    uint8_t* slotPtr(int index) const;
};

}  // namespace vendor::nukisystems::nanoglyph::impl
