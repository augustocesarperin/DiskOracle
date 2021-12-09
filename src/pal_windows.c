#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Target Windows 10. Adjust if a different minimum is required.
#endif
#include "pal.h"
#include "nvme_hybrid.h"
#include "logging.h"
#include "smart.h"
#include "info.h"
#include <stdio.h>
#include <string.h> 
#include <ctype.h>  
#include <strsafe.h>
#include <stddef.h> // For offsetof
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <devioctl.h>
#include <conio.h>
#include <ntddscsi.h> 
#include <nvme.h>     // For NVME_COMMAND and related definitions, and STORAGE_PROTOCOL_SPECIFIC_DATA related enums
#include <devguid.h>  // For GUID_DEVINTERFACE_DISK (declaration or definition based on initguid.h)
#include <cfgmgr32.h> // Added for CM_Get_Device_ID_ExA, MAX_DEVICE_ID_LEN, CR_SUCCESS
#include <winerror.h> // Para HRESULT_FROM_WIN32
// Explicitly define the storage for GUID_DEVINTERFACE_DISK using its direct name.
#ifdef __cplusplus
extern "C" {
#endif
const GUID GUID_DEVINTERFACE_DISK = 
    { 0x53f56307L, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } };
#ifdef __cplusplus
}
#endif

// Define PAL_DEV for Windows implementation
#define PAL_DEV HANDLE

// Definições para os comandos SMART ATA
#define ATA_CMD_SMART           0xB0    // Comando SMART
#define SMART_CMD_READ_DATA     0xD0    // Subcomando READ DATA
#define SMART_CMD_READ_THRESH   0xD1    // Subcomando READ THRESHOLDS
#define SMART_CMD_RETURN_STATUS 0xDA    // Subcomando RETURN STATUS

// Estrutura para buffer ATA pass-through
typedef struct _ATA_PASS_THROUGH_EX_WITH_BUFFER {
    ATA_PASS_THROUGH_EX apt;
    ULONG Filler; 
    UCHAR DataBuf[512];
} ATA_PASS_THROUGH_EX_WITH_BUFFER;

//  SMART  ATA/SATA
static int smart_read_ata(PAL_DEV device, struct smart_data* out)
{
    if (!device || !out) {
        fprintf(stderr, "[ERROR] Invalid device handle or output buffer\n");
        return PAL_STATUS_INVALID_PARAMETER;
    }

    // First check if SMART is enabled
    ATA_PASS_THROUGH_EX_WITH_BUFFER smart_status_buf = {0};
    
    smart_status_buf.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    smart_status_buf.apt.AtaFlags = ATA_FLAGS_DRDY_REQUIRED;
    smart_status_buf.apt.DataTransferLength = 0;
    smart_status_buf.apt.TimeOutValue = 5; // 5 seconds timeout
    smart_status_buf.apt.DataBufferOffset = 0;
    
    // Command registers
    smart_status_buf.apt.CurrentTaskFile[0] = 0xDA; // Features register: SMART RETURN STATUS
    smart_status_buf.apt.CurrentTaskFile[1] = 0; // Sector Count
    smart_status_buf.apt.CurrentTaskFile[2] = 0; // LBA Low
    smart_status_buf.apt.CurrentTaskFile[3] = 0x4F; // LBA Mid (sector number)
    smart_status_buf.apt.CurrentTaskFile[4] = 0xC2; // LBA High (cylinder low)
    smart_status_buf.apt.CurrentTaskFile[5] = 0xA0; // Device register (master device)
    smart_status_buf.apt.CurrentTaskFile[6] = 0xB0; // Command register: SMART
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        device,
        IOCTL_ATA_PASS_THROUGH,
        &smart_status_buf,
        sizeof(ATA_PASS_THROUGH_EX_WITH_BUFFER),
        &smart_status_buf,
        sizeof(ATA_PASS_THROUGH_EX_WITH_BUFFER),
        &bytesReturned,
        NULL
    );
    
    if (!result) {
        return PAL_STATUS_IO_ERROR;
    }
    
    ATA_PASS_THROUGH_EX_WITH_BUFFER smart_read_buf = {0};
    
    smart_read_buf.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    smart_read_buf.apt.AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
    smart_read_buf.apt.DataTransferLength = sizeof(smart_read_buf.DataBuf);
    smart_read_buf.apt.TimeOutValue = 5;
    smart_read_buf.apt.DataBufferOffset = FIELD_OFFSET(ATA_PASS_THROUGH_EX_WITH_BUFFER, DataBuf);
    
    smart_read_buf.apt.CurrentTaskFile[0] = 0xD0; // Features register: SMART READ DATA
    smart_read_buf.apt.CurrentTaskFile[1] = 1; // Sector Count: 1 sector
    smart_read_buf.apt.CurrentTaskFile[2] = 0; // LBA Low
    smart_read_buf.apt.CurrentTaskFile[3] = 0x4F; // LBA Mid (sector number)
    smart_read_buf.apt.CurrentTaskFile[4] = 0xC2; // LBA High (cylinder low)
    smart_read_buf.apt.CurrentTaskFile[5] = 0xA0; // Device register (master device)
    smart_read_buf.apt.CurrentTaskFile[6] = 0xB0; // Command register: SMART
    
    bytesReturned = 0;
    result = DeviceIoControl(
        device,
        IOCTL_ATA_PASS_THROUGH,
        &smart_read_buf,
        sizeof(ATA_PASS_THROUGH_EX_WITH_BUFFER),
        &smart_read_buf,
        sizeof(ATA_PASS_THROUGH_EX_WITH_BUFFER),
        &bytesReturned,
        NULL
    );
    
    if (!result) {
        fprintf(stderr, "[ERROR] SMART READ DATA command failed, error: %lu\n", GetLastError());
        return PAL_STATUS_IO_ERROR;
    }
    
    // SMART data is valid, parse it and fill the output structure
    int attr_count = 0;
    
    for (int i = 0; i < MAX_SMART_ATTRIBUTES; i++) {  // ATA has up to 30 attributes
        int offset = 2 + (i * 12);
        BYTE attr_id = smart_read_buf.DataBuf[offset];
        
        if (attr_id == 0) {
            continue;
        }

        // Extract attribute values
        BYTE flags_lo = smart_read_buf.DataBuf[offset + 1];
        BYTE flags_hi = smart_read_buf.DataBuf[offset + 2];
        BYTE value = smart_read_buf.DataBuf[offset + 3];
        BYTE worst = smart_read_buf.DataBuf[offset + 4];
        BYTE threshold = smart_read_buf.DataBuf[offset + 5];
        
        UINT64 raw_value = 0;
        for (int j = 0; j < 6; j++) {
            raw_value |= ((UINT64)smart_read_buf.DataBuf[offset + 6 + j]) << (j * 8);
        }
        
        if (attr_count < MAX_SMART_ATTRIBUTES) {
            out->data.attrs[attr_count].id = attr_id;
            out->data.attrs[attr_count].value = value;
            out->data.attrs[attr_count].worst = worst;
            out->data.attrs[attr_count].threshold = threshold;
            
            // Copy raw value bytes
            for (int j = 0; j < 6; j++) {
                out->data.attrs[attr_count].raw[j] = smart_read_buf.DataBuf[offset + 6 + j];
            }
            
            out->data.attrs[attr_count].flags = (flags_hi << 8) | flags_lo;
            
            // Set attribute name based on ID
            switch(attr_id) {
                case 1: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Raw_Read_Error_Rate"); break;
                case 2: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Throughput_Performance"); break;
                case 3: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Spin_Up_Time"); break;
                case 4: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Start_Stop_Count"); break;
                case 5: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Reallocated_Sector_Ct"); break;
                case 7: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Seek_Error_Rate"); break;
                case 8: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Seek_Time_Performance"); break;
                case 9: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Power_On_Hours"); break;
                case 10: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Spin_Retry_Count"); break;
                case 11: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Calibration_Retry_Count"); break;
                case 12: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Power_Cycle_Count"); break;
                case 13: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Read_Soft_Error_Rate"); break;
                case 183: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Runtime_Bad_Block"); break;
                case 184: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "End-to-End_Error"); break;
                case 187: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Reported_Uncorrect"); break;
                case 188: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Command_Timeout"); break;
                case 190: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Airflow_Temperature_Cel"); break;
                case 194: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Temperature_Celsius"); break;
                case 196: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Reallocated_Event_Count"); break;
                case 197: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Current_Pending_Sector"); break;
                case 198: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Offline_Uncorrectable"); break;
                case 199: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "UDMA_CRC_Error_Count"); break;
                case 200: strcpy_s(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Multi_Zone_Error_Rate"); break;
                default: 
                    StringCchPrintfA(out->data.attrs[attr_count].name, 
                                     sizeof(out->data.attrs[attr_count].name), 
                                     "Unknown_Attribute_%d", attr_id);
                    break;
            }
            
            attr_count++;
        }
    }
    
    out->attr_count = attr_count;
    
            return PAL_STATUS_SUCCESS; 
        }

