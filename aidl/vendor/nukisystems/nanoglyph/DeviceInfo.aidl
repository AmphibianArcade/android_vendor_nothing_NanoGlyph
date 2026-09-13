// SPDX-License-Identifier: Apache-2.0
package vendor.nukisystems.nanoglyph;

/**
 * Describes the panel behind a given IMatrixLeds instance, so clients can
 * initialize themselves (buffer sizes, pixel packing, max pattern length)
 * without hardcoding per-device assumptions.
 *
 * Deliberately excludes anything that is purely a HAL/kernel implementation
 * detail (ioctl request codes, ring buffer byte offsets, device node path).
 * Those stay inside the HAL: clients have no legitimate use for them, and
 * exposing them would couple app code to a kernel driver ABI that can
 * change independently of this AIDL interface's own versioning.
 */
@VintfStability
parcelable DeviceInfo {
    /** Stable identifier matching this instance's registered name, e.g. "aw20144". */
    String name;

    /** Number of addressable pixels/channels. */
    int pixelCount;

    /**
     * Bytes per pixel the client must use when packing MatrixPattern.frameData.
     * 1 = plain brightness/PWM value per pixel.
     * 2 = 16-bit value per pixel, packed little-endian.
     */
    int bytesPerPixel;

    /**
     * Maximum frames loadPattern() will accept in a single call for this
     * instance (bounded by the underlying ring buffer's slot count).
     */
    int maxFramesPerPattern;

    /**
     * Effective playback rate this device's kernel driver paces frames at,
     * in frames per second. MatrixPattern.fps requests are informational
     * only if they don't match this value -- actual pacing is fixed by
     * the kernel worker, not adjustable per pattern.
     */
    int nativeFps;

    /** True if setAlwaysOn()/setImax() are meaningful for this device. */
    boolean supportsAlwaysOn;
    boolean supportsImax;
}
