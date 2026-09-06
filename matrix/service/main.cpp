// SPDX-License-Identifier: Apache-2.0
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/logging.h>

#include <MatrixLeds.h>

using aidl::vendor::nukisystems::nanoglyph::MatrixLeds;

int main() {
    android::base::InitLogging(nullptr, android::base::KernelLogger);
    LOG(INFO) << "NanoGlyph service starting";

    ABinderProcess_setThreadPoolMaxThreadCount(4);

    auto service = ndk::SharedRefBase::make<MatrixLeds>();

    if (!service->init()) {
        LOG(ERROR) << "MatrixLeds::init() failed";
    }

    const std::string instance =
            std::string() + MatrixLeds::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(
            service->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK) << "Failed to register " << instance;

    LOG(INFO) << "NanoGlyph service ready";

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // joinThreadPool should never return
}