static pal_status_t pal_windows_get_nvme_smart_log_via_query_prop_direct(
    const char* device_path, 
    const nvme_hybrid_context_t* context,
    BYTE* user_buffer, 
    DWORD user_buffer_size, 
    DWORD* bytes_returned,
    nvme_access_result_t* method_result
);
static pal_status_t pal_windows_get_nvme_smart_log_via_protocol_cmd_direct(
    const char* device_path,
    const nvme_hybrid_context_t* context,
    BYTE* user_buffer,
    DWORD user_buffer_size,
    DWORD* bytes_returned,
    nvme_access_result_t* method_result
);
static pal_status_t pal_get_smart_data_nvme_hybrid(
    const char* device_path, 
    nvme_hybrid_context_t* context, 
    BYTE* user_buffer, 
    DWORD user_buffer_size, 
    DWORD* bytes_returned,
    nvme_access_result_t* overall_result
);
static void trim_trailing_spaces(char *str);
static bool get_device_path_from_instance_id(const char* instance_id, char* device_path_buffer, size_t buffer_size);
static void pal_windows_parse_nvme_health_log(const NVME_HEALTH_INFO_LOG* sdk_health_log, struct smart_nvme* target_smart_nvme) {
    if (!sdk_health_log || !target_smart_nvme) {
        return;
    }
    target_smart_nvme->critical_warning = sdk_health_log->CriticalWarning.AsUchar;   
    // Temperature is UCHAR Temperature[2] in NVME_HEALTH_INFO_LOG (Little Endian format from SDK).
    // Copying raw bytes. Conversion to Kelvin/Celsius and display handled by smart_interpret.
    memcpy(target_smart_nvme->temperature, sdk_health_log->Temperature, 2); 

    target_smart_nvme->avail_spare = sdk_health_log->AvailableSpare;
    target_smart_nvme->spare_thresh = sdk_health_log->AvailableSpareThreshold;
    target_smart_nvme->percent_used = sdk_health_log->PercentageUsed;
// NVME_HEALTH_INFO_LOG fields (e.g., DataUnitRead) are ULONGLONG[2] (array of two 64-bit integers).
// struct smart_nvme fields (e.g., data_units_read) are assumed to match this structure (e.g., uint64_t data_units_read[2]).
#define COPY_128BIT_FIELD_ARRAY(dest_array, src_ul_array) \
    do { \
        dest_array[0] = src_ul_array[0]; \
        dest_array[1] = src_ul_array[1]; \
    } while (0)

    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->data_units_read, sdk_health_log->DataUnitRead);
    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->data_units_written, sdk_health_log->DataUnitWritten);
    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->host_read_commands, sdk_health_log->HostReadCommands);
    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->host_write_commands, sdk_health_log->HostWrittenCommands);
    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->controller_busy_time, sdk_health_log->ControllerBusyTime);
    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->power_cycles, sdk_health_log->PowerCycle);
    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->power_on_hours, sdk_health_log->PowerOnHours);
    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->unsafe_shutdowns, sdk_health_log->UnsafeShutdowns);
    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->media_errors, sdk_health_log->MediaErrors);
    COPY_128BIT_FIELD_ARRAY(target_smart_nvme->num_err_log_entries, sdk_health_log->ErrorInfoLogEntryCount);

