// SPDX-License-Identifier: Apache-2.0
package vendor.nukisystems.nanoglyph;

/**
 * Error codes returned via service-specific exceptions
 */
// @VintfStability
@Backing(type="int")
enum MatrixLedsErrorCode {
    NOT_AVAILABLE = 1,
    INVALID_ARGUMENT = 2,
    NO_PATTERN_LOADED = 3,
    BUSY = 4,
    IO_ERROR = 5,
}
