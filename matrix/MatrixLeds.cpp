// SPDX-License-Identifier: Apache-2.0
#include <fcntl.h>
#include <unistd.h>

#include <MatrixLeds.h>
#include <DeviceConfigs.h> 

#include <android-base/logging.h>
#include <android-base/file.h>
#include <android-base/properties.h>

#include "SysfsDefs.h"
#include "ErrorConstants.h"

namespace aidl::vendor::nukisystems::nanoglyph {

using aidl::vendor::nukisystems::nanoglyph::MatrixPattern;

using ::vendor::nukisystems::nanoglyph::impl::LedStripsDevice;
using ::vendor::nukisystems::nanoglyph::impl::MatrixConfig;

namespace {

constexpr int kDeviceMaxScale = 4095; 
uint16_t scaleTo12Bit(uint8_t value8) {
    return static_cast<uint16_t>((static_cast<int>(value8) * kDeviceMaxScale + 127) / 255);
}

MatrixConfig getConfig() {
    std::string model = ::android::base::GetProperty("ro.product.model", "");
    if (model.empty()) {
        LOG(FATAL) << "Unable to determine device model. Cannot continue!";
    } else if ((model != MODEL_FROGGERPRO) && (model != MODEL_METROID)) {
        LOG(FATAL) << "Unsupported model: '" << model << "'";
    }
    LOG(INFO) << "Selecting matrix LED config for device model: '" << model << "'";
    return makeConfig(model);
}

}  // namespace

MatrixLeds::MatrixLeds() : mDevice(getConfig()) {}

MatrixLeds::~MatrixLeds() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mState == StreamState::STREAMING) {
            mDevice.stopStream();
        }
    }
    mFeederRunning = false;
    if (mFeederThread.joinable()) {
        mFeederThread.join();
    }
}

bool MatrixLeds::init() {
    std::lock_guard<std::mutex> lock(mMutex);
    mDeviceAvailable = mDevice.open();
    if (!mDeviceAvailable) {
        LOG(ERROR) << "MatrixLeds::init: device open failed, "
                   << "service starting in unavailable state";
    }
    return mDeviceAvailable;
}

::ndk::ScopedAStatus MatrixLeds::isAvailable(bool* _aidl_return) {
    std::lock_guard<std::mutex> lock(mMutex);
    *_aidl_return = mDeviceAvailable;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::getDeviceInfo(DeviceInfo* _aidl_return) {
    const auto& cfg = mDevice.config();
    DeviceInfo info;
    info.name = cfg.name;
    info.pixelCount = cfg.pixelCount;
    info.bytesPerPixel = cfg.bytesPerPixel;
    info.maxFramesPerPattern = mDevice.slotCount();

    info.nativeFps = 60;
    info.supportsAlwaysOn = true;
    info.supportsImax = false;
    *_aidl_return = std::move(info);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::getPixelCount(int32_t* _aidl_return) {
    *_aidl_return = mDevice.config().pixelCount;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::loadPattern(const MatrixPattern& pattern) {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto& cfg = mDevice.config();

    if (!mDeviceAvailable) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
    }
    if (static_cast<size_t>(pattern.pixelsPerFrame) != cfg.pixelCount) {
        LOG(ERROR) << cfg.name << ": loadPattern: pixelsPerFrame "
                   << pattern.pixelsPerFrame << " != expected " << cfg.pixelCount;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }

    if (pattern.frameCount <= 0) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }
    if (mState == StreamState::STREAMING) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrBusy);
    }

    const size_t expectedBytes =
            static_cast<size_t>(pattern.frameCount) * pattern.pixelsPerFrame;
    if (pattern.frameData.size() != expectedBytes) {
        LOG(ERROR) << cfg.name << ": loadPattern: frameData size "
                   << pattern.frameData.size() << " != expected " << expectedBytes;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }

    if (expectedBytes > 900'000) {
        LOG(ERROR) << cfg.name << ": loadPattern: pattern too large ("
                   << expectedBytes << " bytes)";
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }

    mSequence.clear();
    mSequence.reserve(pattern.frameCount);

    for (int f = 0; f < pattern.frameCount; ++f) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(pattern.frameData.data()) +
                              static_cast<size_t>(f) * pattern.pixelsPerFrame;
        std::vector<uint16_t> scaledFrame(pattern.pixelsPerFrame);
        for (int p = 0; p < pattern.pixelsPerFrame; ++p) {
            scaledFrame[p] = scaleTo12Bit(src[p]);
        }
        mSequence.push_back(std::move(scaledFrame));
    }

    mSequenceCursor = 0;
    mSequenceBrightness = static_cast<uint8_t>(std::clamp(pattern.brightness, 0, 255));
    mPatternLoaded = true;

    LOG(INFO) << cfg.name << ": loadPattern: loaded " << pattern.frameCount
              << " total frames (ring holds " << mDevice.slotCount() << " at a time)";
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::startStream() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mDeviceAvailable) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
    }
    if (!mPatternLoaded || mSequence.empty()) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNoPatternLoaded);
    }
    if (mState == StreamState::STREAMING) {
        return ::ndk::ScopedAStatus::ok();
    }

    mSequenceCursor = 0;
    setStateLocked(StreamState::STREAMING);

    if (!mFeederRunning.exchange(true)) {
    if (mFeederThread.joinable()) mFeederThread.join();
        mFeederThread = std::thread(&MatrixLeds::feederLoop, this);
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::stopStream() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mDeviceAvailable) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
        }
        if (mState != StreamState::STREAMING) {
            return ::ndk::ScopedAStatus::ok();
        }
        mDevice.stopStream();
        setStateLocked(StreamState::STOPPED);
        mFeederRunning = false;
    }  // lock released

    if (mFeederThread.joinable()) {
        mFeederThread.join();
    }
    return ::ndk::ScopedAStatus::ok();
}