#undef COPY_128BIT_FIELD_ARRAY
}
pal_status_t pal_get_smart_data(const char *device_path, struct smart_data *out) {
    if (!device_path || !out) {
        return PAL_STATUS_INVALID_PARAMETER;
    }
    memset(out, 0, sizeof(struct smart_data));

    HANDLE hDeviceForBusType = CreateFileA(device_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    BOOL is_nvme_drive = FALSE;
    BasicDriveInfo basic_info; 
    ZeroMemory(&basic_info, sizeof(BasicDriveInfo));

    if (hDeviceForBusType == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_ACCESS_DENIED) return PAL_STATUS_ACCESS_DENIED;
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return PAL_STATUS_DEVICE_NOT_FOUND;
        return PAL_STATUS_DEVICE_ERROR; 
    }
    STORAGE_PROPERTY_QUERY query_desc;
    STORAGE_DEVICE_DESCRIPTOR dev_desc_buffer = {0};
    DWORD bytesReturned_desc = 0; 
    memset(&query_desc, 0, sizeof(query_desc));
    query_desc.PropertyId = StorageDeviceProperty;
    query_desc.QueryType = PropertyStandardQuery;

    if (DeviceIoControl(hDeviceForBusType, IOCTL_STORAGE_QUERY_PROPERTY, &query_desc, sizeof(query_desc), &dev_desc_buffer, sizeof(dev_desc_buffer), &bytesReturned_desc, NULL) && 
        bytesReturned_desc >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        if (dev_desc_buffer.BusType == BusTypeNvme) {
            is_nvme_drive = TRUE;
        }
    }
    CloseHandle(hDeviceForBusType); 

    out->is_nvme = is_nvme_drive ? 1 : 0;
    out->drive_type = is_nvme_drive ? DRIVE_TYPE_NVME : DRIVE_TYPE_ATA;
    if (is_nvme_drive) {
        nvme_hybrid_context_t local_hybrid_ctx;
        ZeroMemory(&local_hybrid_ctx, sizeof(nvme_hybrid_context_t));
        StringCchCopyA(local_hybrid_ctx.device_path, MAX_PATH, device_path);
        local_hybrid_ctx.try_query_property = TRUE;
        local_hybrid_ctx.try_protocol_command = TRUE;
        local_hybrid_ctx.verbose_logging = FALSE;
        local_hybrid_ctx.benchmark_mode = FALSE;
        local_hybrid_ctx.cache_enabled = FALSE;

        BYTE nvme_log_buffer[NVME_LOG_PAGE_SIZE_BYTES];
        DWORD nvme_bytes_returned = 0;
        nvme_access_result_t temp_overall_hybrid_result;
        ZeroMemory(&temp_overall_hybrid_result, sizeof(temp_overall_hybrid_result));
        pal_status_t nvme_status = pal_get_smart_data_nvme_hybrid(device_path, &local_hybrid_ctx, nvme_log_buffer, sizeof(nvme_log_buffer), &nvme_bytes_returned, &temp_overall_hybrid_result);
        
        local_hybrid_ctx.last_operation_result = temp_overall_hybrid_result;

        if (nvme_status == PAL_STATUS_SUCCESS && nvme_bytes_returned >= sizeof(NVME_HEALTH_INFO_LOG) && temp_overall_hybrid_result.success) {
            memcpy(&out->data.nvme.raw_health_log, nvme_log_buffer, sizeof(NVME_HEALTH_INFO_LOG)); 
            pal_windows_parse_nvme_health_log(&out->data.nvme.raw_health_log, &out->data.nvme);
            out->attr_count = 1; 
        } else {
            out->attr_count = 0; 
        }
        return nvme_status; 

    } else {
        
        HANDLE hDevice = CreateFileA(
            device_path,
                                            GENERIC_READ | GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            NULL,
                                            OPEN_EXISTING,
            0,
            NULL
        );
        
        if (hDevice == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) {
                return PAL_STATUS_DEVICE_NOT_FOUND;
            }
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                return PAL_STATUS_ACCESS_DENIED;
            }
            return PAL_STATUS_ERROR;
        }
        
        int ata_result = smart_read_ata(hDevice, out); // 'out' is struct smart_data*
        
            CloseHandle(hDevice);
        
        if (ata_result == PAL_STATUS_SUCCESS) {
            return PAL_STATUS_SUCCESS; 
        } else {
            return ata_result;
        }
    }
    return PAL_STATUS_ERROR; 
}
int64_t pal_get_device_size(const char *device_path) {
    if (!device_path) {
        return -1; 
    }

    HANDLE hDevice = CreateFileA(device_path,
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               0,
                               NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return -1;
    }

    GET_LENGTH_INFORMATION length_info;
    DWORD bytes_returned_ioctl = 0;

    BOOL success = DeviceIoControl(hDevice,
                                 IOCTL_DISK_GET_LENGTH_INFO,
                                 NULL,
                                 0,
                                 &length_info,
                                 sizeof(length_info),
                                 &bytes_returned_ioctl,
                                 NULL);
    DWORD dioctl_error = success ? 0 : GetLastError();

    CloseHandle(hDevice);

    if (!success || bytes_returned_ioctl == 0) {
        return -1;
    }

    return length_info.Length.QuadPart;
}

