// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <aidl/vendor/nukisystems/nanoglyph/BnRedLed.h>

#include <DeviceConfig.h>

using ::vendor::nukisystems::nanoglyph::impl::RedLedConfig;

namespace aidl::vendor::nukisystems::nanoglyph {


class RedLed : public BnRedLed {
public:

    bool init();

    ::ndk::ScopedAStatus isSupported(bool* _aidl_return) override;
    ::ndk::ScopedAStatus getBrightness(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus setBrightness(int32_t brightness) override;

private:
    RedLedConfig mConfig;

};

}  // namespace aidl::vendor::nukisystems::nanoglyph