void MatrixLeds::feederLoop() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mDevice.resetSlots();
        int slot = 0;
        while (slot < mDevice.slotCount() && mSequenceCursor < mSequence.size()) {
            mDevice.writeSlot(slot, reinterpret_cast<const uint8_t*>(mSequence[mSequenceCursor].data()),
                               mSequence[mSequenceCursor].size(), mSequenceBrightness);
            ++slot;
            ++mSequenceCursor;
        }
        mDevice.startStream(mDevice.config().pixelCount);
    }

    while (mFeederRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        std::lock_guard<std::mutex> lock(mMutex);
        if (mState != StreamState::STREAMING) break;

        for (int i = 0; i < mDevice.slotCount(); ++i) {
            if (mDevice.slotStatus(i) != static_cast<uint8_t>(MmapBufStatus::INVALID)) {
                continue;  // kernel hasn't consumed this slot yet
            }
            if (mSequenceCursor >= mSequence.size()) {
                if (mRepeatForever) {
                    mSequenceCursor = 0;
                } else {
                    continue;
                }
            }
            mDevice.writeSlot(i, reinterpret_cast<const uint8_t*>(mSequence[mSequenceCursor].data()),
                               mSequence[mSequenceCursor].size(), mSequenceBrightness);
            ++mSequenceCursor;
        }

        if (!mRepeatForever && mSequenceCursor >= mSequence.size()) {
            bool allDrained = true;
            for (int i = 0; i < mDevice.slotCount(); ++i) {
                if (mDevice.slotStatus(i) == static_cast<uint8_t>(MmapBufStatus::VALID)) {
                    allDrained = false;
                    break;
                }
            }
            if (allDrained) {
                setStateLocked(StreamState::STOPPED);
                mSequence.clear();
                mSequence.shrink_to_fit();
                mPatternLoaded = false;
                break;
            }
        }
    }
    mFeederRunning = false;
}