pal_status_t pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info) {
    if (!device_path || !info) {
        return PAL_STATUS_INVALID_PARAMETER;
    }

    memset(info, 0, sizeof(BasicDriveInfo));

    strcpy_s(info->path, sizeof(info->path), device_path);

    HANDLE hDevice = CreateFileA(device_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, 0, NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return PAL_STATUS_DEVICE_NOT_FOUND;
        }
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            return PAL_STATUS_ACCESS_DENIED;
        }
        return PAL_STATUS_ERROR;
    }

    STORAGE_PROPERTY_QUERY query_desc_prop;
    memset(&query_desc_prop, 0, sizeof(query_desc_prop));
    query_desc_prop.PropertyId = StorageDeviceProperty;
    query_desc_prop.QueryType = PropertyStandardQuery;

    BYTE buffer_desc[2048];
    memset(buffer_desc, 0, sizeof(buffer_desc));
    DWORD bytes_returned_desc_ioctl = 0;

    BOOL success_desc = DeviceIoControl(hDevice,
                                 IOCTL_STORAGE_QUERY_PROPERTY,
                                 &query_desc_prop,
                                 sizeof(query_desc_prop),
                                 buffer_desc,
                                 sizeof(buffer_desc),
                                 &bytes_returned_desc_ioctl,
                                 NULL);

    if (success_desc && bytes_returned_desc_ioctl > 0) {
        STORAGE_DEVICE_DESCRIPTOR *sdd_desc = (STORAGE_DEVICE_DESCRIPTOR *)buffer_desc;
        if (sdd_desc->ProductIdOffset > 0 && sdd_desc->ProductIdOffset < sizeof(buffer_desc)) {
            char* model_name_ptr = (char*)buffer_desc + sdd_desc->ProductIdOffset;
            trim_trailing_spaces(model_name_ptr);
            strncpy_s(info->model, sizeof(info->model), model_name_ptr, _TRUNCATE);
        }
        if (sdd_desc->SerialNumberOffset > 0 && sdd_desc->SerialNumberOffset < sizeof(buffer_desc)) {
            char* serial_ptr = (char*)buffer_desc + sdd_desc->SerialNumberOffset;
            trim_trailing_spaces(serial_ptr);
            strncpy_s(info->serial, sizeof(info->serial), serial_ptr, _TRUNCATE);
        }
        if (sdd_desc->ProductRevisionOffset > 0 && sdd_desc->ProductRevisionOffset < sizeof(buffer_desc)) {
            char* firmware_ptr = (char*)buffer_desc + sdd_desc->ProductRevisionOffset;
            trim_trailing_spaces(firmware_ptr);
            strncpy_s(info->firmware_rev, sizeof(info->firmware_rev), firmware_ptr, _TRUNCATE);
        }
        switch (sdd_desc->BusType) {
            case BusTypeSata: strncpy_s(info->bus_type, sizeof(info->bus_type), "SATA", _TRUNCATE); break;
            case BusTypeScsi: strncpy_s(info->bus_type, sizeof(info->bus_type), "SCSI", _TRUNCATE); break;
            case BusTypeAtapi: strncpy_s(info->bus_type, sizeof(info->bus_type), "ATAPI", _TRUNCATE); break;
            case BusTypeAta: strncpy_s(info->bus_type, sizeof(info->bus_type), "ATA", _TRUNCATE); break;
            case BusTypeUsb: strncpy_s(info->bus_type, sizeof(info->bus_type), "USB", _TRUNCATE); break;
            case BusType1394: strncpy_s(info->bus_type, sizeof(info->bus_type), "1394", _TRUNCATE); break;
            case BusTypeSsa: strncpy_s(info->bus_type, sizeof(info->bus_type), "SSA", _TRUNCATE); break;
            case BusTypeFibre: strncpy_s(info->bus_type, sizeof(info->bus_type), "Fibre Channel", _TRUNCATE); break;
            case BusTypeRAID: strncpy_s(info->bus_type, sizeof(info->bus_type), "RAID", _TRUNCATE); break;
            case BusTypeNvme: strncpy_s(info->bus_type, sizeof(info->bus_type), "NVMe", _TRUNCATE); break;
            case BusTypeSd: strncpy_s(info->bus_type, sizeof(info->bus_type), "SD", _TRUNCATE); break;
            case BusTypeMmc: strncpy_s(info->bus_type, sizeof(info->bus_type), "MMC", _TRUNCATE); break;
            case BusTypeVirtual: strncpy_s(info->bus_type, sizeof(info->bus_type), "Virtual", _TRUNCATE); break;
            case BusTypeFileBackedVirtual: strncpy_s(info->bus_type, sizeof(info->bus_type), "File Backed Virtual", _TRUNCATE); break;
            default: strncpy_s(info->bus_type, sizeof(info->bus_type), "Other", _TRUNCATE); break;
        }
        info->bus_type[sizeof(info->bus_type)-1] = '\0';

        STORAGE_PROPERTY_QUERY query_penalty;
        memset(&query_penalty, 0, sizeof(query_penalty));
        query_penalty.PropertyId = StorageDeviceSeekPenaltyProperty;
        query_penalty.QueryType = PropertyStandardQuery;

        DEVICE_SEEK_PENALTY_DESCRIPTOR penalty_desc;
        DWORD bytes_returned_penalty = 0;
        if (DeviceIoControl(hDevice, 
                              IOCTL_STORAGE_QUERY_PROPERTY, 
                              &query_penalty, 
                              sizeof(query_penalty), 
                              &penalty_desc, 
                              sizeof(penalty_desc), 
                              &bytes_returned_penalty, 
                              NULL) && bytes_returned_penalty >= sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR)) {
            if (penalty_desc.IncursSeekPenalty) {
                strncpy_s(info->type, sizeof(info->type), "HDD", _TRUNCATE);
            } else {
                strncpy_s(info->type, sizeof(info->type), sdd_desc->BusType == BusTypeNvme ? "NVMe" : "SSD", _TRUNCATE);
            }
        } else {
            strncpy_s(info->type, sizeof(info->type), sdd_desc->BusType == BusTypeNvme ? "NVMe" : "Unknown (SeekTypeFail)", _TRUNCATE);
        }
        info->type[sizeof(info->type)-1] = '\0';
        info->smart_capable = true; 
    } else {
        CloseHandle(hDevice); 
        return PAL_STATUS_IO_ERROR; 
    }
    CloseHandle(hDevice);
    return PAL_STATUS_SUCCESS;
}

pal_status_t pal_create_directory(const char *path) {
    if (!CreateDirectoryA(path, NULL)) {
        DWORD error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS) {
            return PAL_STATUS_SUCCESS; 
        }
        return PAL_STATUS_ERROR; 
    }
    return PAL_STATUS_SUCCESS;
}

pal_status_t pal_get_current_directory(char* buffer, size_t size) {
    DWORD result = GetCurrentDirectoryA((DWORD)size, buffer);
    if (result == 0 || result > size) {
        return PAL_STATUS_ERROR;
    }
    return PAL_STATUS_SUCCESS;
}

pal_status_t pal_initialize(void) {
    return PAL_STATUS_SUCCESS;
}

void pal_cleanup(void) {
}

int pal_do_surface_scan(void *handle, unsigned long long start_lba, unsigned long long lba_count, pal_scan_callback callback, void *user_data) {
    (void)handle; (void)start_lba; (void)lba_count; (void)callback; (void)user_data;
    return PAL_STATUS_UNSUPPORTED;
}

