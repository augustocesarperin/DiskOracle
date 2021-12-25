#include "report.h"
#include "smart.h"
#include "pal.h"
#include "style.h" 
#include <stdio.h>
#include <string.h>
#include <stdint.h> // For uint64_t, uint8_t etc. used in smart_interpret/raw_to_uint64
#include <inttypes.h> // For PRIu64 etc.
#include "logging.h" 
#include <stdbool.h> 


// Unified smart_read function that calls the PAL to get SMART data.
int smart_read(const char *device_path, const char* device_model, const char* device_serial, struct smart_data *out) {

    if (!device_path || !out) {
        fprintf(stderr, "Erro: device_path ou out_data nulo em smart_read.\n");
        return PAL_STATUS_INVALID_PARAMETER;
    }

    memset(out, 0, sizeof(struct smart_data));

    // A função pal_get_smart_data irá preencher out->is_nvme.
    int pal_status = pal_get_smart_data(device_path, out);

    return pal_status;
}

void smart_get_ata_attribute_name(uint8_t id, char* buffer, int buffer_len) {
    switch (id) {
        case 1:   snprintf(buffer, buffer_len, "Raw_Read_Error_Rate"); break;
        case 2:   snprintf(buffer, buffer_len, "Throughput_Performance"); break;
        case 3:   snprintf(buffer, buffer_len, "Spin_Up_Time"); break;
        case 4:   snprintf(buffer, buffer_len, "Start_Stop_Count"); break;
        case 5:   snprintf(buffer, buffer_len, "Reallocated_Sector_Ct"); break;
        case 7:   snprintf(buffer, buffer_len, "Seek_Error_Rate"); break;
        case 9:   snprintf(buffer, buffer_len, "Power_On_Hours"); break;
        case 10:  snprintf(buffer, buffer_len, "Spin_Retry_Count"); break;
        case 12:  snprintf(buffer, buffer_len, "Power_Cycle_Count"); break;
        case 184: snprintf(buffer, buffer_len, "End-to-End_Error"); break;
        case 187: snprintf(buffer, buffer_len, "Reported_Uncorrect"); break;
        case 188: snprintf(buffer, buffer_len, "Command_Timeout"); break;
        case 189: snprintf(buffer, buffer_len, "High_Fly_Writes"); break;
        case 190: snprintf(buffer, buffer_len, "Airflow_Temperature_Cel"); break;
        case 191: snprintf(buffer, buffer_len, "G-Sense_Error_Rate"); break;
        case 192: snprintf(buffer, buffer_len, "Power-Off_Retract_Count"); break;
        case 193: snprintf(buffer, buffer_len, "Load_Cycle_Count"); break;
        case 194: snprintf(buffer, buffer_len, "Temperature_Celsius"); break;
        case 197: snprintf(buffer, buffer_len, "Current_Pending_Sector"); break;
        case 198: snprintf(buffer, buffer_len, "Offline_Uncorrectable"); break;
        case 199: snprintf(buffer, buffer_len, "UDMA_CRC_Error_Count"); break;
        case 240: snprintf(buffer, buffer_len, "Head_Flying_Hours"); break;
        case 241: snprintf(buffer, buffer_len, "Total_LBAs_Written"); break;
        case 242: snprintf(buffer, buffer_len, "Total_LBAs_Read"); break;
        case 254: snprintf(buffer, buffer_len, "Vendor_Specific"); break;
        default:  snprintf(buffer, buffer_len, "Unknown_Attribute_%u", id); break;
    }
}

uint64_t raw_to_uint64(const unsigned char* raw_value) {
    uint64_t result = 0;
    for (int i = 0; i < 6; ++i) {
        result |= ((uint64_t)raw_value[i] << (i * 8));
    }
    return result;
}

uint64_t nvme_counter_to_uint64(const uint8_t counter[16]) {
    uint64_t val = 0;
    memcpy(&val, counter, sizeof(uint64_t)); // Standard interpretation is to read the first 8 bytes (64 bits)
    return val;
}
static uint16_t nvme_temp_to_uint16(const uint8_t temp[2]) {
    uint16_t val = 0;
    memcpy(&val, temp, sizeof(uint16_t));
    return val;
}

