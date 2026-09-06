// SPDX-License-Identifier: Apache-2.0
// vendor/nothing/NanoGlyph/matrix/test/matrix_test.cpp
#include <aidl/vendor/nukisystems/nanoglyph/IMatrixLeds.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/logging.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using aidl::vendor::nukisystems::nanoglyph::IMatrixLeds;
using aidl::vendor::nukisystems::nanoglyph::MatrixPattern;
using aidl::vendor::nukisystems::nanoglyph::StreamState;

namespace {

std::shared_ptr<IMatrixLeds> connect() {
    const std::string instance =
            std::string(IMatrixLeds::descriptor) + "/default";
    ndk::SpAIBinder binder(AServiceManager_waitForService(instance.c_str()));
    if (binder.get() == nullptr) {
        LOG(ERROR) << "Failed to get " << instance;
        return nullptr;
    }
    return IMatrixLeds::fromBinder(binder);
}

void printStatus(const std::shared_ptr<IMatrixLeds>& svc) {
    bool available = false;
    svc->isAvailable(&available);
    int32_t pixelCount = 0;
    svc->getPixelCount(&pixelCount);
    StreamState state;
    svc->getStreamState(&state);
    printf("available=%d pixelCount=%d state=%d\n",
           available, pixelCount, static_cast<int>(state));
}

}  // namespace

int main(int argc, char** argv) {
    android::base::InitLogging(argv, android::base::StderrLogger);

    auto svc = connect();
    if (!svc) return 1;

    printStatus(svc);

    if (argc > 1 && std::string(argv[1]) == "solid") {
        int brightness = argc > 2 ? std::stoi(argv[2]) : 128;
        auto status = svc->setSolidBrightness(brightness);
        if (!status.isOk()) {
            LOG(ERROR) << "setSolidBrightness failed: exceptionCode="
                    << status.getExceptionCode()
                    << " serviceSpecificError="
                    << status.getServiceSpecificError()
                    << " message=" << status.getMessage();
        }
        printStatus(svc);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        svc->stopStream();
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "pattern") {
        int32_t pixelCount = 0;
        svc->getPixelCount(&pixelCount);

        MatrixPattern pattern;
        pattern.pixelsPerFrame = pixelCount;
        pattern.frameCount = 2;
        pattern.brightness = 255;
        pattern.fps = 10;
        pattern.frameData.resize(pixelCount * pattern.frameCount);
        // frame 0: all off, frame 1: all on -- crude blink test
        std::fill(pattern.frameData.begin(),
                  pattern.frameData.begin() + pixelCount, 0);
        std::fill(pattern.frameData.begin() + pixelCount,
                  pattern.frameData.end(), 255);

        auto status = svc->loadPattern(pattern);
        if (!status.isOk()) {
            LOG(ERROR) << "loadPattern failed: "
                       << status.getServiceSpecificError();
            return 1;
        }
        status = svc->startStream();
        if (!status.isOk()) {
            LOG(ERROR) << "startStream failed: "
                       << status.getServiceSpecificError();
            return 1;
        }
        printStatus(svc);
        std::this_thread::sleep_for(std::chrono::seconds(5));
        svc->stopStream();
        return 0;
    }

    printf("usage: %s [solid <brightness> | pattern]\n", argv[0]);
    return 0;
}
