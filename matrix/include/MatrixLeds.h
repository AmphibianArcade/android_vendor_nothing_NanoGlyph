// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <aidl/vendor/nukisystems/nanoglyph/BnMatrixLeds.h>
#include <aidl/vendor/nukisystems/nanoglyph/IMatrixLedsCallback.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "LedStripsDevice.h"

namespace aidl::vendor::nukisystems::nanoglyph {

using ::vendor::nukisystems::nanoglyph::impl::LedStripsDevice;
using ::vendor::nukisystems::nanoglyph::impl::MatrixConfig;
using ::vendor::nukisystems::nanoglyph::impl::MmapBufStatus;

class MatrixLeds : public BnMatrixLeds {
public:

    MatrixLeds();
    ~MatrixLeds() override;

    bool init();

    ::ndk::ScopedAStatus isAvailable(bool* _aidl_return) override;
    ::ndk::ScopedAStatus getDeviceInfo(DeviceInfo* _aidl_return) override;
    ::ndk::ScopedAStatus getPixelCount(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus loadPattern(const MatrixPattern& pattern) override;
    ::ndk::ScopedAStatus startStream() override;
    ::ndk::ScopedAStatus stopStream() override;
    ::ndk::ScopedAStatus getStreamState(StreamState* _aidl_return) override;
    ::ndk::ScopedAStatus setSolidBrightness(int32_t brightness) override;
    ::ndk::ScopedAStatus setSingleBrightness(int32_t index, int32_t brightness) override;
    ::ndk::ScopedAStatus setFrame(const std::vector<int32_t>& brightness) override;
    ::ndk::ScopedAStatus setImax(int32_t imax) override;
    ::ndk::ScopedAStatus setAlwaysOn(bool enabled) override;
    ::ndk::ScopedAStatus setCallback(
            const std::shared_ptr<IMatrixLedsCallback>& callback) override;

private:
    void feederLoop();

    void setStateLocked(StreamState newState);
    void notifyStateChanged(StreamState newState);

    LedStripsDevice mDevice;

    std::mutex mMutex;
    bool mDeviceAvailable = false;
    std::vector<std::vector<uint16_t>> mSequence;
    size_t mSequenceCursor = 0;
    uint8_t mSequenceBrightness = 0;
    bool mRepeatForever = false;
    std::thread mFeederThread;
    std::atomic<bool> mFeederRunning{false};
    bool mPatternLoaded = false;
    StreamState mState = StreamState::STOPPED;
    std::shared_ptr<IMatrixLedsCallback> mCallback;
};

}  // namespace aidl::vendor::nukisystems::nanoglyph
