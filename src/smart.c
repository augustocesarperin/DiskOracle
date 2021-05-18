#include "smart.h"
#include "pal.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int smart_read(const char *device, struct smart_data *out) {
    if (!device || !out) {
        fprintf(stderr, "Oops! smart_read needs a valid device and output structure.\\n");
        return 1;
    }
    memset(out, 0, sizeof(struct smart_data));
    return pal_get_smart_data(device, out);
}

static uint64_t raw_to_uint64(const unsigned char* raw_value) {
    uint64_t result = 0;
    for (int i = 0; i < 6; ++i) {
        result |= ((uint64_t)raw_value[i] << (i * 8));
    }
    return result;
}

int smart_interpret(const char *device, struct smart_data *data) {
    if (!data) {
        fprintf(stderr, "Hmm, DiskOracle couldn't find any SMART data for %s. That's unexpected.\n", device);
        return 1;
    }

    printf("\n========= DiskOracle SMART Data Report for %s =========\n", device);

    if (data->is_nvme) {
        printf("Drive Type: NVMe SSD - Reporting NVMe Specific Health Log!\\n\\n");
        printf("  Key Health Indicators:\\n");
        printf("  ----------------------\\n");
        printf("  [ Critical Warning Flags ] --> 0x%02X\\n", data->nvme.critical_warning);
        printf("     Bit 0 (Available Spare Below Threshold): %s\\n", (data->nvme.critical_warning & 0x01) ? "ALERT!" : "OK");
        printf("     Bit 1 (Temperature Above Threshold)  : %s\\n", (data->nvme.critical_warning & 0x02) ? "ALERT!" : "OK");
        printf("     Bit 2 (Reliability Degraded)         : %s\\n", (data->nvme.critical_warning & 0x04) ? "ALERT!" : "OK");
        printf("     Bit 3 (Media Read-Only)              : %s\\n", (data->nvme.critical_warning & 0x08) ? "ALERT!" : "OK");
        printf("     Bit 4 (Volatile Memory Backup Failed): %s\\n", (data->nvme.critical_warning & 0x10) ? "ALERT!" : "OK");
        printf("\\n");
        printf("  [ Composite Temperature ] --> %u K (Kelvin)\\n", data->nvme.temperature);
        printf("  [ Available Spare Space ] --> %u %%\\n", data->nvme.avail_spare);
        printf("  [ Spare Space Threshold ] --> %u %%\\n", data->nvme.spare_thresh);
        printf("  [ Percentage Used       ] --> %u %%\\n", data->nvme.percent_used);
        printf("\\n  Usage Statistics:\\n");
        printf("  -----------------\\n");
        printf("  [ Data Units Read       ] --> %llu (x512k blocks)\\n", (unsigned long long)data->nvme.data_units_read);
        printf("  [ Data Units Written    ] --> %llu (x512k blocks)\\n", (unsigned long long)data->nvme.data_units_written);
        printf("  [ Host Read Commands    ] --> %llu\\n", (unsigned long long)data->nvme.host_reads);
        printf("  [ Host Write Commands   ] --> %llu\\n", (unsigned long long)data->nvme.host_writes);
        printf("  [ Controller Busy Time  ] --> %llu minutes\\n", (unsigned long long)data->nvme.controller_busy_time);
        printf("  [ Power Cycles          ] --> %llu\\n", (unsigned long long)data->nvme.power_cycles);
        printf("  [ Power On Hours        ] --> %llu hours\\n", (unsigned long long)data->nvme.power_on_hours);
        printf("  [ Unsafe Shutdowns      ] --> %llu\\n", (unsigned long long)data->nvme.unsafe_shutdowns);
        printf("  [ Media & Data Integrity Errors ] --> %llu\\n", (unsigned long long)data->nvme.media_errors);
        printf("  [ Number of Error Info Log Entries ] --> %llu\\n", (unsigned long long)data->nvme.num_err_log_entries);
        printf("\\n");

    } else {
        printf("Drive Type: ATA/SATA - Reporting Classic SMART Attributes!\\n\\n");
        printf("  ID# ATTRIBUTE_NAME          FLAGS    VALUE WORST THRESH RAW_VALUE           STATUS\\n");
        printf("  ------------------------------------------------------------------------------------\\n");

        for (int i = 0; i < data->attr_count; ++i) {
            struct smart_attribute *attr = &data->attrs[i];
            char *name = "Unknown";
            const char* status_str = (attr->value > 0 && attr->threshold > 0 && attr->value <= attr->threshold) ? "FAILING" : "OK";
            if (attr->value == 0 && attr->threshold == 0 && attr->worst == 0) {
                status_str = "INFO";
            }

            switch (attr->id) {
                case 1: name = "Raw_Read_Error_Rate"; break;
                case 2: name = "Throughput_Performance"; break;
                case 3: name = "Spin_Up_Time"; break;
                case 4: name = "Start_Stop_Count"; break;
                case 5: name = "Reallocated_Sector_Ct"; status_str = (raw_to_uint64(attr->raw) > 0) ? "WARNING" : "OK"; break;
                case 7: name = "Seek_Error_Rate"; break;
                case 8: name = "Seek_Time_Performance"; break;
                case 9: name = "Power_On_Hours"; break;
                case 10: name = "Spin_Retry_Count"; status_str = (raw_to_uint64(attr->raw) > 0) ? "WARNING" : "OK"; break;
                case 11: name = "Calibration_Retry_Count"; break;
                case 12: name = "Power_Cycle_Count"; break;
                case 13: name = "Read_Soft_Error_Rate"; break;
                case 168: name = "SATA_Phy_Error_Count"; break;
                case 170: name = "Reserve_Block_Count"; break;
                case 171: name = "Program_Fail_Count"; break;
                case 172: name = "Erase_Fail_Count"; break;
                case 173: name = "Wear_Leveling_Count"; break;
                case 174: name = "Unexpected_Power_Loss_Ct"; break;
                case 175: name = "Program_Fail_Count_Chip"; break;
                case 176: name = "Erase_Fail_Count_Chip"; break;
                case 177: name = "Wear_Range_Delta"; break;
                case 179: name = "Used_Rsvd_Blk_Cnt_Tot"; break;
                case 180: name = "Unused_Rsvd_Blk_Cnt_Tot"; break;
                case 181: name = "Program_Fail_Cnt_Total"; break;
                case 182: name = "Erase_Fail_Count_Total"; break;
                case 183: name = "Runtime_Bad_Block"; break;
                case 184: name = "End-to-End_Error"; break;
                case 187: name = "Reported_Uncorrect"; status_str = (raw_to_uint64(attr->raw) > 0) ? "WARNING" : "OK"; break;
                case 188: name = "Command_Timeout"; status_str = (raw_to_uint64(attr->raw) > 0) ? "WARNING" : "OK"; break;
                case 189: name = "High_Fly_Writes"; break;
                case 190: name = "Airflow_Temperature_Cel"; name = "Airflow_Temperature_Cel"; break;
                case 191: name = "G-Sense_Error_Rate"; break;
                case 192: name = "Power-Off_Retract_Count"; break;
                case 193: name = "Load_Cycle_Count"; break;
                case 194: name = "Temperature_Celsius"; break;
                case 195: name = "Hardware_ECC_Recovered"; break;
                case 196: name = "Reallocated_Event_Count"; status_str = (raw_to_uint64(attr->raw) > 0) ? "WARNING" : "OK"; break;
                case 197: name = "Current_Pending_Sector"; status_str = (raw_to_uint64(attr->raw) > 0) ? "WARNING" : "OK"; break;
                case 198: name = "Offline_Uncorrectable"; status_str = (raw_to_uint64(attr->raw) > 0) ? "WARNING" : "OK"; break;
                case 199: name = "UDMA_CRC_Error_Count"; status_str = (raw_to_uint64(attr->raw) > 0) ? "WARNING" : "OK"; break;
                case 200: name = "Multi_Zone_Error_Rate"; break;
                case 201: name = "Soft_Read_Error_Rate"; break;
                case 202: name = "Data_Address_Mark_Errs"; break;
                case 203: name = "Run_Out_Cancel"; break;
                case 204: name = "Soft_ECC_Correction"; break;
                case 205: name = "Thermal_Asperity_Rate"; break;
                case 206: name = "Flying_Height"; break;
                case 207: name = "Spin_High_Current"; break;
                case 208: name = "Spin_Buzz"; break;
                case 209: name = "Offline_Seek_Performnce"; break;
                case 220: name = "Disk_Shift"; break;
                case 221: name = "G-Sense_Error_Rate_2"; break;
                case 222: name = "Loaded_Hours"; break;
                case 223: name = "Load_Retry_Count"; break;
                case 224: name = "Load_Friction"; break;
                case 225: name = "Load_Cycle_Count_2"; break;
                case 226: name = "Load_In_Time"; break;
                case 228: name = "Power-off_Retract_Count"; break;
                case 230: name = "Head_Amplitude"; break;
                case 231: name = "Temperature_Celsius_SSD"; break;
                case 232: name = "Available_Reservd_Space"; break;
                case 233: name = "Media_Wearout_Indicator"; break;
                case 234: name = "Average_Erase_Count_AND_Maximum_Erase_Count"; break;
                case 235: name = "Good_Block_Count_AND_System_Free_Block_Count"; break;
                case 240: name = "Head_Flying_Hours"; break;
                case 241: name = "Total_LBAs_Written"; break;
                case 242: name = "Total_LBAs_Read"; break;
                case 243: name = "Total_LBAs_Written_Expanded"; break;
                case 244: name = "Total_LBAs_Read_Expanded"; break;
                case 250: name = "Read_Error_Retry_Rate"; break;
                default: name = "Vendor_Specific"; break;
            }

            uint64_t raw_val = raw_to_uint64(attr->raw);

            printf("  %-3d %-25s %.4X %5d %5d %5d %10llu (",
                   attr->id, name, attr->flags, attr->value, attr->worst, attr->threshold, (unsigned long long)raw_val);
            for(int j=0; j<6; ++j) printf("%02X", attr->raw[j]);
            printf(")  %s\\n", status_str);
        }
    }
    printf("===================== End of Report ======================\\n\\n");
    return 0;
}

