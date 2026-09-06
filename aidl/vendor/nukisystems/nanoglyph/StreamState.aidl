// StreamState.aidl
package vendor.nukisystems.nanoglyph;

@VintfStability
@Backing(type="int")
enum StreamState {
    STOPPED = 0,
    STREAMING = 1,
    ERROR = 2,
}
