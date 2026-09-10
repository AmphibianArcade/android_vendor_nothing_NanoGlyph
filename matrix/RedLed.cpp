// SPDX-License-Identifier: Apache-2.0
#include <fcntl.h>
#include <unistd.h>

#include <RedLed.h>
#include <SysfsDefs.h>
#include <DeviceConfigs.h>
#include <ErrorConstants.h>

#include <android-base/logging.h>
#include <android-base/file.h>
#include <android-base/properties.h>

using ::vendor::nukisystems::nanoglyph::impl::RedLedConfig;

namespace aidl::vendor::nukisystems::nanoglyph {

namespace {
    RedLedConfig getRedLedConfig() {
        std::string model = ::android::base::GetProperty("ro.product.model", "");
        if (model.empty()) {
            LOG(FATAL) << "Unable to determine device model. Cannot continue!";
        } else if ((model != MODEL_FROGGERPRO) && (model != MODEL_METROID)) {
            LOG(FATAL) << "Unsupported model: '" << model << "'";
        }
        LOG(INFO) << "Selecting Red LED config for device model: '" << model << "'";
        return makeRedLedConfig(model);
    }

}  // namespace


bool RedLed::init() {
    const auto& mConfig = getRedLedConfig();

    if (::access(mConfig.brightnessPath, R_OK) != 0) {
        LOG(ERROR) << "RedLed::init: brightness read access failed";
    }

    if (::access(mConfig.brightnessPath, W_OK) != 0) {
        LOG(ERROR) << "RedLed::init: brightness write access failed";
    }

    return true;
}

::ndk::ScopedAStatus RedLed::isSupported(bool* _aidl_return) {
    *_aidl_return = mConfig.isSupported;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RedLed::setBrightness(int32_t brightness) {

    if (brightness < 0 || brightness > 255) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(kErrInvalidArgument);
    }

    std::string str = std::to_string(brightness);
    if (!::android::base::WriteStringToFile(str, mConfig.brightnessPath)) {
        LOG(ERROR) << "Unable to write to " << mConfig.brightnessPath;
        return ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RedLed::getBrightness(int32_t* _aidl_return) {
    std::string value;

    if (!android::base::ReadFileToString(mConfig.brightnessPath, &value)) {
        LOG(ERROR) << "Unable to read from " << mConfig.brightnessPath;
        return ndk::ScopedAStatus::fromServiceSpecificError(kErrIoError);
    }

    int32_t brightness = std::stoi(value);
    *_aidl_return = brightness;

    return ::ndk::ScopedAStatus::ok();
}

}  // namespace aidl::vendor::nukisystems::nanoglyph
