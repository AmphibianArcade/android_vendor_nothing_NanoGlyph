// SPDX-License-Identifier: Apache-2.0
//
// Usage:
//   matrixleds_test info
//   matrixleds_test solid <brightness 0-255>
//   matrixleds_test single <index> <brightness 0-255>
//   matrixleds_test frame [value 0-4095, default 2000]
//   matrixleds_test pattern [frames, default 2] [fps, default 10]
//   matrixleds_test stop
//   matrixleds_test state
//   matrixleds_test imax <value>
//   matrixleds_test alwayson <0|1>
//   matrixleds_test callback [seconds to wait, default 10]
//   matrixleds_test all      -- runs every method once, in a safe order
#include <aidl/vendor/nukisystems/nanoglyph/BnMatrixLedsCallback.h>
#include <aidl/vendor/nukisystems/nanoglyph/IMatrixLeds.h>
#include <aidl/vendor/nukisystems/nanoglyph/IRedLed.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/logging.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using aidl::vendor::nukisystems::nanoglyph::BnMatrixLedsCallback;
using aidl::vendor::nukisystems::nanoglyph::DeviceInfo;
using aidl::vendor::nukisystems::nanoglyph::IMatrixLeds;
using aidl::vendor::nukisystems::nanoglyph::IRedLed;
using aidl::vendor::nukisystems::nanoglyph::MatrixPattern;
using aidl::vendor::nukisystems::nanoglyph::StreamState;

