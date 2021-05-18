#ifndef PAL_H
#define PAL_H

#include <stdint.h> // For int64_t
#include <stdbool.h> // For bool type
#include "smart.h"   // For struct smart_data

// Platform Abstraction Layer interface

// Structure to hold basic drive information
typedef struct {
    char model[256];
    char serial[128]; // Adjusted size, serials are typically shorter than models
    char type[32];    // e.g., "HDD", "SSD", "NVMe", "Unknown"
    char bus_type[32]; // e.g., "ATA", "SCSI", "NVMe", "USB", "Unknown"
    // Consider adding firmware_rev[64]; in the future if easily obtainable
} BasicDriveInfo;

int pal_list_drives();
// int pal_get_smart(const char *device);

/**
 * @brief Gets SMART (Self-Monitoring, Analysis, and Reporting Technology) data from the specified device.
 *
 * @param device_path Path to the device.
 * @param out Pointer to a smart_data struct to be populated.
 * @return int 0 on success, non-zero on failure.
 */
int pal_get_smart(const char *device_path, struct smart_data *out);

/**
 * @brief Gets the total size of the specified block device in bytes.
 *
 * @param device_path Path to the device (e.g., "\\\\.\\PhysicalDrive0" on Windows).
 * @return int64_t The size of the device in bytes, or -1 on error.
 */
int64_t pal_get_device_size(const char *device_path);

/*oed out or set to "Unknown" on failure or if specific info is not found.
 */
bool pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info);

#endif // PAL_H