// Define necessary flags and structures if not perfectly available from SDK headers for MinGW
// These are standard values from winioctl.h / ntddstor.h
#ifndef IOCTL_STORAGE_QUERY_PROPERTY
#define IOCTL_STORAGE_QUERY_PROPERTY CTL_CODE(IOCTL_STORAGE_BASE, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#ifndef PropertyStandardQuery
#define PropertyStandardQuery ((STORAGE_QUERY_TYPE)0)
#endif
#ifndef StorageDeviceProperty
#define StorageDeviceProperty ((STORAGE_PROPERTY_ID)0)
#endif
#ifndef StorageAdapterProtocolSpecificProperty // For controller-level protocol queries
#define StorageAdapterProtocolSpecificProperty ((STORAGE_PROPERTY_ID)49) 
#endif
#ifndef ProtocolTypeNvme
#define ProtocolTypeNvme ((STORAGE_PROTOCOL_TYPE)3)
#endif
#ifndef NVMeDataTypeLogPage
#define NVMeDataTypeLogPage ((STORAGE_PROTOCOL_NVME_DATA_TYPE)2)
#endif

static pal_status_t pal_windows_get_nvme_smart_log_via_query_prop_direct(
    const char* device_path, 
    const nvme_hybrid_context_t* context,
    BYTE* user_buffer, 
    DWORD user_buffer_size, 
    DWORD* bytes_returned,
    nvme_access_result_t* method_result
) {
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    BOOL bResult = FALSE;
    DWORD dwBytesReturnedIoctl = 0;
    pal_status_t final_pal_status = PAL_STATUS_ERROR;
    
    PSTORAGE_PROPERTY_QUERY pQuery = NULL;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA pProtocolDataIn = NULL;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR pProtocolDataDescriptor = NULL;
    BYTE* pInBuffer = NULL;
    BYTE* pOutBuffer = NULL;
    DWORD dwInBufferSz = 0;
    DWORD dwOutBufferSz = 0;

    ULARGE_INTEGER startTime, endTime;
    GetSystemTimeAsFileTime((FILETIME*)&startTime); 

    if (!device_path || !user_buffer || !bytes_returned || !method_result || !context) {
        if (method_result) {
            ZeroMemory(method_result, sizeof(nvme_access_result_t));
            StringCchCopyA(method_result->method_name, sizeof(method_result->method_name), NVME_METHOD_NAME_QUERY_PROPERTY);
            method_result->method_used = NVME_ACCESS_METHOD_QUERY_PROPERTY;
            method_result->error_code = ERROR_INVALID_PARAMETER;
            method_result->success = FALSE;
            GetSystemTimeAsFileTime((FILETIME*)&endTime);
            ULARGE_INTEGER elapsed_err; elapsed_err.QuadPart = endTime.QuadPart - startTime.QuadPart;
            method_result->execution_time_ms = (DWORD)(elapsed_err.QuadPart / 10000);
        }
        return PAL_STATUS_INVALID_PARAMETER;
    }

    ZeroMemory(method_result, sizeof(nvme_access_result_t));
    StringCchCopyA(method_result->method_name, sizeof(method_result->method_name), NVME_METHOD_NAME_QUERY_PROPERTY);
    method_result->method_used = NVME_ACCESS_METHOD_QUERY_PROPERTY;
    method_result->success = FALSE;
    method_result->error_code = 0;

    if (user_buffer_size < NVME_LOG_PAGE_SIZE_BYTES) {
        method_result->error_code = ERROR_INSUFFICIENT_BUFFER;
        goto complete_and_cleanup; 
    }
    *bytes_returned = 0;

    hDevice = CreateFileA(device_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        method_result->error_code = GetLastError();
        goto complete_and_cleanup; 
    }
    dwInBufferSz = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    pInBuffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwInBufferSz);
    if (!pInBuffer) {
        method_result->error_code = GetLastError();
        final_pal_status = PAL_STATUS_NO_MEMORY; 
        goto complete_and_cleanup; 
    }

    dwOutBufferSz = sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR) + NVME_LOG_PAGE_SIZE_BYTES;
    pOutBuffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwOutBufferSz);
    if (!pOutBuffer) {
        method_result->error_code = GetLastError();
        final_pal_status = PAL_STATUS_NO_MEMORY; 
        goto complete_and_cleanup; 
    }
    pQuery = (PSTORAGE_PROPERTY_QUERY)pInBuffer;
    pProtocolDataIn = (PSTORAGE_PROTOCOL_SPECIFIC_DATA)pQuery->AdditionalParameters;
    pQuery->PropertyId = StorageAdapterProtocolSpecificProperty;
    pQuery->QueryType = PropertyStandardQuery;
    pProtocolDataIn->ProtocolType = ProtocolTypeNvme;
    pProtocolDataIn->DataType = NVMeDataTypeLogPage;
    pProtocolDataIn->ProtocolDataRequestValue = NVME_LOG_PAGE_HEALTH_INFO;
    pProtocolDataIn->ProtocolDataRequestSubValue = 0; 
    pProtocolDataIn->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA); 
    pProtocolDataIn->ProtocolDataLength = NVME_LOG_PAGE_SIZE_BYTES;

    bResult = DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, pInBuffer, dwInBufferSz, pOutBuffer, dwOutBufferSz, &dwBytesReturnedIoctl, NULL);
    method_result->error_code = GetLastError(); // Capture error code immediately after DeviceIoControl

    if (bResult && dwBytesReturnedIoctl > 0) {
        pProtocolDataDescriptor = (PSTORAGE_PROTOCOL_DATA_DESCRIPTOR)pOutBuffer;
        if (pProtocolDataDescriptor->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR) || pProtocolDataDescriptor->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) {
            final_pal_status = PAL_STATUS_IO_ERROR;
            goto complete_and_cleanup; 
        }
        PSTORAGE_PROTOCOL_SPECIFIC_DATA pProtocolDataOut = &pProtocolDataDescriptor->ProtocolSpecificData;

        if (pProtocolDataOut->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) {
            
             final_pal_status = PAL_STATUS_IO_ERROR;
             goto complete_and_cleanup; 
        }
        if (pProtocolDataOut->ProtocolDataLength < NVME_LOG_PAGE_SIZE_BYTES) {
            final_pal_status = PAL_STATUS_ERROR_DATA_UNDERFLOW;
            goto complete_and_cleanup; 
        }

        BYTE* pLogDataStart = (BYTE*)pProtocolDataOut + pProtocolDataOut->ProtocolDataOffset;
        DWORD min_bytes_needed = (DWORD)((BYTE*)pLogDataStart - (BYTE*)pOutBuffer) + NVME_LOG_PAGE_SIZE_BYTES;

        if (dwBytesReturnedIoctl < min_bytes_needed) {
            final_pal_status = PAL_STATUS_IO_ERROR;
            goto complete_and_cleanup; 
        }
        memcpy(user_buffer, pLogDataStart, NVME_LOG_PAGE_SIZE_BYTES);
        *bytes_returned = NVME_LOG_PAGE_SIZE_BYTES;
        final_pal_status = PAL_STATUS_SUCCESS;
        method_result->success = TRUE; 
        method_result->error_code = 0; 

    } else { 
        if (method_result->error_code == ERROR_INVALID_PARAMETER) final_pal_status = PAL_STATUS_INVALID_PARAMETER; 
        else if (method_result->error_code == ERROR_NOT_SUPPORTED) final_pal_status = PAL_STATUS_UNSUPPORTED;
        else if (method_result->error_code == ERROR_ACCESS_DENIED) final_pal_status = PAL_STATUS_ACCESS_DENIED;
        else final_pal_status = PAL_STATUS_IO_ERROR;
    }

