// SPDX-License-Identifier: Apache-2.0
#include <LedStripsDevice.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include <SysfsDefs.h>

#include <android-base/logging.h>

namespace vendor::nukisystems::nanoglyph::impl {

namespace {
    uint8_t* slotBase(void* ring, int index, size_t slotSize) {
        return static_cast<uint8_t*>(ring) + (static_cast<size_t>(index) * slotSize);
    }
}
LedStripsDevice::~LedStripsDevice() {
    close();
}

bool setOperatingMode(bool state) {
    int fd = ::open(kOperatingModePath.c_str(), O_WRONLY);
    if (fd < 0) {
        LOG(ERROR) << "Failed to open " << kOperatingModePath << ": " << strerror(errno);
        return false;
    }
    const char value = state ? '1' : '0'; 
    ssize_t n = ::write(fd, &value, 1);  
    ::close(fd);
    if (n != 1) {
        LOG(ERROR) << "Failed to write operating_mode: " << strerror(errno);
        return false;
    }
    return true;
}

bool LedStripsDevice::open() {
    if (isOpen()) {
        LOG(WARNING) << mConfig.name << ": open() called while already open";
        return true;
    }

    mFd = ::open(mConfig.devicePath, O_RDWR);
    if (mFd < 0) {
        mLastErrno = errno;
        LOG(ERROR) << mConfig.name << ": failed to open " << mConfig.devicePath
                   << ": " << strerror(errno);
        return false;
    }

    void* mapped = ::mmap(nullptr, mConfig.ringTotalSize, PROT_READ | PROT_WRITE,
                           MAP_SHARED, mFd, 0);
    if (mapped == MAP_FAILED) {
        mLastErrno = errno;
        LOG(ERROR) << mConfig.name << ": mmap failed: " << strerror(errno);
        ::close(mFd);
        mFd = -1;
        return false;
    }

    mRing = mapped;

    for (int i = 0; i < mConfig.numSlots; ++i) {
        linkAndInvalidateSlot(i);
    }

    LOG(INFO) << mConfig.name << ": opened fd=" << mFd
              << " (" << mConfig.pixelCount << " px, "
              << mConfig.bytesPerPixel << " bytes/px), ring mapped at " << mRing;

    if (mConfig.deviceType == DeviceType::AW20144) {
        return setOperatingMode(true);
    }
        
    return true;
}

void LedStripsDevice::close() {
    if (mRing != nullptr) {
        ::munmap(mRing, mConfig.ringTotalSize);
        mRing = nullptr;
    }
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }

    if (mConfig.deviceType == DeviceType::AW20144) {
        setOperatingMode(false);
    }
}

bool LedStripsDevice::writeSlot(
        int slotIndex,
        const uint8_t* frameData,
        size_t pixelCount,
        uint8_t brightness) {

    if (!isOpen()) {
        LOG(ERROR) << "Device is not open";
        return false;
    }

    if (slotIndex < 0 || slotIndex >= mConfig.numSlots) {
        LOG(ERROR) << "Invalid slot index: " << slotIndex;
        return false;
    }

    if (pixelCount > mConfig.pixelCount) {
        LOG(ERROR) << "Invalid pixel count: " << pixelCount
                   << ", expected " << mConfig.pixelCount;
        return false;
    }

    switch (mConfig.deviceType) {
        case DeviceType::AW20144:
            return writeSlotAw20144(
                    slotIndex,
                    frameData,
                    pixelCount,
                    brightness);

        case DeviceType::SPI_MATRIX:
            return writeSlotSpiMatrix(
                    slotIndex,
                    frameData,
                    pixelCount,
                    brightness);
    }

    LOG(ERROR) << "Unknown device type";
    return false;
}

bool LedStripsDevice::writeSlotAw20144(int slotIndex,
                                       const uint8_t* frameData,
                                       size_t pixelCount,
                                       uint8_t brightness) {
    if (pixelCount > mConfig.mmapDataElements) {
        LOG(ERROR) << "Too many pixels: " << pixelCount;
        return false;
    }

    auto* slot = reinterpret_cast<LedMmapBufAwinic*>(slotBase(mRing, slotIndex, mConfig.slotSize));

    const size_t dataBytes = pixelCount * sizeof(uint16_t);

    std::memcpy(slot->data, frameData, dataBytes);

    slot->length = static_cast<uint16_t>(pixelCount);
    slot->brightness = brightness;

    slot->status = static_cast<uint8_t>(MmapBufStatus::VALID);

    return true;
}

bool LedStripsDevice::writeSlotSpiMatrix(int slotIndex,
                                         const uint8_t* frameData,
                                         size_t pixelCount,
                                         uint8_t brightness) {
    if (pixelCount > mConfig.mmapDataElements) {
        LOG(ERROR) << "Too many pixels: " << pixelCount;
        return false;
    }

    auto* slot = reinterpret_cast<LedMmapBufSPI*>(slotBase(mRing, slotIndex, mConfig.slotSize));

    const size_t dataBytes = pixelCount * sizeof(uint16_t);

    std::memcpy(slot->data, frameData, dataBytes);

    slot->length = static_cast<uint16_t>(pixelCount);
    slot->brightness = static_cast<uint16_t>(brightness);

    slot->status = static_cast<uint8_t>(MmapBufStatus::VALID);

    return true;
}

