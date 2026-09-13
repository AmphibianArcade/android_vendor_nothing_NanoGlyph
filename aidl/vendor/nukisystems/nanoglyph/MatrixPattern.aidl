// MatrixPattern.aidl
package vendor.nukisystems.nanoglyph;

@VintfStability
parcelable MatrixPattern {
    /** Number of pixels per frame. Must equal IMatrixLeds.getPixelCount(). */
    int pixelsPerFrame;

    /** Number of frames in this pattern. */
    int frameCount;

    /**
     * Flattened frame data, frameCount * pixelsPerFrame entries,
     * row-major (frame 0 pixels, then frame 1 pixels, ...).
     * Each value is 0-255 brightness per pixel for this revision;
     * see field `bitsPerPixel` if wider formats are added later.
     */
    byte[] frameData;

    /** Global brightness scalar applied to every pixel, 0-255. */
    int brightness;

    /**
     * Target playback rate in frames per second. Implementations may
     * clamp to hardware limits; actual rate is reported via
     * StreamState if it differs from the request.
     */
    int fps = 60;
}