complete_and_cleanup:
    GetSystemTimeAsFileTime((FILETIME*)&endTime);
    ULARGE_INTEGER elapsed;
    elapsed.QuadPart = endTime.QuadPart - startTime.QuadPart;
    method_result->execution_time_ms = (DWORD)(elapsed.QuadPart / 10000);

    if (hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(hDevice);
    }
    if (pInBuffer) HeapFree(GetProcessHeap(), 0, pInBuffer);
    if (pOutBuffer) HeapFree(GetProcessHeap(), 0, pOutBuffer);

    return final_pal_status;
}

// Implementação de pal_get_smart_data_nvme_protocol_cmd
static pal_status_t pal_windows_get_nvme_smart_log_via_protocol_cmd_direct(
    const char* device_path,
    const nvme_hybrid_context_t* context,
    BYTE* user_buffer,
    DWORD user_buffer_size,
    DWORD* bytes_returned,
    nvme_access_result_t* method_result
) {
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    BOOL bResult = FALSE;
    DWORD dwBytesReturnedIoctl = 0;
    pal_status_t final_pal_status = PAL_STATUS_ERROR;
    
    BYTE* pCommandBuffer = NULL; 
    DWORD dwCommandBufferSz = 0;
    DWORD command_fam_offset = FIELD_OFFSET(STORAGE_PROTOCOL_COMMAND, Command);

    ULARGE_INTEGER startTime, endTime;
    GetSystemTimeAsFileTime((FILETIME*)&startTime);

    if (method_result) {
        ZeroMemory(method_result, sizeof(nvme_access_result_t));
        StringCchCopyA(method_result->method_name, sizeof(method_result->method_name), NVME_METHOD_NAME_PROTOCOL_COMMAND);
        method_result->method_used = NVME_ACCESS_METHOD_PROTOCOL_COMMAND;
        method_result->success = FALSE;
        method_result->error_code = 0;
        } else {
        fprintf(stderr, "[PAL_CRITICAL_ERROR PAL_NVME_SMART_IOCTL_PROTO] method_result is NULL.\n"); fflush(stderr);
        return PAL_STATUS_INVALID_PARAMETER;
    }

    if (!device_path || !user_buffer || !bytes_returned || !context) {
        fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_IOCTL_PROTO] Invalid parameters (device_path, user_buffer, bytes_returned, or context is NULL).\n"); fflush(stderr);
        method_result->error_code = ERROR_INVALID_PARAMETER;
        final_pal_status = PAL_STATUS_INVALID_PARAMETER;
        goto complete_and_cleanup_proto;
    }
    if (context->verbose_logging) {
    } else {
    }
    if (user_buffer_size < NVME_LOG_PAGE_SIZE_BYTES) {
        final_pal_status = PAL_STATUS_BUFFER_TOO_SMALL;
        method_result->error_code = ERROR_INSUFFICIENT_BUFFER;
        goto complete_and_cleanup_proto;
    }
    *bytes_returned = 0;

    dwCommandBufferSz = command_fam_offset + sizeof(NVME_COMMAND) + NVME_LOG_PAGE_SIZE_BYTES;
    pCommandBuffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwCommandBufferSz);
    if (!pCommandBuffer) {
        method_result->error_code = GetLastError();
        final_pal_status = PAL_STATUS_NO_MEMORY; 
        goto complete_and_cleanup_proto; 
    }

    STORAGE_PROTOCOL_COMMAND* spt_command = (STORAGE_PROTOCOL_COMMAND*)pCommandBuffer;
    NVME_COMMAND* nvme_command = (NVME_COMMAND*)((BYTE*)spt_command + command_fam_offset);
    BYTE* data_buffer_ptr_in_cmd_buff = (BYTE*)((BYTE*)nvme_command + sizeof(NVME_COMMAND));

    spt_command->Version = command_fam_offset; 
    spt_command->Length = command_fam_offset;  
    spt_command->ProtocolType = ProtocolTypeNvme;
    spt_command->Flags = 0x00000002UL; // Start with DATA_IN, can be made configurable via context later
    spt_command->ReturnStatus = STORAGE_PROTOCOL_STATUS_PENDING; 
    spt_command->CommandLength = sizeof(NVME_COMMAND); 
    spt_command->DataFromDeviceTransferLength = NVME_LOG_PAGE_SIZE_BYTES;
    spt_command->DataFromDeviceBufferOffset = command_fam_offset + sizeof(NVME_COMMAND);
    spt_command->TimeOutValue = 10; 
    spt_command->ErrorInfoOffset = 0; spt_command->ErrorInfoLength = 0;
    spt_command->DataToDeviceBufferOffset = 0; spt_command->DataToDeviceTransferLength = 0;

    nvme_command->CDW0.OPC = NVME_ADMIN_COMMAND_GET_LOG_PAGE; 
    nvme_command->NSID = NVME_NAMESPACE_ALL;                 
    ULONG cdw10_value = (NVME_LOG_PAGE_HEALTH_INFO & 0xFF) | ((((NVME_LOG_PAGE_SIZE_BYTES / sizeof(DWORD)) - 1) & 0xFFF) << 16);
    ((PULONG)nvme_command)[10] = cdw10_value;


    hDevice = CreateFileA(device_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        method_result->error_code = GetLastError();
        final_pal_status = PAL_STATUS_DEVICE_ERROR; 
        goto complete_and_cleanup_proto;
    }

    DWORD nInBufferSize_for_ioctl = command_fam_offset + sizeof(NVME_COMMAND);
    bResult = DeviceIoControl(hDevice, IOCTL_STORAGE_PROTOCOL_COMMAND, pCommandBuffer, nInBufferSize_for_ioctl, pCommandBuffer, dwCommandBufferSz, &dwBytesReturnedIoctl, NULL);
    method_result->error_code = GetLastError();


    if (bResult) {
        if (spt_command->ReturnStatus == STORAGE_PROTOCOL_STATUS_SUCCESS || spt_command->ReturnStatus == 0) {
            method_result->error_code = 0;
            if (spt_command->DataFromDeviceTransferLength >= NVME_LOG_PAGE_SIZE_BYTES && dwBytesReturnedIoctl >= (command_fam_offset + sizeof(NVME_COMMAND) + NVME_LOG_PAGE_SIZE_BYTES)) {
                memcpy(user_buffer, data_buffer_ptr_in_cmd_buff, NVME_LOG_PAGE_SIZE_BYTES); 
                *bytes_returned = NVME_LOG_PAGE_SIZE_BYTES; 
                final_pal_status = PAL_STATUS_SUCCESS; 
                method_result->success = TRUE;
                    } else {
                final_pal_status = PAL_STATUS_ERROR_DATA_UNDERFLOW; 
                    }
                } else {
            method_result->error_code = spt_command->ReturnStatus;
        }
    } else {
        if (method_result->error_code == ERROR_INVALID_PARAMETER) final_pal_status = PAL_STATUS_INVALID_PARAMETER; 
        else if (method_result->error_code == ERROR_NOT_SUPPORTED) final_pal_status = PAL_STATUS_UNSUPPORTED;
        else if (method_result->error_code == ERROR_ACCESS_DENIED) final_pal_status = PAL_STATUS_ACCESS_DENIED;
        else final_pal_status = PAL_STATUS_IO_ERROR;
    }

