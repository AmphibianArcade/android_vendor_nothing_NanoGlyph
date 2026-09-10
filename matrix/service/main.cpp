// SPDX-License-Identifier: Apache-2.0
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/logging.h>

#include <MatrixLeds.h>
#include <RedLed.h>

using aidl::vendor::nukisystems::nanoglyph::MatrixLeds;
using aidl::vendor::nukisystems::nanoglyph::RedLed;

int main() {
    android::base::InitLogging(nullptr, android::base::KernelLogger);
    LOG(INFO) << "NanoGlyph service starting";

    ABinderProcess_setThreadPoolMaxThreadCount(4);

    auto matrixService = ndk::SharedRefBase::make<MatrixLeds>();
    auto redLedService = ndk::SharedRefBase::make<RedLed>();

    if (!matrixService->init()) {
        LOG(ERROR) << "MatrixLeds::init() failed";
    }

    if (!redLedService->init()) {
        LOG(ERROR) << "RedLed::init() failed";
    }

    const std::string matrixInstance =
            std::string() + MatrixLeds::descriptor + "/default";
    const std::string redLedInstance =
            std::string() + RedLed::descriptor + "/default";

    binder_status_t matrixStatus = AServiceManager_addService(matrixService->asBinder().get(),
                                matrixInstance.c_str());
    binder_status_t redLedStatus = AServiceManager_addService(redLedService->asBinder().get(),
                                redLedInstance.c_str());

    CHECK_EQ(matrixStatus, STATUS_OK) << "Failed to register " << matrixInstance;
    CHECK_EQ(redLedStatus, STATUS_OK) << "Failed to register " << redLedInstance;

    LOG(INFO) << "NanoGlyph service running";

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // joinThreadPool should never return
}