void LedStripsDevice::linkAndInvalidateSlot(int index) {
    uint8_t* base = static_cast<uint8_t*>(mRing) +
                    (static_cast<size_t>(index) * mConfig.slotSize);
    uint8_t* nextBase = static_cast<uint8_t*>(mRing) +
                    (static_cast<size_t>((index + 1) % mConfig.numSlots) * mConfig.slotSize);

    switch (mConfig.deviceType) {
        case DeviceType::AW20144: {
            auto* slot = reinterpret_cast<LedMmapBufAwinic*>(base);
            slot->user_next = reinterpret_cast<LedMmapBufAwinic*>(nextBase);
            slot->status = static_cast<uint8_t>(MmapBufStatus::INVALID);
            return;
        }
        case DeviceType::SPI_MATRIX: {
            auto* slot = reinterpret_cast<LedMmapBufSPI*>(base);
            slot->user_next = reinterpret_cast<LedMmapBufSPI*>(nextBase);
            slot->status = static_cast<uint8_t>(MmapBufStatus::INVALID);
            return;
        }
    }
    LOG(ERROR) << "Unknown device type";
}


/* bool LedStripsDevice::writeSlot(int slotIndex, const uint8_t* frameData,
                                 size_t pixelCount, uint8_t brightness) {
    if (!isOpen()) {
        LOG(ERROR) << mConfig.name << ": writeSlot called before open()";
        return false;
    }
    if (slotIndex < 0 || slotIndex >= mConfig.numSlots) {
        LOG(ERROR) << mConfig.name << ": writeSlot: slot index out of range: "
                   << slotIndex;
        return false;
    }
    // frameData is expected packed as pixelCount little-endian uint16_t
    // values (2 bytes/pixel) -- the aw20144 ring stores 12-bit-scale
    // brightness per channel as uint16_t, not 1 byte/pixel. This was
    // another latent mismatch versus the earlier generic DeviceConfig
    // (which assumed 1 byte/pixel for this device).
    const size_t pixelBytes = pixelCount * sizeof(uint16_t);
    if (pixelCount > static_cast<size_t>(kMaxDataLen)) {
        LOG(ERROR) << mConfig.name << ": writeSlot: pixelCount " << pixelCount
                   << " exceeds slot capacity " << kMaxDataLen;
        return false;
    }

    LedMmapBuf* slot = slotAt(mRing, slotIndex);

    std::memcpy(slot->data, frameData, pixelBytes);
    slot->length = static_cast<uint16_t>(pixelCount);
    slot->brightness = brightness;

    slot->status = static_cast<uint8_t>(MmapBufStatus::VALID);

    return true;
}
 */

void LedStripsDevice::resetSlots() {
    if (!isOpen()) return;
    for (int i = 0; i < mConfig.numSlots; ++i) {
        linkAndInvalidateSlot(i);
    }
}

bool LedStripsDevice::startStream(int pixelCount) {
    if (!isOpen()) {
        LOG(ERROR) << mConfig.name << ": startStream called before open()";
        return false;
    }

    uint8_t val = static_cast<uint8_t>(pixelCount);
    if (::ioctl(mFd, LED_STRIPS_STREAM_MODE, &val) != 0) {
        mLastErrno = errno;
        LOG(ERROR) << mConfig.name << ": LED_STRIPS_STREAM_MODE ioctl failed: "
                   << strerror(errno);
        return false;
    }
    return true;
}

bool LedStripsDevice::stopStream() {
    if (!isOpen()) {
        LOG(ERROR) << mConfig.name << ": stopStream called before open()";
        return false;
    }
    if (::ioctl(mFd, mConfig.ioctlStopMode, nullptr) != 0) {
        mLastErrno = errno;
        LOG(ERROR) << mConfig.name << ": LED_STRIPS_STOP_MODE ioctl failed: "
                   << strerror(errno);
        return false;
    }
    return true;
}

bool LedStripsDevice::setAlwaysOn(bool on) {
    if (!isOpen()) {
        LOG(ERROR) << mConfig.name << ": setAlwaysOn called before open()";
        return false;
    }
    uint8_t val = on ? 1 : 0;
    if (::ioctl(mFd, mConfig.ioctlAlwaysOn, &val) != 0) {
        mLastErrno = errno;
        LOG(ERROR) << mConfig.name << ": LED_STRIPS_ALWAYS_ON ioctl failed: "
                   << strerror(errno);
        return false;
    }
    return true;
}

bool LedStripsDevice::waitFrameEvent(char* outEventCode) {
    if (!isOpen()) return false;
    mWaitInterrupted = false;

    char buf = 0;
    ssize_t n = ::read(mFd, &buf, 1);
    if (n < 0) {
        mLastErrno = errno;
        if (errno == EBADF && mWaitInterrupted) {
            return false;
        }
        LOG(ERROR) << mConfig.name << ": read() failed: " << strerror(errno);
        return false;
    }
    if (n != 1) {
        LOG(WARNING) << mConfig.name << ": unexpected read() size: " << n;
        return false;
    }
    if (outEventCode) *outEventCode = buf;
    return true;
}

void LedStripsDevice::interruptWait() {
    mWaitInterrupted = true;
    close();
}

}  // namespace vendor::nukisystems::nanoglyph::impl
