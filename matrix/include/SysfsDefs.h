// SPDX-License-Identifier: Apache-2.0
#pragma once

const std::string kMatrixSysfsPath = "/sys/class/leds/matrix-leds/";

const std::string kOperatingModePath = kMatrixSysfsPath + "operating_mode";
const std::string kFrameBrightnessPath = kMatrixSysfsPath + "frame_brightness";

const std::string kAllBrightnessPathAwinic = kMatrixSysfsPath + "all_white_brightness";
const std::string kAllBrightnessPathSPI = kMatrixSysfsPath + "all_brightness";

const std::string kSingleBrightnessPath = kMatrixSysfsPath + "single_brightness";

const std::string kRedLEDPath = "/sys/class/leds/red/";
const std::string kRedLEDBrightnessPath = kRedLEDPath + "brightness";
