// SPDX-License-Identifier: Apache-2.0
#include <MatrixLeds.h>
#include <DeviceConfigs.h> 

#include <android-base/logging.h>

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

}  // namespace

MatrixLeds::MatrixLeds() : mDevice(makeConfig()) {}

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
    if (static_cast<size_t>(pattern.pixelsPerFrame) != cfg.pixelCount) {
        LOG(ERROR) << cfg.name << ": loadPattern: pixelsPerFrame "
                   << pattern.pixelsPerFrame << " != expected " << cfg.pixelCount;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }
    if (pattern.frameCount <= 0 || pattern.frameCount > mDevice.slotCount()) {

        LOG(ERROR) << cfg.name << ": loadPattern: frameCount " << pattern.frameCount
                   << " out of range (max " << mDevice.slotCount() << ")";
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }
    const size_t expectedBytes = static_cast<size_t>(pattern.frameCount) *
                                  pattern.pixelsPerFrame * cfg.bytesPerPixel;
    if (pattern.frameData.size() != expectedBytes) {
        LOG(ERROR) << "loadPattern: frameData size " << pattern.frameData.size()
                   << " != expected " << expectedBytes;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }
    if (mState == StreamState::STREAMING) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrBusy);
    }

    mDevice.resetSlots();
    const uint8_t brightness = static_cast<uint8_t>(
            std::clamp(pattern.brightness, 0, 255));

    const size_t frameStrideBytes =
            static_cast<size_t>(pattern.pixelsPerFrame) * cfg.bytesPerPixel;

    for (int i = 0; i < pattern.frameCount; ++i) {
        const uint8_t* frameStart =
                reinterpret_cast<const uint8_t*>(pattern.frameData.data()) +
                (static_cast<size_t>(i) * frameStrideBytes);
        if (!mDevice.writeSlot(i, frameStart, pattern.pixelsPerFrame, brightness)) {
            LOG(ERROR) << cfg.name << ": loadPattern: writeSlot failed at frame " << i;
            return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
        }
    }

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
    std::lock_guard<std::mutex> lock(mMutex);

    if (!mDeviceAvailable) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrNotAvailable);
    }
    if (brightness < 0 || brightness > 255) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }
    if (mState == StreamState::STREAMING) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrBusy);
    }

    const auto& cfg = mDevice.config();

    brightness = std::clamp(brightness, 0, 255);

    std::vector<uint16_t> frame(
            cfg.pixelCount,
            4095);

    mDevice.resetSlots();

    if (!mDevice.writeSlot(
            0,
            reinterpret_cast<const uint8_t*>(frame.data()),
            cfg.pixelCount,
            static_cast<uint8_t>(brightness))) {
        return ndk::ScopedAStatus::fromExceptionCode(
                EX_ILLEGAL_STATE);
    }

    if (!mDevice.startStream(
            static_cast<int>(cfg.pixelCount))) {
        return ndk::ScopedAStatus::fromExceptionCode(
                EX_ILLEGAL_STATE);
    }

    mPatternLoaded = true;
    mLoadedFrameCount = 1;
    mLoadedPixelsPerFrame = cfg.pixelCount;
    setStateLocked(StreamState::STREAMING);
    if (!mMonitorRunning.exchange(true)) {
        if (mMonitorThread.joinable()) mMonitorThread.join();
        mMonitorThread = std::thread(&MatrixLeds::playbackMonitorLoop, this);
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
                LOG(ERROR) << "playbackMonitorLoop: device error, marking ERROR";
                mState = StreamState::ERROR;
                if (mCallback) mCallback->onDeviceError(mDevice.lastErrno());
            }
            mMonitorRunning = false;
            break;
        }

        LOG(INFO) << "playbackMonitorLoop: frame event code=" << eventCode;
    }
}

}  // namespace aidl::vendor::nukisystems::nanoglyph
