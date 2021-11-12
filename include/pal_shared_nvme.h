#ifndef PAL_SHARED_NVME_H
#define PAL_SHARED_NVME_H

#include "pal.h" // For pal_status_t and common PAL definitions
// #include "pal_types.h" // No longer needed for nvme_smart_health_information_log_t

#if defined(_WIN32) || defined(__MINGW32__)
    // Ensure windows.h is included for basic Windows types (UCHAR, ULONG etc.)
    // before nvme.h, as MinGW's nvme.h depends on them.
    #include <windows.h>
    #include <nvme.h>    // For NVME_HEALTH_INFO_LOG
#else
    // For non-Windows/MinGW, if NVME_HEALTH_INFO_LOG is used, a placeholder or platform-specific header would be needed.
    // This might lead to compilation errors on other platforms if not handled.
    // Consider a more robust cross-platform type definition or abstraction if pal_shared_nvme.h is to be truly cross-platform.
    typedef void NVME_HEALTH_INFO_LOG; // Basic placeholder to avoid compile error on other platforms
#endif

#include <stdint.h>   // For uint8_t, uint32_t
#include <stdbool.h>  // For bool

/**
 * @brief Parses a raw buffer containing NVMe SMART/Health Information Log data
 *        into a structured nvme_smart_health_information_log_t format.
 *
 * This function currently performs a direct memory copy. In the future, it might
 * include byte-swapping logic if endianness differences between the host
 * and the device's report format need to be handled for specific fields.
 *
 * @param raw_buffer Pointer to the buffer containing the raw SMART log page data.
 * @param buffer_size The size of the raw_buffer in bytes.
 * @param out_log Pointer to an NVME_HEALTH_INFO_LOG structure
 *                where the parsed data will be stored.
 * @return bool true if parsing was successful (i.e., buffer was large enough),
 *         false otherwise.
 */
bool nvme_smart_log_parse(
    const uint8_t *raw_buffer,
    uint32_t buffer_size,
    NVME_HEALTH_INFO_LOG *out_log
);

#endif // PAL_SHARED_NVME_H 