complete_and_cleanup_proto:
    GetSystemTimeAsFileTime((FILETIME*)&endTime);
    ULARGE_INTEGER elapsed;
    elapsed.QuadPart = endTime.QuadPart - startTime.QuadPart;
    method_result->execution_time_ms = (DWORD)(elapsed.QuadPart / 10000);

    if (hDevice != INVALID_HANDLE_VALUE) CloseHandle(hDevice);
    if (pCommandBuffer) HeapFree(GetProcessHeap(), 0, pCommandBuffer);
    
    return final_pal_status;
}

pal_status_t pal_get_smart_data_nvme_hybrid(
    const char* device_path, 
    nvme_hybrid_context_t* context,
    BYTE* user_buffer,             
    DWORD user_buffer_size,        
    DWORD* bytes_returned,         
    nvme_access_result_t* overall_result 
) {
    if (!device_path || !context || !user_buffer || !bytes_returned || !overall_result) {
        return PAL_STATUS_INVALID_PARAMETER;
    }

    ZeroMemory(overall_result, sizeof(nvme_access_result_t));
    *bytes_returned = 0;
    pal_status_t final_pal_status_for_function = PAL_STATUS_ERROR; 

    BYTE raw_log_buffer_for_current_method[NVME_LOG_PAGE_SIZE_BYTES];
    DWORD current_method_bytes_returned = 0;
    pal_status_t current_method_status = PAL_STATUS_ERROR;
    nvme_access_result_t temp_method_result;
    bool first_successful_method_found = false;
    DWORD last_windows_error = 0;

    if (!first_successful_method_found && context->try_query_property) {
        ZeroMemory(&temp_method_result, sizeof(nvme_access_result_t));
        StringCchCopyA(temp_method_result.method_name, sizeof(temp_method_result.method_name), NVME_METHOD_NAME_QUERY_PROPERTY);
        temp_method_result.method_used = NVME_ACCESS_METHOD_QUERY_PROPERTY;
        
        current_method_status = pal_windows_get_nvme_smart_log_via_query_prop_direct(
            device_path, context, raw_log_buffer_for_current_method, 
            sizeof(raw_log_buffer_for_current_method), &current_method_bytes_returned, &temp_method_result
        );
        last_windows_error = temp_method_result.error_code;

        if (current_method_status == PAL_STATUS_SUCCESS && current_method_bytes_returned >= sizeof(NVME_HEALTH_INFO_LOG)) {
            memcpy(user_buffer, raw_log_buffer_for_current_method, current_method_bytes_returned);
            *bytes_returned = current_method_bytes_returned;
            final_pal_status_for_function = PAL_STATUS_SUCCESS;
            *overall_result = temp_method_result; 
            first_successful_method_found = true;
            if (!context->benchmark_mode && first_successful_method_found) goto end_method_loop;
        } else {
            if (overall_result->method_used == NVME_ACCESS_METHOD_NONE) { *overall_result = temp_method_result; }
        }
    }
    // Attempt IOCTL_STORAGE_PROTOCOL_COMMAND (Direct)
    if (!first_successful_method_found && context->try_protocol_command) {
        ZeroMemory(&temp_method_result, sizeof(nvme_access_result_t));
        StringCchCopyA(temp_method_result.method_name, sizeof(temp_method_result.method_name), NVME_METHOD_NAME_PROTOCOL_COMMAND);
        temp_method_result.method_used = NVME_ACCESS_METHOD_PROTOCOL_COMMAND;

        current_method_status = pal_windows_get_nvme_smart_log_via_protocol_cmd_direct(
            device_path, context, raw_log_buffer_for_current_method, 
            sizeof(raw_log_buffer_for_current_method), &current_method_bytes_returned, &temp_method_result
        );
        last_windows_error = temp_method_result.error_code;

        if (current_method_status == PAL_STATUS_SUCCESS && current_method_bytes_returned >= sizeof(NVME_HEALTH_INFO_LOG)) {
            memcpy(user_buffer, raw_log_buffer_for_current_method, current_method_bytes_returned);
            *bytes_returned = current_method_bytes_returned;
            final_pal_status_for_function = PAL_STATUS_SUCCESS;
            *overall_result = temp_method_result; 
            first_successful_method_found = true;
            if (!context->benchmark_mode && first_successful_method_found) goto end_method_loop;
        } else {
            if (overall_result->method_used == NVME_ACCESS_METHOD_NONE || !overall_result->success) { *overall_result = temp_method_result; }
        }
    }

end_method_loop:
    if (final_pal_status_for_function != PAL_STATUS_SUCCESS) {
        if (overall_result->method_used == NVME_ACCESS_METHOD_NONE || overall_result->error_code == 0) { 
            if (overall_result->error_code == 0) overall_result->error_code = (DWORD)final_pal_status_for_function; 
            overall_result->success = FALSE;
        }
    }
    
    return final_pal_status_for_function;
}
// Implementação de trim_trailing_spaces
static void trim_trailing_spaces(char *str) {
    if (str == NULL) return;
    size_t i = strlen(str) - 1;
    while (i >= 0 && isspace((unsigned char)str[i])) {
        str[i] = '\0';
        i--;
    }
}
static bool get_device_path_from_instance_id(const char* instance_id, char* device_path_buffer, size_t buffer_size) {
    if (!instance_id || !device_path_buffer || buffer_size == 0) {
        return false;
    }
    *device_path_buffer = '\0'; 

    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    DWORD i;
    bool found_match = false;

    for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); i++) {
        char current_instance_id[MAX_DEVICE_ID_LEN];
        if (CM_Get_Device_ID_ExA(deviceInfoData.DevInst, current_instance_id, MAX_DEVICE_ID_LEN, 0, NULL) == CR_SUCCESS) {
            if (_stricmp(instance_id, current_instance_id) == 0) {
                found_match = true;
                break;
            }
        }
    }

    if (!found_match) {
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }
    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    if (!SetupDiEnumDeviceInterfaces(hDevInfo, &deviceInfoData, &GUID_DEVINTERFACE_DISK, 0, &deviceInterfaceData)) {
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    DWORD required_size = 0;
    SetupDiGetDeviceInterfaceDetailA(hDevInfo, &deviceInterfaceData, NULL, 0, &required_size, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A deviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required_size);
    if (!deviceInterfaceDetailData) {
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }
    deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A); // Critical: Set cbSize for the structure itself, not the allocated size

    if (!SetupDiGetDeviceInterfaceDetailA(hDevInfo, &deviceInterfaceData, deviceInterfaceDetailData, required_size, NULL, NULL)) {
        HeapFree(GetProcessHeap(), 0, deviceInterfaceDetailData);
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    HRESULT hr = StringCchCopyA(device_path_buffer, buffer_size, deviceInterfaceDetailData->DevicePath);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, deviceInterfaceDetailData);
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }
    
    HeapFree(GetProcessHeap(), 0, deviceInterfaceDetailData);
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return true;
}
pal_status_t pal_list_drives(DriveInfo *drive_list, int max_drives, int *drive_count) {
    if (!drive_list || !drive_count || max_drives <= 0) {
        return PAL_STATUS_INVALID_PARAMETER;
    }

    *drive_count = 0;
    char device_path_buffer[MAX_PATH];
    BasicDriveInfo basic_info;
    HANDLE hDevice;

    for (int i = 0; i < 16; ++i) { // Check for up to 16 physical drives
        if (*drive_count >= max_drives) {
            break;
        }
        sprintf_s(device_path_buffer, sizeof(device_path_buffer), "\\\\.\\PhysicalDrive%d", i);
                              hDevice = CreateFileA(device_path_buffer,
                              0, // No access needed, just checking existence
                              FILE_SHARE_READ | FILE_SHARE_WRITE, // Share mode
                              NULL,             // Default security attributes
                              OPEN_EXISTING,    // Opens a file or device, only if it exists
                              0,                // Flags and attributes
                              NULL);            // No template file

        if (hDevice == INVALID_HANDLE_VALUE) {
            continue;
        }
        CloseHandle(hDevice); 
        pal_status_t basic_info_status = pal_get_basic_drive_info(device_path_buffer, &basic_info);
        if (basic_info_status == PAL_STATUS_SUCCESS) {
            drive_list[*drive_count].size_bytes = pal_get_device_size(device_path_buffer);

            if (drive_list[*drive_count].size_bytes < 0) {
                continue;
            }
            
            strncpy_s(drive_list[*drive_count].device_path, sizeof(drive_list[*drive_count].device_path), device_path_buffer, sizeof(drive_list[*drive_count].device_path) - 1);
            drive_list[*drive_count].device_path[sizeof(drive_list[*drive_count].device_path) - 1] = '\0';
            strncpy_s(drive_list[*drive_count].model, sizeof(drive_list[*drive_count].model), basic_info.model, _TRUNCATE);
            drive_list[*drive_count].model[sizeof(drive_list[*drive_count].model) - 1] = '\0';
            strncpy_s(drive_list[*drive_count].serial, sizeof(drive_list[*drive_count].serial), basic_info.serial, _TRUNCATE);
            drive_list[*drive_count].serial[sizeof(drive_list[*drive_count].serial) - 1] = '\0';        
            strncpy_s(drive_list[*drive_count].type, sizeof(drive_list[*drive_count].type), basic_info.type, _TRUNCATE);
            drive_list[*drive_count].type[sizeof(drive_list[*drive_count].type) - 1] = '\0';
            (*drive_count)++;
        }
    }
    return PAL_STATUS_SUCCESS;
}

pal_status_t pal_get_terminal_size(int* width, int* height) {
    if (width == NULL || height == NULL) {
        return PAL_STATUS_INVALID_PARAMETER;
    }
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return PAL_STATUS_SUCCESS;
    }
    return PAL_STATUS_ERROR;
}

// =================================================================================
// TUI Utility Functions Implementation
// =================================================================================

void pal_clear_screen(void) {
    system("cls");
}

#include <conio.h>
void pal_wait_for_keypress(void) {
    printf("\nPress Enter to continue...\n");
    while (_getch() != '\r');
}

int pal_get_char_input(void) {
    return _getch();
}

bool pal_get_string_input(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return false;
    }
    
    if (fgets(buffer, (int)buffer_size, stdin) != NULL) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        return true;
    }
    return false;
}

#endif // _WIN32