namespace {

const char* streamStateName(StreamState s) {
    switch (s) {
        case StreamState::STOPPED:   return "STOPPED";
        case StreamState::STREAMING: return "STREAMING";
        case StreamState::ERROR:     return "ERROR";
    }
    return "UNKNOWN";
}

// Prints exceptionCode / serviceSpecificError / message for any failed
// call -- see MatrixLedsErrorCode for what the serviceSpecificError
// values mean (1=NOT_AVAILABLE, 2=INVALID_ARGUMENT, 3=NO_PATTERN_LOADED,
// 4=BUSY, 5=IO_ERROR).
void logFailure(const char* what, const ndk::ScopedAStatus& status) {
    LOG(ERROR) << what << " failed: exceptionCode=" << status.getExceptionCode()
               << " serviceSpecificError=" << status.getServiceSpecificError()
               << " message=" << status.getMessage();
}

bool ok(const char* what, const ndk::ScopedAStatus& status) {
    if (!status.isOk()) {
        logFailure(what, status);
        return false;
    }
    printf("%s: OK\n", what);
    return true;
}

std::shared_ptr<IMatrixLeds> connect() {
    const std::string instance =
            std::string(IMatrixLeds::descriptor) + "/default";
    ndk::SpAIBinder binder(AServiceManager_waitForService(instance.c_str()));
    if (binder.get() == nullptr) {
        LOG(ERROR) << "Failed to get " << instance
                   << " -- check SELinux (binder_call/find grants), "
                   << "and that the service is actually registered "
                   << "(check VINTF manifest instance name matches).";
        return nullptr;
    }
    return IMatrixLeds::fromBinder(binder);
}

std::shared_ptr<IRedLed> connect2() {
    const std::string instance =
            std::string(IRedLed::descriptor) + "/default";
    ndk::SpAIBinder binder(AServiceManager_waitForService(instance.c_str()));
    if (binder.get() == nullptr) {
        LOG(ERROR) << "Failed to get " << instance
                   << " -- check SELinux (binder_call/find grants), "
                   << "and that the service is actually registered "
                   << "(check VINTF manifest instance name matches).";
        return nullptr;
    }
    return IRedLed::fromBinder(binder);
}


void cmdInfo(const std::shared_ptr<IMatrixLeds>& svc) {
    bool available = false;
    if (auto s = svc->isAvailable(&available); !s.isOk()) {
        logFailure("isAvailable", s);
    } else {
        printf("isAvailable: %s\n", available ? "true" : "false");
    }

    DeviceInfo info;
    if (auto s = svc->getDeviceInfo(&info); !s.isOk()) {
        logFailure("getDeviceInfo", s);
    } else {
        printf("getDeviceInfo:\n");
        printf("  name=%s\n", info.name.c_str());
        printf("  pixelCount=%d\n", info.pixelCount);
        printf("  bytesPerPixel=%d\n", info.bytesPerPixel);
        printf("  maxFramesPerPattern=%d\n", info.maxFramesPerPattern);
        printf("  nativeFps=%d\n", info.nativeFps);
        printf("  supportsAlwaysOn=%d\n", info.supportsAlwaysOn);
        printf("  supportsImax=%d\n", info.supportsImax);
    }

    int32_t pixelCount = 0;
    if (auto s = svc->getPixelCount(&pixelCount); !s.isOk()) {
        logFailure("getPixelCount", s);
    } else {
        printf("getPixelCount: %d\n", pixelCount);
    }

    StreamState state;
    if (auto s = svc->getStreamState(&state); !s.isOk()) {
        logFailure("getStreamState", s);
    } else {
        printf("getStreamState: %s\n", streamStateName(state));
    }
}

void cmdSolid(const std::shared_ptr<IMatrixLeds>& svc, int brightness) {
    ok("setSolidBrightness", svc->setSolidBrightness(brightness));
}

void cmdRed(const std::shared_ptr<IRedLed>& svc, int brightness) {
    ok("setBrightness", svc->setBrightness(brightness));
}

void cmdGetRed(const std::shared_ptr<IRedLed>& svc) {
    int32_t brightness;
    auto status = svc->getBrightness(&brightness);

    if (status.isOk()) {
       printf("Brightness: %d\n", brightness); 
    }
}

void cmdSingle(const std::shared_ptr<IMatrixLeds>& svc, int index, int brightness) {
    ok("setSingleBrightness", svc->setSingleBrightness(index, brightness));
}

void cmdFrame(const std::shared_ptr<IMatrixLeds>& svc, int value) {
    int32_t pixelCount = 0;
    if (auto s = svc->getPixelCount(&pixelCount); !s.isOk()) {
        logFailure("getPixelCount", s);
        return;
    }
    std::vector<int32_t> frame(pixelCount, value);
    ok("setFrame", svc->setFrame(frame));
}

void cmdPattern(const std::shared_ptr<IMatrixLeds>& svc, int frameCount, int fps) {
    int32_t pixelCount = 0;
    if (auto s = svc->getPixelCount(&pixelCount); !s.isOk()) {
        logFailure("getPixelCount", s);
        return;
    }

    MatrixPattern pattern;
    pattern.pixelsPerFrame = pixelCount;
    pattern.frameCount = frameCount;
    pattern.brightness = 255;
    pattern.fps = fps;
    pattern.frameData.resize(static_cast<size_t>(pixelCount) * frameCount);

    for (int f = 0; f < frameCount; ++f) {
        uint8_t value = (f % 2 == 0) ? 0 : 255;
        std::fill(pattern.frameData.begin() + static_cast<size_t>(f) * pixelCount,
                  pattern.frameData.begin() + static_cast<size_t>(f + 1) * pixelCount,
                  value);
    }

    if (!ok("loadPattern", svc->loadPattern(pattern))) return;
    ok("startStream", svc->startStream());
}

void cmdStop(const std::shared_ptr<IMatrixLeds>& svc) {
    ok("stopStream", svc->stopStream());
}

void cmdImax(const std::shared_ptr<IMatrixLeds>& svc, int value) {
    ok("setImax", svc->setImax(value));
}

void cmdAlwaysOn(const std::shared_ptr<IMatrixLeds>& svc, bool enabled) {
    ok("setAlwaysOn", svc->setAlwaysOn(enabled));
}

class TestCallback : public BnMatrixLedsCallback {
public:
    ::ndk::ScopedAStatus onStreamStateChanged(StreamState newState) override {
        printf("[callback] onStreamStateChanged: %s\n", streamStateName(newState));
        return ::ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDeviceError(int32_t errnoValue) override {
        printf("[callback] onDeviceError: errno=%d (%s)\n", errnoValue,
               strerror(errnoValue));
        return ::ndk::ScopedAStatus::ok();
    }
};

void cmdCallback(const std::shared_ptr<IMatrixLeds>& svc, int waitSeconds) {
    auto cb = ndk::SharedRefBase::make<TestCallback>();
    if (!ok("setCallback(register)", svc->setCallback(cb))) return;

    printf("Registered callback, waiting %ds for events "
           "(trigger start/stop/errors from another shell)...\n", waitSeconds);
    std::this_thread::sleep_for(std::chrono::seconds(waitSeconds));

    ok("setCallback(unregister)", svc->setCallback(nullptr));
}

void cmdAll(const std::shared_ptr<IMatrixLeds>& svc, const std::shared_ptr<IRedLed>& svc2) {
    printf("=== info ===\n");
    cmdInfo(svc);

    printf("\n=== setImax ===\n");
    cmdImax(svc, 100);

    printf("\n=== setAlwaysOn ===\n");
    cmdAlwaysOn(svc, true);
    cmdAlwaysOn(svc, false);

    printf("\n=== setSingleBrightness ===\n");
    cmdSingle(svc, 0, 128);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    printf("\n=== setSolidBrightness ===\n");
    cmdSolid(svc, 200);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    printf("\n=== (red) setBrightness ===\n");
    cmdRed(svc2, 200);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    cmdRed(svc2, 0);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    printf("\n=== (red) getBrightness ===\n");
    cmdGetRed(svc2);

    printf("\n=== setFrame ===\n");
    cmdFrame(svc, 1555);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    printf("\n=== loadPattern + startStream ===\n");
    cmdPattern(svc, 4, 5);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    printf("\n=== stopStream ===\n");
    cmdStop(svc);

    printf("\n=== final state ===\n");
    cmdInfo(svc);
}

void printUsage(const char* argv0) {
    printf(
        "usage: %s <command> [args]\n"
        "  info                          isAvailable/getDeviceInfo/getPixelCount/getStreamState\n"
        "  solid <brightness 0-255>      setSolidBrightness\n"
        "  red <brightness 0-255>        (red) setBrightness\n"
        "  getRed                        (red) getBrightness\n"
        "  single <index> <brightness>   setSingleBrightness\n"
        "  frame [value 0-255]          setFrame (default 2000)\n"
        "  pattern [frames] [fps]        loadPattern + startStream (defaults 2, 10)\n"
        "  stop                          stopStream\n"
        "  state                         getStreamState only\n"
        "  imax <value>                  setImax\n"
        "  alwayson <0|1>                setAlwaysOn\n"
        "  callback [seconds]            setCallback, wait, unregister (default 10)\n"
        "  all                           run every method once, in sequence\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    android::base::InitLogging(argv, android::base::StderrLogger);

    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    auto svc = connect();
    auto svc2 = connect2();
    if (!svc || !svc2) return 1;

    const std::string cmd = argv[1];

    if (cmd == "info") {
        cmdInfo(svc);
    } else if (cmd == "solid") {
        if (argc < 3) { printUsage(argv[0]); return 1; }
        cmdSolid(svc, std::atoi(argv[2]));
    } else if (cmd == "red") {
        if (argc < 3) { printUsage(argv[0]); return 1; }
        cmdRed(svc2, std::atoi(argv[2]));
    } else if (cmd == "getRed") {
        if (argc < 2) { printUsage(argv[0]); return 1; }
        cmdGetRed(svc2);
    } else if (cmd == "single") {
        if (argc < 4) { printUsage(argv[0]); return 1; }
        cmdSingle(svc, std::atoi(argv[2]), std::atoi(argv[3]));
    } else if (cmd == "frame") {
        int value = argc > 2 ? std::atoi(argv[2]) : 155;
        cmdFrame(svc, value);
    } else if (cmd == "pattern") {
        int frames = argc > 2 ? std::atoi(argv[2]) : 2;
        int fps = argc > 3 ? std::atoi(argv[3]) : 10;
        cmdPattern(svc, frames, fps);
    } else if (cmd == "stop") {
        cmdStop(svc);
    } else if (cmd == "state") {
        StreamState state;
        if (auto s = svc->getStreamState(&state); !s.isOk()) {
            logFailure("getStreamState", s);
            return 1;
        } else {
            printf("state: %s\n", streamStateName(state));
        }
    } else if (cmd == "imax") {
        if (argc < 3) { printUsage(argv[0]); return 1; }
        cmdImax(svc, std::atoi(argv[2]));
    } else if (cmd == "alwayson") {
        if (argc < 3) { printUsage(argv[0]); return 1; }
        cmdAlwaysOn(svc, std::atoi(argv[2]) != 0);
    } else if (cmd == "callback") {
        int seconds = argc > 2 ? std::atoi(argv[2]) : 10;
        cmdCallback(svc, seconds);
    } else if (cmd == "all") {
        cmdAll(svc, svc2);
    } else {
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
