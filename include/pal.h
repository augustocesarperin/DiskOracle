#ifndef PAL_H
#define PAL_H

#if defined(_WIN32) || defined(__MINGW32__)
    #include <windows.h> // Necessário para tipos como HANDLE, DWORD etc. e para <nvme.h>
    #include <winioctl.h> // Para IOCTLs, se não vier com nvme.h ou windows.h de forma abrangente
    #include <nvme.h>    // Para definições NVME como NVME_HEALTH_INFO_LOG
#endif

#include <stdint.h> // For int64_t
#include <stdbool.h> // For bool type
#include "smart.h"   // For struct smart_data

#define PAL_API

#define PAL_STATUS_SUCCESS 0
#define PAL_STATUS_ERROR 1                // Generic error
#define PAL_STATUS_INVALID_PARAMETER 2    // Bad function argument
#define PAL_STATUS_IO_ERROR 3             // Error during device I/O
#define PAL_STATUS_NO_MEMORY 4            // Memory allocation failed
#define PAL_STATUS_TIMEOUT 5              // Operation timed out
#define PAL_STATUS_DEVICE_NOT_FOUND 6     // Device specified does not exist
#define PAL_STATUS_ACCESS_DENIED 7        // Insufficient permissions
#define PAL_STATUS_UNSUPPORTED 8          // Operation not supported on this device/platform
#define PAL_STATUS_DEVICE_OPEN_FAILED 9   // Failed to open/get handle to the device
#define PAL_STATUS_BUFFER_TOO_SMALL 10    // Provided buffer is too small for data
#define PAL_STATUS_NO_DRIVES_FOUND 11     // No drives were found by pal_list_drives
#define PAL_STATUS_SMART_NOT_SUPPORTED 12 // SMART functionality not supported by device
#define PAL_STATUS_SMART_DISABLED 13      // SMART is supported but disabled in firmware
#define PAL_STATUS_DEVICE_NOT_READY 14    // Device is not ready for operations
#define PAL_STATUS_DEVICE_IO_FAILED 15    // Generic IO failure
#define PAL_STATUS_WRONG_DRIVE_TYPE 16    // Added for orchestrator
#define PAL_STATUS_ERROR_DATA_UNDERFLOW 17 // Data returned from device is smaller than expected
#define PAL_STATUS_DEVICE_ERROR 18        // Specific device error not covered by others
#define PAL_STATUS_COUNT 19               // Keep this last for counting enum members

typedef int pal_status_t;

// Structure to hold information for each drive listed by pal_list_drives
typedef struct {
    char device_path[256]; // Platform-specific device path (e.g., /dev/sda, \\.\PhysicalDrive0)
    char model[256];       // Drive model number
    char serial[128];      // Drive serial number
    char vendor[128];      // Optional: Drive vendor/manufacturer (if easily available)
    char type[32];         // e.g., "HDD", "SSD", "NVMe" - populated from BasicDriveInfo
    int64_t size_bytes;    // Drive size in bytes - populated from pal_get_device_size
    bool is_ssd;           // Optional: True if known to be an SSD/NVMe from listing info
} pal_drive_info_t;

// Structure to hold basic drive identification and capability information
typedef struct {
    char model[256];
    char serial[128]; 
    char type[32];       // e.g., "HDD", "SSD", "NVMe", "Unknown"
    char bus_type[32];   // e.g., "ATA", "SCSI", "NVMe", "USB", "Unknown"
    bool is_ssd;         // True if the drive is likely an SSD or NVMe
    char firmware_rev[64]; // Drive firmware revision
    bool smart_capable;  // Indicates if SMART features are generally supported/accessible
    // Note: Drive size is not included here, use pal_get_device_size() for that.
} BasicDriveInfo;

// Platform Abstraction Layer interface

// Callback type for surface scan progress
typedef void (*pal_scan_callback)(unsigned long long current_lba, double progress_percentage, void *user_data);

/**
 * @brief Lists available physical drives on the system.
 *
 * @param drive_list Pointer to an array of pal_drive_info_t structures to be populated.
 * @param max_drives The maximum number of drives the drive_list array can hold.
 * @param drive_count Pointer to an integer that will be filled with the number of drives found.
 * @return pal_status_t PAL_STATUS_SUCCESS on success, or an error code.
 */
pal_status_t pal_list_drives(pal_drive_info_t *drive_list, int max_drives, int *drive_count);

/**
 * @brief Gets SMART (Self-Monitoring, Analysis, and Reporting Technology) data from the specified device.
 *
 * @param device_path Path to the device.
 * @param out Pointer to a smart_data struct to be populated.
 * @return pal_status_t PAL_STATUS_SUCCESS on success, or an error code.
 */
pal_status_t pal_get_smart_data(const char *device_path, struct smart_data *out);

/**
 * @brief Gets the total size of the specified block device in bytes.
 *
 * @param device_path Path to the device (e.g., "\\\\.\\PhysicalDrive0" on Windows).
 * @return int64_t The size of the device in bytes, or a negative error code (e.g., -PAL_STATUS_IO_ERROR).
 *         Caller should check for < 0 for error.
 */
int64_t pal_get_device_size(const char *device_path);

/**
 * @brief Gets basic information about a drive.
 *
 * @param device_path Path to the device.
 * @param info Pointer to a BasicDriveInfo struct to be populated.
 * @return pal_status_t PAL_STATUS_SUCCESS on success, or an error code. 
 *         Information in info struct is zeroed out or set to "Unknown" on failure or if specific info is not found.
 */
pal_status_t pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info);

/**
 * @brief Performs a surface scan on a portion of the device.
 *
 * @param handle Handle to the open device (obtained from pal_open_device, though mock might not use it).
 * @param start_lba Starting Logical Block Address for the scan.
 * @param lba_count Number of LBAs to scan.
 * @param callback Function to call for progress updates and error reporting.
 * @param user_data User-defined data to pass to the callback.
 * @return pal_status_t PAL_STATUS_SUCCESS on successful completion, or an error code.
 */
PAL_API pal_status_t pal_do_surface_scan(void *handle, unsigned long long start_lba, unsigned long long lba_count, pal_scan_callback callback, void *user_data);

PAL_API void* pal_open_device(const char *device_path);
PAL_API pal_status_t pal_close_device(void *handle);

PAL_API pal_status_t pal_initialize(void);
PAL_API void pal_cleanup(void);

PAL_API int pal_get_ssd_wear_status(const struct smart_data *sd, int *wear_percentage, int *wear_status_code);
PAL_API const char* pal_get_wear_status_string(int wear_status_code);

// Funções PAL específicas do Windows
#if defined(_WIN32)
struct nvme_smart_log; // Forward declaration já deve existir ou ser adicionada por smart.h via pal.h
// PAL_API void pal_windows_parse_nvme_health_log(const unsigned char* raw_log_page_data, struct nvme_smart_log* nvme_data_out);
#endif

#endif // PAL_H
