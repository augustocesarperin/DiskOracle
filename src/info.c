#include "info.h"
#include <stdio.h>
#include <string.h>
#include "pal.h"
#include "smart.h"

static const char* smart_status_to_string(SmartHealthStatus status) {
    switch (status) {
        case SMART_HEALTH_GOOD:      return "Good";
        case SMART_HEALTH_WARNING:   return "Warning";
        case SMART_HEALTH_FAILING:   return "Failing";
        case SMART_HEALTH_PREFAIL:   return "Pre-fail";
        case SMART_HEALTH_UNKNOWN:   return "Unknown";
        default:                     return "Undefined Status";
    }
}

void display_drive_info(const char *device_path) {
    if (device_path == NULL) {
        fprintf(stderr, "Oops! DiskOracle needs a device path to display its information. Please provide one.\n");
        return;
    }

    printf("DiskOracle: Detailed Information for Device: %s\n", device_path);
    printf("----------------------------------------------------------\n");

    BasicDriveInfo drive_info;
    memset(&drive_info, 0, sizeof(BasicDriveInfo)); 

    if (pal_get_basic_drive_info(device_path, &drive_info)) {
        printf("  Model:         %s\n", drive_info.model);
        printf("  Serial Number: %s\n", drive_info.serial);
        printf("  Type:          %s\n", drive_info.type);
        printf("  Bus Type:      %s\n", drive_info.bus_type);
    } else {
        printf("  Hmm, DiskOracle couldn\'t retrieve basic drive information (model, serial, type).\n");
        strncpy(drive_info.model, "Unknown", sizeof(drive_info.model) -1);
        strncpy(drive_info.serial, "Unknown", sizeof(drive_info.serial) -1);
        strncpy(drive_info.type, "Unknown", sizeof(drive_info.type) -1);
        strncpy(drive_info.bus_type, "Unknown", sizeof(drive_info.bus_type) -1);
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
    memset(&s_data, 0, sizeof(s_data));
    if (smart_read(device_path, &s_data) == 0) {
        SmartHealthStatus health_status = smart_get_health_summary(&s_data);
        printf("  SMART Health Status: %s\n", smart_status_to_string(health_status));
    } else {
        printf("  SMART Health Status: DiskOracle couldn\'t retrieve SMART data for this drive.\n");
    }

    printf("----------------------------------------------------------\n");
    printf("End of DiskOracle\'s detailed information for %s.\n", device_path);
}