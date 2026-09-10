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
    void playbackMonitorLoop();

    void setStateLocked(StreamState newState);  
    void notifyStateChanged(StreamState newState);

    LedStripsDevice mDevice;

    std::mutex mMutex;
    std::vector<std::vector<uint16_t>> mLoadedFrames;
    uint8_t mLoadedBrightness8 = 0;                 
    bool mDeviceAvailable = false;
    bool mPatternLoaded = false;
    int mLoadedFrameCount = 0;
    int mLoadedPixelsPerFrame = 0;
    StreamState mState = StreamState::STOPPED;
    std::shared_ptr<IMatrixLedsCallback> mCallback;

    std::thread mMonitorThread;
    std::atomic<bool> mMonitorRunning{false};
};

}  // namespace aidl::vendor::nukisystems::nanoglyph
