#ifndef SMART_H
#define SMART_H

#include <stdint.h>
#include <stdbool.h>
#include "pal_types.h"

#if defined(_WIN32) || defined(__MINGW32__)
    // Ensure windows.h is included for basic Windows types (UCHAR, ULONG etc.)
    // before nvme.h, as MinGW's nvme.h depends on them.
    #include <windows.h>
    // Ensure nvme.h is included for NVME_HEALTH_INFO_LOG definition
    // This is needed because struct smart_nvme uses this type directly.
    #include <nvme.h>
#endif

#define MAX_SMART_ATTRIBUTES 30
// #define MAX_ATA_ATTRIBUTES 30 // MAX_SMART_ATTRIBUTES será usado para consistência

// Definindo os tipos de drive aqui para serem usados em smart_data
#define DRIVE_TYPE_UNKNOWN 0
#define DRIVE_TYPE_ATA 1
#define DRIVE_TYPE_NVME 2

// Health prediction status (consistent with previous definitions)
typedef enum {
    SMART_HEALTH_OK,         // 0
    SMART_HEALTH_WARNING,    // 1
    SMART_HEALTH_PREFAIL,    // 2
    SMART_HEALTH_FAILING,    // 3
    SMART_HEALTH_UNKNOWN     // 4
} SmartStatus; // Renamed from SmartHealthStatus for brevity and consistency

// Individual ATA SMART attribute structure (as per latest understanding)
struct smart_attr {
    uint8_t id;
    char name[32];
    uint16_t flags;
    uint8_t value;
    uint8_t worst;
    uint8_t raw[6];
    uint8_t threshold;
};

// Common SMART Attribute Flags (can be OR'd into smart_attr.flags)
#define SMART_ATTR_FLAG_PREFAIL             0x0001 // Indicates a pre-failure attribute
#define SMART_ATTR_FLAG_ONLINE_COLLECTION   0x0002 // Attribute is updated during normal operation
#define SMART_ATTR_FLAG_PERFORMANCE         0x0004 // Attribute relates to performance
#define SMART_ATTR_FLAG_ERROR_RATE          0x0008 // Attribute relates to error rates
#define SMART_ATTR_FLAG_EVENT_COUNT         0x0010 // Attribute is an event counter
#define SMART_ATTR_FLAG_SELF_PRESERVING     0x0020 // Attribute is self-preserving (value should not decrease)

// NVMe SMART specific data structure (as per latest understanding)
// The full definition of NVME_HEALTH_INFO_LOG is now expected to be available
// via pal.h including <nvme.h> before smart.h is included.

struct smart_nvme { // Renamed from smart_nvme_t
    NVME_HEALTH_INFO_LOG raw_health_log; // Changed to use SDK type
    uint8_t critical_warning;
    uint8_t temperature[2]; // Kelvin
    uint8_t avail_spare;
    uint8_t spare_thresh;
    uint8_t percent_used;
    uint8_t firmware[8]; // Added Firmware Revision
    uint8_t data_units_read[16];
    uint8_t data_units_written[16];
    uint8_t host_read_commands[16];
    uint8_t host_write_commands[16];
    uint8_t controller_busy_time[16];
    uint8_t power_cycles[16];
    uint8_t power_on_hours[16];
    uint8_t unsafe_shutdowns[16];
    uint8_t media_errors[16];
    uint8_t num_err_log_entries[16];
};

struct nvme_smart_log {
    uint8_t critical_warning;
    uint16_t temperature; // In Kelvin. Subtract 273.15 for Celsius. Field is 2 bytes, value is integer part.
    uint8_t avail_spare;
    uint8_t spare_thresh;
    uint8_t percent_used;
    uint8_t data_units_read[16];        // Number of 512-byte data units read by the host, LSB first
    uint8_t data_units_written[16];     // Number of 512-byte data units written by the host, LSB first
    uint8_t host_read_commands[16];     // Number of read commands completed by the controller, LSB first
    uint8_t host_write_commands[16];    // Number of write commands completed by the controller, LSB first
    uint8_t controller_busy_time[16];   // Controller busy time in minutes, LSB first
    uint8_t power_cycles[16];           // Number of power cycles, LSB first
    uint8_t power_on_hours[16];         // Number of power-on hours, LSB first
    uint8_t unsafe_shutdowns[16];       // Number of unsafe shutdowns, LSB first
    uint8_t media_errors[16];           // Number of media and data integrity errors, LSB first
    uint8_t num_err_log_entries[16];    // Number of error information log entries, LSB first
    uint32_t warning_composite_temp_time;    
    uint32_t critical_composite_temp_time;   
    uint16_t temp_sensor_1_trans_count;      
    uint16_t temp_sensor_2_trans_count;      
    uint32_t temp_sensor_1_total_time;       
    uint32_t temp_sensor_2_total_time;       
};

union smart_device_data {
    struct smart_attr attrs[MAX_SMART_ATTRIBUTES]; 
    struct smart_nvme nvme;
    struct nvme_smart_log nvme_log;
};

// Main SMART data structure (corrected based on recent file read)
struct smart_data {
    int is_nvme;       // Corrigido para int
    int drive_type;    // Corrigido para int (usará macros DRIVE_TYPE_...)
    int attr_count;    // Only relevant for ATA
    union smart_device_data data; 
    char device_name[256]; // Mantido da versão funcional
    // Health status pode ser adicionado aqui depois, se necessário
};

// Function prototypes

// Function to read SMART data from a device using the PAL
// Implemented in smart.c, calls pal_get_smart_data
int smart_read(const char *device_path, const char *model, const char *serial, struct smart_data *out);

// Implemented in smart.c, main interpretation logic
// The pal_windows.c and smart_ata.c changes mean this function now handles ATA interpretation directly.
int smart_interpret(const char *device, struct smart_data *data); 

// Implemented in smart_ata.c, helper to get attribute name (if still used, or logic moved to smart_interpret)
const char* ata_attribute_name(uint8_t attr_id);

// Function to get a summary of health (likely in smart.c or a new health.c)
SmartStatus smart_get_health_summary(const struct smart_data *data);


#endif // SMART_H

