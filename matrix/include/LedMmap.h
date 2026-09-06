#pragma once

#include <cstdint>
#include <sys/ioctl.h>

#define MATRIXLEDS_FROGGERPRO_PIXEL_COUNT 137
#define MATRIXLEDS_METROID_PIXEL_COUNT 489

namespace vendor::nukisystems::nanoglyph::impl {

#define LED_STRIPS_STREAM_MODE  _IOW('x', 0x50, unsigned long)
#define LED_STRIPS_STOP_MODE    _IOW('x', 0x51, unsigned long)
#define LED_STRIPS_ALWAYS_ON    _IOW('x', 0x52, unsigned long)
#define LED_STRIPS_FREQ_SET     _IOW('x', 0x53, unsigned long)

struct LedMmapBufCommon {
    uint8_t status;         // MMAP_BUF_DATA_{VALID,FINISHED,INVALID} magic 
    uint8_t bit;
    uint16_t length;
};

#pragma pack(push, 4)
struct LedMmapBufSPI : public LedMmapBufCommon {
    LedMmapBufSPI* kernel_next;
    LedMmapBufSPI* user_next;

    uint16_t brightness;
    uint16_t data[2 * MATRIXLEDS_METROID_PIXEL_COUNT + 34];
};
#pragma pack(pop)

#pragma pack(push, 4)
struct LedMmapBufAwinic : public LedMmapBufCommon {
    LedMmapBufAwinic* kernel_next;
    LedMmapBufAwinic* user_next;

    uint8_t brightness;
    uint8_t _padding[1];

    uint16_t data[500];
};
#pragma pack(pop)

#if MATRIXLEDS_FROGGERPRO_PIXEL_COUNT == MATRIX_LEDS_COUNT
using LedMmapBuf = LedMmapBufAwinic;
#endif

#if MATRIXLEDS_METROID_PIXEL_COUNT == MATRIX_LEDS_COUNT
using LedMmapBuf = LedMmapBufSPI;
#endif

static_assert(sizeof(LedMmapBufAwinic) == 1024);
static_assert(sizeof(LedMmapBufSPI) == 2048);

enum class MmapBufStatus : uint8_t {
    VALID    = 0x55,
    FINISHED = 0xAA,
    INVALID  = 0xFF,
};

constexpr int kNumSlots = 8;      // LED_MMAP_BUF_SUM

}