SmartHealthStatus smart_get_health_summary(const struct smart_data *data) {
    if (!data) return SMART_HEALTH_UNKNOWN;

    if (data->is_nvme) {
        SmartHealthStatus nvme_status = SMART_HEALTH_GOOD;
        uint8_t cw = data->nvme.critical_warning;

        if (cw & 0x04) return SMART_HEALTH_FAILING;
        if (cw & 0x08) return SMART_HEALTH_FAILING;
        if (data->nvme.percent_used > 110) {
             nvme_status = SMART_HEALTH_FAILING;
        }
        if (data->nvme.spare_thresh > 0 && data->nvme.avail_spare == 0) {
            nvme_status = SMART_HEALTH_FAILING;
        }
        else if (data->nvme.spare_thresh > 4 && data->nvme.avail_spare < (data->nvme.spare_thresh / 4)) {
            if (nvme_status < SMART_HEALTH_FAILING) nvme_status = SMART_HEALTH_FAILING;
        }
        if (data->nvme.media_errors > 100) {
            if (nvme_status < SMART_HEALTH_FAILING) nvme_status = SMART_HEALTH_FAILING;
        }
        if (nvme_status == SMART_HEALTH_FAILING) return SMART_HEALTH_FAILING;

        if (cw & 0x01) {
            if (nvme_status < SMART_HEALTH_WARNING) nvme_status = SMART_HEALTH_WARNING;
        }
        else if (data->nvme.spare_thresh > 0 && data->nvme.avail_spare < data->nvme.spare_thresh) {
            if (nvme_status < SMART_HEALTH_WARNING) nvme_status = SMART_HEALTH_WARNING;
        }
        if (cw & 0x02) {
            if (nvme_status < SMART_HEALTH_WARNING) nvme_status = SMART_HEALTH_WARNING;
        }
        if (data->nvme.temperature > 343) { 
            if (nvme_status < SMART_HEALTH_WARNING) nvme_status = SMART_HEALTH_WARNING;
        }
        if (cw & 0x10) {
            if (nvme_status < SMART_HEALTH_WARNING) nvme_status = SMART_HEALTH_WARNING;
        }
        if (data->nvme.percent_used >= 95 && data->nvme.percent_used <= 110) {
             if (nvme_status < SMART_HEALTH_WARNING) nvme_status = SMART_HEALTH_WARNING;
        }
        if (data->nvme.media_errors > 0 && data->nvme.media_errors <= 100) {
            if (nvme_status < SMART_HEALTH_WARNING) nvme_status = SMART_HEALTH_WARNING;
        }
        if (data->nvme.unsafe_shutdowns > 20) {
            if (nvme_status < SMART_HEALTH_WARNING) nvme_status = SMART_HEALTH_WARNING;
        }
        return nvme_status;

    } else {
        SmartHealthStatus current_status = SMART_HEALTH_GOOD;
        int reallocated_sectors_raw = -1;
        int pending_sectors_raw = -1;
        int uncorrectable_sectors_raw = -1;

        for (int i = 0; i < data->attr_count; ++i) {
            uint8_t id = data->attrs[i].id;
            uint8_t value = data->attrs[i].value;
            uint8_t threshold = data->attrs[i].threshold;
            uint16_t flags = data->attrs[i].flags;
            uint64_t raw_value = 0;
            for (int j = 0; j < 6; ++j) raw_value |= ((uint64_t)data->attrs[i].raw[j]) << (8*j);

            if ((flags & 0x0001) && threshold > 0) {
                if (value <= threshold) {
                    if (current_status < SMART_HEALTH_PREFAIL) {
                        current_status = SMART_HEALTH_PREFAIL;
                    }
                    if (id == 5 || id == 196 || id == 197 || id == 198) {
                         if (current_status < SMART_HEALTH_FAILING) {
                            current_status = SMART_HEALTH_FAILING;
                         }
                    }
                }
            }

            switch (id) {
                case 5:
                    reallocated_sectors_raw = raw_value;
                    if (raw_value > 50) {
                        if (current_status < SMART_HEALTH_FAILING) current_status = SMART_HEALTH_FAILING;
                    } else if (raw_value > 5) {
                        if (current_status < SMART_HEALTH_WARNING) current_status = SMART_HEALTH_WARNING;
                    }
                    break;
                case 197:
                    pending_sectors_raw = raw_value;
                    if (raw_value > 10) {
                        if (current_status < SMART_HEALTH_FAILING) current_status = SMART_HEALTH_FAILING;
                    } else if (raw_value > 0) {
                        if (current_status < SMART_HEALTH_WARNING) current_status = SMART_HEALTH_WARNING;
                    }
                    break;
                case 198:
                    uncorrectable_sectors_raw = raw_value;
                    if (raw_value > 10) {
                        if (current_status < SMART_HEALTH_FAILING) current_status = SMART_HEALTH_FAILING;
                    } else if (raw_value > 0) {
                        if (current_status < SMART_HEALTH_WARNING) current_status = SMART_HEALTH_WARNING;
                    }
                    break;
                case 199:
                    if (raw_value > 100) {
                         if (current_status < SMART_HEALTH_WARNING) current_status = SMART_HEALTH_WARNING;
                    }
                    break;
            }
        }
        return current_status;
    }
    return SMART_HEALTH_UNKNOWN;
}
