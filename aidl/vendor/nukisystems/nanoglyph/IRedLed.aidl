package vendor.nukisystems.nanoglyph;

import vendor.nukisystems.nanoglyph.DeviceInfo;
import vendor.nukisystems.nanoglyph.IMatrixLedsCallback;
import vendor.nukisystems.nanoglyph.MatrixPattern;
import vendor.nukisystems.nanoglyph.StreamState;

/**
 * Vendor extension interface for matrix LED control.
 *
 * This interface is intentionally separate from android.hardware.light.ILights:
 * ILights models simple per-light RGB/brightness state and has no concept of
 * animated multi-frame patterns. Callers needing basic notification/battery
 * light behavior should use ILights; callers needing matrix animation should
 * use this interface.
 *
 */

// @VintfStability
interface IRedLed {
    /**
     * Returns true if the device has an additional red led alongside Glyph
     *
     */
    boolean isSupported();

    /**
     * Sets red LED brightness 
     *
     * @param brightness 0-255.
     */
    void setBrightness(int brightness);

    /**
     * Get red LED brightness 
     *
     * @return Current brightness 0-255
     */
    int getBrightness();
}
