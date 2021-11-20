#include "../include/info.h"
#include "../include/pal.h"
#include "../include/smart.h"
#include "../include/logging.h" // For DEBUG_PRINT, if used
#include <stdio.h>    // For printf
#include <string.h>   // For memset, if not done in pal_get_basic_drive_info init

// Convert SmartStatus enum to a string representation
static const char* smart_status_to_string(SmartStatus status) {
    switch (status) {
        case SMART_HEALTH_OK: return "OK";
        case SMART_HEALTH_WARNING: return "Warning";
        case SMART_HEALTH_FAILING: return "Failing";
        case SMART_HEALTH_PREFAIL: return "Prefail";
        case SMART_HEALTH_UNKNOWN: return "Unknown";
        default: return "N/A";
    }
}

// Stub implementation for display_drive_info
// This function will be expanded to show detailed information about the drive.
void display_drive_info(const char *device_path) {
    if (device_path == NULL) {
        fprintf(stderr, "Oops! DiskOracle needs a device path to display its information. Please provide one.\n");
        return;
    }

    printf("DiskOracle: Detailed Information for Device: %s\n", device_path);
    printf("----------------------------------------------------------\n");

    BasicDriveInfo drive_info;
    // pal_get_basic_drive_info should initialize the struct, but defensive zeroing is fine.
    memset(&drive_info, 0, sizeof(BasicDriveInfo)); 

    if (pal_get_basic_drive_info(device_path, &drive_info)) {
        printf("  Model:         %s\n", drive_info.model);
        printf("  Serial Number: %s\n", drive_info.serial);
        printf("  Type:          %s\n", drive_info.type);
        printf("  Bus Type:      %s\n", drive_info.bus_type);
    } else {
        printf("  Hmm, DiskOracle couldn\'t retrieve basic drive information (model, serial, type).\n");
        // Still try to get size and SMART data if basic info fails
        strncpy_s(drive_info.model, sizeof(drive_info.model), "Unknown", _TRUNCATE);
        strncpy_s(drive_info.serial, sizeof(drive_info.serial), "Unknown", _TRUNCATE);
        strncpy_s(drive_info.bus_type, sizeof(drive_info.bus_type), "Unknown", _TRUNCATE);
    }

    int64_t size_bytes = pal_get_device_size(device_path);
    if (size_bytes > 0) {
        double size_gb = (double)size_bytes / (1024.0 * 1024.0 * 1024.0);
        double size_tb = size_gb / 1024.0;
        if (size_tb >= 1.0) {
            printf("  Size:          %.2f TB (%lld Bytes)\n", size_tb, (long long)size_bytes);
        } else {
            printf("  Size:          %.2f GB (%lld Bytes)\n", size_gb, (long long)size_bytes);
        }
    } else {
        printf("  Size:          DiskOracle couldn\'t retrieve the device size.\n");
    }
    
    printf("\nSMART Information:\n");
    struct smart_data s_data;
    if (smart_read(device_path, drive_info.model, drive_info.serial, &s_data) == 0) {
        printf("  SMART Data Readable: Yes\n");
        SmartStatus health_status = smart_get_health_summary(&s_data);
        printf("  SMART Health Status: %s\n", smart_status_to_string(health_status));

        // Optionally, print a few key attributes if available (example)
        if (!s_data.is_nvme && s_data.attr_count > 0) {

        }
    } else {
        printf("  SMART Data Readable: No (or error reading)\n");
        printf("  SMART Health Status: N/A\n");
    }

    printf("----------------------------------------------------------\n");
    printf("End of DiskOracle\'s detailed information for %s.\n", device_path);
}