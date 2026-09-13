// IMatrixLedsCallback.aidl
package vendor.nukisystems.nanoglyph;

import vendor.nukisystems.nanoglyph.StreamState;

@VintfStability
oneway interface IMatrixLedsCallback {
    /** Called whenever playback state changes. */
    void onStreamStateChanged(in StreamState newState);

    /** Called if the underlying device reports an I/O failure mid-stream. */
    void onDeviceError(int errnoValue);
}
