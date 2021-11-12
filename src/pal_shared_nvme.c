#include "pal_shared_nvme.h"

#if defined(_WIN32) || defined(__MINGW32__)
    // Ensure windows.h is included for basic Windows types (UCHAR, ULONG etc.)
    // before nvme.h, as MinGW's nvme.h depends on them.
    // pal_shared_nvme.h (which this .c file includes) should also bring these in,
    // but being explicit here for the .c file doesn't hurt and ensures clarity.
    #include <windows.h>
    #include <nvme.h>    // For NVME_HEALTH_INFO_LOG definition
#else
    // Placeholder for NVME_HEALTH_INFO_LOG if it wasn't defined via pal_shared_nvme.h for other platforms
    // This typedef should match the one in pal_shared_nvme.h's #else branch.
    typedef void NVME_HEALTH_INFO_LOG;
#endif

#include <string.h> // For memcpy and potentially memset
#include <stdio.h>  // For debugging prints if any (none currently)

// Definition 1 (Keep this one)
bool nvme_smart_log_parse(
    const uint8_t *raw_buffer,
    uint32_t buffer_size,
    NVME_HEALTH_INFO_LOG *out_log
) {
    if (!raw_buffer || !out_log) {
        return false; // Null pointers
    }
    if (buffer_size < sizeof(NVME_HEALTH_INFO_LOG)) {
        // Buffer too small to contain the full log page
        return false; 
    }
    memcpy(out_log, raw_buffer, sizeof(NVME_HEALTH_INFO_LOG));
    return true;
}
