// SPDX-License-Identifier: Apache-2.0
#include <fcntl.h>
#include <unistd.h>

#include <MatrixLeds.h>
#include <DeviceConfigs.h> 

#include <android-base/logging.h>
#include <android-base/file.h>
#include <android-base/properties.h>

#include "SysfsDefs.h"

namespace aidl::vendor::nukisystems::nanoglyph {

using aidl::vendor::nukisystems::nanoglyph::MatrixPattern;

using ::vendor::nukisystems::nanoglyph::impl::LedStripsDevice;
using ::vendor::nukisystems::nanoglyph::impl::DeviceConfig;

namespace {

constexpr int32_t kErrNotAvailable    = 1; // MatrixLedsErrorCode.NOT_AVAILABLE
constexpr int32_t kErrInvalidArgument = 2; // MatrixLedsErrorCode.INVALID_ARGUMENT
constexpr int32_t kErrNoPatternLoaded = 3; // MatrixLedsErrorCode.NO_PATTERN_LOADED
constexpr int32_t kErrBusy            = 4; // MatrixLedsErrorCode.BUSY
constexpr int32_t kErrIoError         = 5; // MatrixLedsErrorCode.IO_ERROR

constexpr int kDeviceMaxScale = 4095; 
uint16_t scaleTo12Bit(uint8_t value8) {
    return static_cast<uint16_t>((static_cast<int>(value8) * kDeviceMaxScale + 127) / 255);
}

DeviceConfig getConfig() {
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
    mMonitorRunning = false;
    mDevice.interruptWait();
    if (mMonitorThread.joinable()) {
        mMonitorThread.join();
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
    if (pattern.pixelsPerFrame != static_cast<int32_t>(cfg.pixelCount)) {
        LOG(ERROR) << cfg.name << ": loadPattern: pixelsPerFrame "
                   << pattern.pixelsPerFrame << " != expected " << cfg.pixelCount;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }
    if (pattern.frameCount <= 0 || pattern.frameCount > mDevice.slotCount()) {
        LOG(ERROR) << cfg.name << ": loadPattern: frameCount " << pattern.frameCount
                   << " out of range (max " << mDevice.slotCount() << ")";
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }

    // frameData is always 1 byte/pixel (0-255) per the AIDL contract,
    // regardless of this device's native width.
    const size_t expectedBytes =
            static_cast<size_t>(pattern.frameCount) * pattern.pixelsPerFrame;
    if (pattern.frameData.size() != expectedBytes) {
        LOG(ERROR) << cfg.name << ": loadPattern: frameData size "
                   << pattern.frameData.size() << " != expected " << expectedBytes;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }

    mDevice.resetSlots();

    const uint8_t brightness8 =
            static_cast<uint8_t>(std::clamp(pattern.brightness, 0, 255));

    mLoadedFrames.clear();
    mLoadedFrames.reserve(pattern.frameCount);
    for (int f = 0; f < pattern.frameCount; ++f) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(pattern.frameData.data()) +
                            static_cast<size_t>(f) * pattern.pixelsPerFrame;
        std::vector<uint16_t> scaledFrame(pattern.pixelsPerFrame);
        for (int p = 0; p < pattern.pixelsPerFrame; ++p) {
            scaledFrame[p] = scaleTo12Bit(src[p]);
        }
        if (!mDevice.writeSlot(f, reinterpret_cast<const uint8_t*>(scaledFrame.data()),
                                scaledFrame.size(), brightness8)) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
        }
        mLoadedFrames.push_back(std::move(scaledFrame)); 
    }
    mLoadedBrightness8 = brightness8;

    mPatternLoaded = true;
    mLoadedFrameCount = pattern.frameCount;
    mLoadedPixelsPerFrame = pattern.pixelsPerFrame;
    LOG(INFO) << cfg.name << ": loadPattern: loaded " << pattern.frameCount
              << " frames of " << pattern.pixelsPerFrame << " pixels";
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::startStream() {
    std::lock_guard<std::mutex> lock(mMutex);

    if (!mDeviceAvailable) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
    }
    if (!mPatternLoaded) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNoPatternLoaded);
    }
    if (mState == StreamState::STREAMING) {
        return ::ndk::ScopedAStatus::ok();  // idempotent
    }

    if (!mDevice.startStream(mDevice.config().pixelCount)) {
        setStateLocked(StreamState::ERROR);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
    }

    setStateLocked(StreamState::STREAMING);

    if (!mMonitorRunning.exchange(true)) {
        if (mMonitorThread.joinable()) mMonitorThread.join();
        mMonitorThread = std::thread(&MatrixLeds::playbackMonitorLoop, this);
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus MatrixLeds::stopStream() {
    std::lock_guard<std::mutex> lock(mMutex);

    if (!mDeviceAvailable) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
    }
    if (mState != StreamState::STREAMING) {
        return ::ndk::ScopedAStatus::ok();  // idempotent
    }

    bool ok = mDevice.stopStream();
    setStateLocked(StreamState::STOPPED);

    if (!ok) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
    }
    return ::ndk::ScopedAStatus::ok();
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
            mMonitorRunning = false;
            mDevice.interruptWait();
        }
    } 

    if (mMonitorThread.joinable()) {
        mMonitorThread.join();  
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
            mMonitorRunning = false;
            mDevice.interruptWait();
        }
    } 

    if (mMonitorThread.joinable()) {
        mMonitorThread.join();  
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
            mMonitorRunning = false;
            mDevice.interruptWait();
        }
    }

    if (mMonitorThread.joinable()) {
        mMonitorThread.join();
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

void MatrixLeds::playbackMonitorLoop() {
    char eventCode = 0;
    while (mMonitorRunning) {
        if (!mDevice.waitFrameEvent(&eventCode)) {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mMonitorRunning) {
                mState = StreamState::ERROR;
                if (mCallback) mCallback->onDeviceError(mDevice.lastErrno());
            }
            mMonitorRunning = false;
            break;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        if (mState != StreamState::STREAMING || mLoadedFrames.empty()) {
            mMonitorRunning = false;
            break;
        }

        mDevice.resetSlots();
        for (size_t i = 0; i < mLoadedFrames.size(); ++i) {
            mDevice.writeSlot(static_cast<int>(i),
                               reinterpret_cast<const uint8_t*>(mLoadedFrames[i].data()),
                               mLoadedFrames[i].size(), mLoadedBrightness8);
        }
        mDevice.startStream(mDevice.config().pixelCount);
    }
}

}  // namespace aidl::vendor::nukisystems::nanoglyph