// Helper function to determine if a new attribute entry is "better"
static bool is_better_attribute_entry(const struct smart_attr *new_attr, const struct smart_attr *old_attr) {
    if (old_attr->id != new_attr->id) {
        return false; 
    }

    if (old_attr->value == 0 && new_attr->value != 0) {
        return true;
    }
    if (new_attr->value == 0 && old_attr->value != 0) {
        return false;
    }

    // Priority 2: If values are similar (e.g., both zero or both non-zero),
    if (old_attr->flags == 0x0000 && new_attr->flags != 0x0000) {
        return true;
    }
    if (new_attr->flags == 0x0000 && old_attr->flags != 0x0000) {
        return false;
    }

    return false; 
}

// Improved smart_interpret function
int smart_interpret(const char *device_path, struct smart_data *data, const char* firmware_rev) {
    // This function now acts as a wrapper to maintain compatibility
    return report_smart_data(stdout, device_path, data, firmware_rev);
}

// TODO: This function needs to be  more sophisticated in the future
SmartStatus smart_get_health_summary(const struct smart_data *data) {
    if (!data) {
        return SMART_HEALTH_UNKNOWN;
    }

    if (data->is_nvme) {
        SmartStatus nvme_status = SMART_HEALTH_OK;
        const struct smart_nvme *nvme = &data->data.nvme;

        if (nvme->critical_warning & 0x01) { 
            nvme_status = SMART_HEALTH_WARNING;
        }
        if (nvme->critical_warning & 0x02) { 
            nvme_status = SMART_HEALTH_WARNING; 
        }
        if (nvme->critical_warning & 0x04) { // Reliability is degraded
            nvme_status = SMART_HEALTH_WARNING;
        }
        if (nvme->critical_warning & 0x08) { // Media is in read-only mode
            nvme_status = SMART_HEALTH_FAILING;
        }
        if (nvme->critical_warning & 0x10) { 
            nvme_status = SMART_HEALTH_FAILING;
        }

        // Check percentage used (this is more of a wear indicator than immediate failure)
        if (nvme->percent_used >= 90 && nvme_status == SMART_HEALTH_OK) {
            // Not changing status to WARNING unless other critical flags are set, 
            // as high usage is expected over time.
        }

        return nvme_status;

    } else {
        SmartStatus overall_status = SMART_HEALTH_OK;

        for (int i = 0; i < data->attr_count; ++i) {
            const struct smart_attr *attr = &data->data.attrs[i];
            uint64_t raw_val = raw_to_uint64(attr->raw); 
            SmartStatus current_attr_status = SMART_HEALTH_OK;

            if (attr->threshold != 0 && attr->value <= attr->threshold) {
                if (attr->flags & 0x0001) { // SMART_PREFAIL_WARRANTY_ATTRIBUTE
                    current_attr_status = SMART_HEALTH_PREFAIL;
                } else {
                    current_attr_status = SMART_HEALTH_WARNING;
                }
            } 

            // Specific checks for critical attributes
            if (attr->id == 5 && raw_val > 0) {
                if (SMART_HEALTH_PREFAIL > current_attr_status) { 
                    current_attr_status = SMART_HEALTH_PREFAIL;
                }
            } else if (attr->id == 197 && raw_val > 0) { 
                if (SMART_HEALTH_PREFAIL > current_attr_status) { 
                    current_attr_status = SMART_HEALTH_PREFAIL;
                }
            } else if (attr->id == 198 && raw_val > 0) { 
                if (SMART_HEALTH_PREFAIL > current_attr_status) { 
                    current_attr_status = SMART_HEALTH_PREFAIL;
                }
            }

            if (current_attr_status > overall_status) {
                overall_status = current_attr_status;
            }
        }
   
        return overall_status;
    }
}
