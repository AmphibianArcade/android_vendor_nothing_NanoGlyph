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

@VintfStability
interface IMatrixLeds {
    /**
     * Returns true if the matrix LED chip was detected and initialized
     * successfully at HAL startup.
     */
    boolean isAvailable();

    /**
     * Returns everything a client needs to initialize itself against this
     * specific panel instance -- pixel count, wire format, and pattern
     * limits. Call this once after connecting, before building any
     * MatrixPattern for loadPattern().
     */
    DeviceInfo getDeviceInfo();

    /**
     * Returns the number of addressable pixels/channels on this panel.
     * Callers must size pattern frame data to this value.
     */
    int getPixelCount();

    /**
     * Loads a full animation pattern into the device's frame buffer.
     * This does not start playback; call startStream() afterward.
     *
     * Replaces any previously loaded pattern. Safe to call while a
     * previous stream is stopped; behavior while a stream is actively
     * running is implementation-defined (implementations should either
     * queue the new pattern for the next loop iteration or reject with
     * MatrixLedsException.BUSY -- callers should not rely on which).
     *
     * @throws MatrixLedsException with code INVALID_ARGUMENT if pattern
     *         frames do not match getPixelCount(), or NOT_AVAILABLE if
     *         isAvailable() is false.
     */
    void loadPattern(in MatrixPattern pattern);

    /**
     * Begins streaming the most recently loaded pattern to the panel.
     *
     * @throws MatrixLedsException with code NO_PATTERN_LOADED if
     *         loadPattern() has not been called, or NOT_AVAILABLE.
     */
    void startStream();

    /**
     * Stops any active stream and blanks the panel.
     * Safe to call when no stream is active (no-op).
     */
    void stopStream();

    /**
     * Returns current playback state.
     */
    StreamState getStreamState();

    /**
     * One-shot: sets every pixel to a single brightness value and displays
     * it immediately, independent of any loaded pattern or active stream.
     * Intended for simple status indication, not animation.
     *
     * @param brightness 0-255.
     */
    void setSolidBrightness(int brightness);

    /**
     * Sets the global current/imax configuration for the panel driver.
     * Range and meaning are device-specific; see device documentation.
     */
    void setImax(int imax);

    /**
     * Enables or disables always-on mode, which keeps the panel powered
     * across suspend when a stream or solid brightness is active.
     */
    void setAlwaysOn(boolean enabled);

    /**
     * Registers a callback to receive playback lifecycle events.
     * Passing null unregisters any existing callback.
     */
    void setCallback(in @nullable IMatrixLedsCallback callback);
}