::ndk::ScopedAStatus MatrixLeds::getStreamState(StreamState* _aidl_return) {
    std::lock_guard<std::mutex> lock(mMutex);
    *_aidl_return = mState;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::setSolidBrightness(int32_t brightness) {
    {
        std::lock_guard<std::mutex> lock(mMutex);

        if (!mDeviceAvailable) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
        }
        if (brightness < 0 || brightness > 255) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
        }

        if (mState == StreamState::STREAMING) {
            mDevice.stopStream();
            setStateLocked(StreamState::STOPPED);
            mFeederRunning = false;
                    }
    } 

    if (mFeederThread.joinable()) {
        mFeederThread.join();  
    }

    const auto& cfg = mDevice.config();
    std::string str = std::to_string(brightness);
    if (!::android::base::WriteStringToFile(str, cfg.allBrightnessPath)) {
        return ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::setSingleBrightness(int32_t index, int32_t brightness) {
    const auto& cfg = mDevice.config();
    {
        std::lock_guard<std::mutex> lock(mMutex);

        if (!mDeviceAvailable) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
        }
        if (brightness < 0 || brightness > 255) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
        }

        if (index < 0 || static_cast<size_t>(index) >= cfg.pixelCount) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
        }

        if (mState == StreamState::STREAMING) {
            mDevice.stopStream();
            setStateLocked(StreamState::STOPPED);
            mFeederRunning = false;
        }
    } 

    if (mFeederThread.joinable()) {
        mFeederThread.join();
    }

    std::string str_brightness = std::to_string(brightness);
    std::string str_index = std::to_string(index);
    if (!::android::base::WriteStringToFile(str_index + " " + str_brightness, cfg.singleBrightnessPath)) {
        return ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::setFrame(const std::vector<int32_t>& brightness) {
    const auto& cfg = mDevice.config();
    {
        std::lock_guard<std::mutex> lock(mMutex);

        if (!mDeviceAvailable) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
        }

        if (static_cast<int32_t>(brightness.size()) != static_cast<int32_t>(cfg.pixelCount)) {
            LOG(ERROR) << cfg.name << ": setFrame: length " << brightness.size()
                    << " != expected " << cfg.pixelCount;
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
        }

        if (mState == StreamState::STREAMING) {
            mDevice.stopStream();
            setStateLocked(StreamState::STOPPED);
            mFeederRunning = false;
        }
    }

    if (mFeederThread.joinable()) {
        mFeederThread.join();
    }

    std::string payload;
    payload.reserve(brightness.size() * 4);
    for (size_t i = 0; i < brightness.size(); ++i) {
        if (brightness[i] < 0 || brightness[i] > 255) {
            LOG(ERROR) << cfg.name << ": setFrame: value " << brightness[i]
                       << " at index " << i << " out of range 0-255";
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
        }
        if (i > 0) payload += ' ';
        payload += std::to_string(brightness[i]);
    }

    const char* kPath = kFrameBrightnessPath.c_str();
    int fd = ::open(kPath, O_WRONLY);
    if (fd < 0) {
        LOG(ERROR) << cfg.name << ": failed to open " << kPath << ": "
                   << strerror(errno);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
    }
    ssize_t n = ::write(fd, payload.data(), payload.size());
    ::close(fd);
    if (n < 0 || static_cast<size_t>(n) != payload.size()) {
        LOG(ERROR) << cfg.name << ": write to frame_brightness failed: "
                   << strerror(errno);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::setImax(int32_t imax) {
    LOG(WARNING) << "setImax(" << imax << ") not yet implemented for this device";
    return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
}

::ndk::ScopedAStatus MatrixLeds::setAlwaysOn(bool enabled) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mDeviceAvailable) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
    }
    if (!mDevice.setAlwaysOn(enabled)) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::setCallback(
        const std::shared_ptr<IMatrixLedsCallback>& callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mCallback = callback;
    return ::ndk::ScopedAStatus::ok();
}

void MatrixLeds::setStateLocked(StreamState newState) {
    mState = newState;
    if (mCallback) {
        mCallback->onStreamStateChanged(newState);
    }
}

void MatrixLeds::notifyStateChanged(StreamState newState) {
    std::shared_ptr<IMatrixLedsCallback> cb;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        cb = mCallback;
    }
    if (cb) {
        cb->onStreamStateChanged(newState);
    }
}

}  // namespace aidl::vendor::nukisystems::nanoglyph
