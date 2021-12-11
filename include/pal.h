#ifndef PAL_H
#define PAL_H

#include <stdint.h>
#include <stdbool.h>
#include "info.h" 
#include "surface.h" 

typedef void (*pal_scan_callback)(const scan_state_t* state, void* user_data);


typedef int pal_status_t;

// === Constantes de Status ===
#define PAL_STATUS_SUCCESS 0
#define PAL_STATUS_ERROR 1
#define PAL_STATUS_INVALID_PARAMETER 2
#define PAL_STATUS_IO_ERROR 3
#define PAL_STATUS_NO_MEMORY 4
#define PAL_STATUS_DEVICE_NOT_FOUND 6
#define PAL_STATUS_ACCESS_DENIED 7
#define PAL_STATUS_UNSUPPORTED 8
#define PAL_STATUS_BUFFER_TOO_SMALL 10
#define PAL_STATUS_NO_DRIVES_FOUND 11
#define PAL_STATUS_SMART_NOT_SUPPORTED 12
#define PAL_STATUS_WRONG_DRIVE_TYPE 16      
#define PAL_STATUS_ERROR_DATA_UNDERFLOW 17 
#define PAL_STATUS_DEVICE_ERROR 18        
#define PAL_STATUS_ERROR_INSUFFICIENT_BUFFER 19
#define PAL_STATUS_ERROR_CREATING_DIR 20


pal_status_t pal_initialize(void);
void pal_cleanup(void);

pal_status_t pal_list_drives(DriveInfo* drives, int max_drives, int* drive_count);
pal_status_t pal_get_basic_drive_info(const char* device_path, BasicDriveInfo* drive_info);
int64_t pal_get_device_size(const char *device_path);

// S.M.A.R.T.
struct smart_data; 
pal_status_t pal_get_smart_data(const char* device_path, struct smart_data* data);

// NVMe Error Log
typedef struct {
    uint64_t error_count;
    uint16_t sqid;
    uint16_t cmdid;
    uint16_t status_field;
    uint16_t param_error_loc;
    uint64_t lba;
    uint32_t nsid;
    uint8_t  vendor_specific_info_avail;
    uint8_t  reserved[35];
} NVMeErrorLogEntry;

pal_status_t pal_get_nvme_error_log(const char* device_path, uint8_t entry_index, NVMeErrorLogEntry* log_entry);

/**
 * @brief Retrieves the 4096-byte Identify Controller data structure from an NVMe device.
 *
 * @param device_path The platform-specific path to the device.
 * @param buffer_4k A pointer to a 4096-byte buffer to store the result.
 * @return pal_status_t PAL_STATUS_SUCCESS on success, or an error code on failure.
 */
pal_status_t pal_get_nvme_identify_data(const char* device_path, uint8_t* buffer_4k);

// Scan Superfície
pal_status_t pal_do_surface_scan(void *handle, unsigned long long start_lba, unsigned long long lba_count, pal_scan_callback callback, void *user_data);
void* pal_open_device(const char *device_path);
pal_status_t pal_close_device(void *handle);

// Util
void pal_clear_screen(void);
void pal_wait_for_keypress(void);
int pal_get_char_input(void);
bool pal_get_string_input(char* buffer, size_t buffer_size);
pal_status_t pal_get_terminal_size(int* width, int* height);

// Funções de manipulação de sistema de arquivos
pal_status_t pal_create_directory(const char *path);
pal_status_t pal_get_current_directory(char* buffer, size_t size);

const char* pal_get_error_string(pal_status_t status);

/**
 * @brief Enumeration for different storage bus types.
 *
 * Provides a platform-independent way to identify the underlying bus
 * technology of a storage device.
 */
typedef enum {
    PAL_BUS_TYPE_UNKNOWN,
    PAL_BUS_TYPE_SCSI,
    PAL_BUS_TYPE_ATA,
    PAL_BUS_TYPE_NVME,
    PAL_BUS_TYPE_SD,
    PAL_BUS_TYPE_USB,
    PAL_BUS_TYPE_SATA
} PAL_BUS_TYPE;

/**
 * @brief Gets the bus type of the specified storage device.
 *
 * @param device_path The platform-specific path to the device.
 * @return The PAL_BUS_TYPE enumeration value for the device.
 */
PAL_BUS_TYPE pal_get_device_bus_type(const char* device_path);

/**
 * @brief Ensures that a given directory path exists, creating it if necessary.
 *
 * This function will create the specified directory and any necessary parent
 * directories.
 *
 * @param path The directory path to ensure existence of.
 * @return pal_status_t PAL_STATUS_SUCCESS on success or if the directory already exists.
 *         Returns an error code on failure.
 */
pal_status_t pal_ensure_directory_exists(const char* path);

/**
 * @brief Checks if the current process is running with elevated (Administrator/root) privileges.
 * 
 * @return true if the process has elevated privileges, false otherwise.
 */
bool pal_is_running_as_admin(void);

#ifdef __cplusplus
}
#endif

#endif // PAL_H
