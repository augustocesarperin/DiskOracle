#include "pal.h"
#include <stdio.h>
#include <string.h> 
#include <ctype.h>  

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <devioctl.h>

static void trim_trailing_spaces(char *str) {
    if (str == NULL) return;
    int i = strlen(str) - 1;
    while (i >= 0 && isspace((unsigned char)str[i])) {
        str[i] = '\0';
        i--;
    }
}

int pal_list_drives() {
    printf("Available physical drives (Windows):\n");
    printf("%-20s | %-30s | %-25s | %-15s | %s\n", "Device Path", "Model", "Serial Number", "Size (GB)", "Bus Type");
    printf("------------------------------------------------------------------------------------------------------------------\n");

    char device_path_format[] = "\\\\.\\PhysicalDrive%d";
    char device_path[32];
    BOOL found_any = FALSE;

    for (int i = 0; i < 16; ++i) {
        snprintf(device_path, sizeof(device_path), device_path_format, i);

        HANDLE hDevice = CreateFileA(device_path,
                                   0,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL,
                                   OPEN_EXISTING,
                                   0,
                                   NULL);

        if (hDevice == INVALID_HANDLE_VALUE) {
            continue;
        }

        STORAGE_PROPERTY_QUERY query;
        memset(&query, 0, sizeof(query));
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;

        BYTE buffer[2048];
        memset(buffer, 0, sizeof(buffer));
        DWORD bytes_returned = 0;

        BOOL success = DeviceIoControl(hDevice,
                                     IOCTL_STORAGE_QUERY_PROPERTY,
                                     &query,
                                     sizeof(query),
                                     buffer,
                                     sizeof(buffer),
                                     &bytes_returned,
                                     NULL);
        CloseHandle(hDevice);

        if (success && bytes_returned > 0) {
            STORAGE_DEVICE_DESCRIPTOR *desc = (STORAGE_DEVICE_DESCRIPTOR *)buffer;
            found_any = TRUE;

            char model[128] = "N/A";
            char serial[128] = "N/A";

            if (desc->ProductIdOffset > 0 && desc->ProductIdOffset < sizeof(buffer)) {
                strncpy(model, (char*)buffer + desc->ProductIdOffset, sizeof(model) - 1);
                model[sizeof(model)-1] = '\0';
                trim_trailing_spaces(model);
            }
            
            if (desc->SerialNumberOffset > 0 && desc->SerialNumberOffset < sizeof(buffer)) {
                strncpy(serial, (char*)buffer + desc->SerialNumberOffset, sizeof(serial) - 1);
                serial[sizeof(serial)-1] = '\0';
                trim_trailing_spaces(serial);
            }

            int64_t size_bytes = pal_get_device_size(device_path);
            double size_gb = -1.0;
            if (size_bytes > 0) {
                size_gb = (double)size_bytes / (1024.0 * 1024.0 * 1024.0);
            }

            const char* bus_type_str = "Unknown";
            switch (desc->BusType) {
                case BusTypeSata: bus_type_str = "SATA"; break;
                case BusTypeScsi: bus_type_str = "SCSI"; break;
                case BusTypeAtapi: bus_type_str = "ATAPI"; break;
                case BusTypeAta: bus_type_str = "ATA"; break;
                case BusTypeUsb: bus_type_str = "USB"; break;
                case BusType1394: bus_type_str = "1394"; break;
                case BusTypeSsa: bus_type_str = "SSA"; break;
                case BusTypeFibre: bus_type_str = "Fibre Channel"; break;
                case BusTypeRAID: bus_type_str = "RAID"; break;
                case BusTypeNvme: bus_type_str = "NVMe"; break;
                case BusTypeSd: bus_type_str = "SD"; break;
                case BusTypeMmc: bus_type_str = "MMC"; break;
                case BusTypeVirtual: bus_type_str = "Virtual"; break;
                case BusTypeFileBackedVirtual: bus_type_str = "File Backed Virtual"; break;
                default: bus_type_str = "Other"; break;
            }
            if (size_gb > 0.0) {
                 printf("%-20s | %-30s | %-25s | %-15.2f | %s\n", device_path, model, serial, size_gb, bus_type_str);
            } else {
                 printf("%-20s | %-30s | %-25s | %-15s | %s\n", device_path, model, serial, "N/A", bus_type_str);
            }
        }
    }
    if (!found_any) {
        printf("No physical drives found or accessible.\n");
    }
    return 0;
}

#include "smart.h"

typedef struct _ATA_PASS_THROUGH_EX_WITH_BUFFER {
    ATA_PASS_THROUGH_EX apt;
    ULONG Filler; 
    UCHAR DataBuf[512];
} ATA_PASS_THROUGH_EX_WITH_BUFFER;

int pal_get_smart_data(const char *device_path, struct smart_data *out) {
    if (!device_path || !out) {
        fprintf(stderr, "pal_get_smart_data: Invalid parameters.\n");
        return 1;
    }
    memset(out, 0, sizeof(struct smart_data));

    HANDLE hDevice = CreateFileA(device_path,
                               GENERIC_READ | GENERIC_WRITE, 
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               0,
                               NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "pal_get_smart_data: Failed to open device %s. Error: %lu\n", device_path, GetLastError());
        return 1;
    }

    STORAGE_PROPERTY_QUERY query_desc;
    STORAGE_DEVICE_DESCRIPTOR dev_desc_buffer = {0};
    DWORD bytesReturned_desc;

    memset(&query_desc, 0, sizeof(query_desc));
    query_desc.PropertyId = StorageDeviceProperty;
    query_desc.QueryType = PropertyStandardQuery;

    BOOL is_nvme_drive = FALSE;
    if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
                        &query_desc, sizeof(query_desc),
                        &dev_desc_buffer, sizeof(dev_desc_buffer),
                        &bytesReturned_desc, NULL) && bytesReturned_desc >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        if (dev_desc_buffer.BusType == BusTypeNvme) {
            is_nvme_drive = TRUE;
        }
    }

    if (is_nvme_drive) {
        out->is_nvme = 1;
        fprintf(stderr, "pal_get_smart_data: NVMe drive detected (%s).\n", device_path);

        UCHAR buffer[sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + sizeof(NVME_HEALTH_INFO_LOG) + 4096]; 
        STORAGE_PROTOCOL_SPECIFIC_DATA *protocol_data = (STORAGE_PROTOCOL_SPECIFIC_DATA *)buffer;
        DWORD returned_length;

        memset(buffer, 0, sizeof(buffer));

        protocol_data->ProtocolType = ProtocolTypeNvme;
        protocol_data->DataType = StorageDataTypeLogPage; 
        protocol_data->ProtocolDataRequestValue = NVME_LOG_PAGE_HEALTH_INFO; 
        protocol_data->ProtocolDataRequestSubValue = 0; 
        protocol_data->ProtocolDataOffset = 0;
        protocol_data->ProtocolDataLength = sizeof(NVME_HEALTH_INFO_LOG);
        protocol_data->FixedProtocolReturnData = 0;
        protocol_data->ProtocolDataRequestSubValue2 = 0;
        protocol_data->ProtocolDataRequestSubValue3 = 0;
        protocol_data->ProtocolDataRequestSubValue4 = 0;


        if (!DeviceIoControl(hDevice,
                             IOCTL_STORAGE_QUERY_PROPERTY,
                             protocol_data, 
                             sizeof(buffer),  
                             buffer,          
                             sizeof(buffer),  
                             &returned_length,
                             NULL) || returned_length < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + sizeof(NVME_HEALTH_INFO_LOG)) {
            fprintf(stderr, "pal_get_smart_data: Failed to get NVMe Health Info Log for %s. Error: %lu, Returned Length: %lu\n", device_path, GetLastError(), returned_length);
            CloseHandle(hDevice);
            return 2; 
        }

        NVME_HEALTH_INFO_LOG *health_log = (NVME_HEALTH_INFO_LOG *)((PCHAR)protocol_data + protocol_data->ProtocolDataOffset);

        out->nvme.critical_warning = health_log->CriticalWarning.AsUchar;
        out->nvme.temperature = (health_log->Temperature[1] << 8) | health_log->Temperature[0]; 
        out->nvme.avail_spare = health_log->AvailableSpare;
        out->nvme.spare_thresh = health_log->AvailableSpareThreshold;
        out->nvme.percent_used = health_log->PercentageUsed;
        memcpy(out->nvme.data_units_read, health_log->DataUnitRead, sizeof(out->nvme.data_units_read));
        memcpy(out->nvme.data_units_written, health_log->DataUnitWritten, sizeof(out->nvme.data_units_written));
        memcpy(out->nvme.host_reads, health_log->HostReadCommands, sizeof(out->nvme.host_reads));
        memcpy(out->nvme.host_writes, health_log->HostWriteCommands, sizeof(out->nvme.host_writes));
        memcpy(out->nvme.controller_busy_time, health_log->ControllerBusyTime, sizeof(out->nvme.controller_busy_time));
        memcpy(out->nvme.power_cycles, health_log->PowerCycle, sizeof(out->nvme.power_cycles));
        memcpy(out->nvme.power_on_hours, health_log->PowerOnHours, sizeof(out->nvme.power_on_hours));
        memcpy(out->nvme.unsafe_shutdowns, health_log->UnsafeShutdowns, sizeof(out->nvme.unsafe_shutdowns));
        memcpy(out->nvme.media_errors, health_log->MediaError, sizeof(out->nvme.media_errors));
        memcpy(out->nvme.num_err_log_entries, health_log->ErrorLogEntryCount, sizeof(out->nvme.num_err_log_entries));
        

    } else {
        out->is_nvme = 0;
        ATA_PASS_THROUGH_EX_WITH_BUFFER apt_buffer;
        DWORD bytesReturned_ata;

        memset(&apt_buffer, 0, sizeof(apt_buffer));
        apt_buffer.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
        apt_buffer.apt.AtaFlags = ATA_FLAGS_DATA_IN;
        apt_buffer.apt.DataTransferLength = 512;
        apt_buffer.apt.TimeOutValue = 10; 
        apt_buffer.apt.DataBufferOffset = offsetof(ATA_PASS_THROUGH_EX_WITH_BUFFER, DataBuf);
        apt_buffer.apt.CurrentTaskFile[0] = 0;    
        apt_buffer.apt.CurrentTaskFile[1] = 1;    
        apt_buffer.apt.CurrentTaskFile[2] = 1;    
        apt_buffer.apt.CurrentTaskFile[3] = 0x4F; 
        apt_buffer.apt.CurrentTaskFile[4] = 0xC2; 
        apt_buffer.apt.CurrentTaskFile[5] = 0;    
        apt_buffer.apt.CurrentTaskFile[6] = 0xB0; 
        apt_buffer.apt.CurrentTaskFile[7] = 0xD0; 

        if (!DeviceIoControl(hDevice, IOCTL_ATA_PASS_THROUGH,
                             &apt_buffer, sizeof(apt_buffer),
                             &apt_buffer, sizeof(apt_buffer),
                             &bytesReturned_ata, NULL) || bytesReturned_ata == 0) {
            fprintf(stderr, "pal_get_smart_data: SMART READ DATA command failed for %s. Error: %lu\n", device_path, GetLastError());
            CloseHandle(hDevice);
            return 1;
        }

        out->attr_count = 0;
        for (int i = 2; (i + 11 < 512) && (out->attr_count < MAX_SMART_ATTRIBUTES); i += 12) {
            uint8_t id = apt_buffer.DataBuf[i];
            if (id == 0) continue; 

            out->attrs[out->attr_count].id = id;
            out->attrs[out->attr_count].flags = (apt_buffer.DataBuf[i+2] << 8) | apt_buffer.DataBuf[i+1];
            out->attrs[out->attr_count].value = apt_buffer.DataBuf[i+3];
            out->attrs[out->attr_count].worst = apt_buffer.DataBuf[i+4];
            memcpy(out->attrs[out->attr_count].raw, &apt_buffer.DataBuf[i+5], 6);
            out->attrs[out->attr_count].threshold = 0; 
            out->attr_count++;
        }

        memset(&apt_buffer, 0, sizeof(apt_buffer));
        apt_buffer.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
        apt_buffer.apt.AtaFlags = ATA_FLAGS_DATA_IN;
        apt_buffer.apt.DataTransferLength = 512;
        apt_buffer.apt.TimeOutValue = 10;
        apt_buffer.apt.DataBufferOffset = offsetof(ATA_PASS_THROUGH_EX_WITH_BUFFER, DataBuf);
        apt_buffer.apt.CurrentTaskFile[0] = 0;
        apt_buffer.apt.CurrentTaskFile[1] = 1;
        apt_buffer.apt.CurrentTaskFile[2] = 1;
        apt_buffer.apt.CurrentTaskFile[3] = 0x4F;
        apt_buffer.apt.CurrentTaskFile[4] = 0xC2;
        apt_buffer.apt.CurrentTaskFile[5] = 0;
        apt_buffer.apt.CurrentTaskFile[6] = 0xB0; 
        apt_buffer.apt.CurrentTaskFile[7] = 0xD1; 

        if (DeviceIoControl(hDevice, IOCTL_ATA_PASS_THROUGH,
                            &apt_buffer, sizeof(apt_buffer),
                            &apt_buffer, sizeof(apt_buffer),
                            &bytesReturned_ata, NULL) && bytesReturned_ata > 0) {
            for (int i = 0; i < out->attr_count; ++i) { 
                for (int j = 2; (j + 1 < 512) ; j += 12) { 
                    uint8_t thresh_id = apt_buffer.DataBuf[j];
                    if (thresh_id == 0) continue;
                    if (out->attrs[i].id == thresh_id) {
                        out->attrs[i].threshold = apt_buffer.DataBuf[j+1];
                        break; 
                    }
                }
            }
        } else {
            fprintf(stderr, "pal_get_smart_data: SMART READ THRESHOLDS command failed for %s. Error: %lu\n", device_path, GetLastError());
        }
    }

    CloseHandle(hDevice);
    return 0; 
}

int64_t pal_get_device_size(const char *device_path) {
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
    DWORD bytes_returned;

    BOOL success = DeviceIoControl(hDevice,
                                 IOCTL_DISK_GET_LENGTH_INFO,
                                 NULL,
                                 0,
                                 &length_info,
                                 sizeof(GET_LENGTH_INFORMATION),
                                 &bytes_returned,
                                 NULL);

    CloseHandle(hDevice);

    if (!success || bytes_returned == 0) {
        return -1;
    }

    return length_info.Length.QuadPart;
}

bool pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info) {
    if (!device_path || !info) {
        return false;
    }

    memset(info, 0, sizeof(BasicDriveInfo));
    strncpy(info->model, "Unknown", sizeof(info->model) - 1);
    strncpy(info->serial, "Unknown", sizeof(info->serial) - 1);
    strncpy(info->type, "Unknown", sizeof(info->type) - 1);
    strncpy(info->bus_type, "Unknown", sizeof(info->bus_type) - 1);

    HANDLE hDevice = CreateFileA(device_path,
                               0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               0,
                               NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return false;
    }

    STORAGE_PROPERTY_QUERY query_desc;
    memset(&query_desc, 0, sizeof(query_desc));
    query_desc.PropertyId = StorageDeviceProperty;
    query_desc.QueryType = PropertyStandardQuery;

    BYTE buffer_desc[2048];
    memset(buffer_desc, 0, sizeof(buffer_desc));
    DWORD bytes_returned_desc = 0;

    BOOL success_desc = DeviceIoControl(hDevice,
                                 IOCTL_STORAGE_QUERY_PROPERTY,
                                 &query_desc,
                                 sizeof(query_desc),
                                 buffer_desc,
                                 sizeof(buffer_desc),
                                 &bytes_returned_desc,
                                 NULL);

    if (success_desc && bytes_returned_desc > 0) {
        STORAGE_DEVICE_DESCRIPTOR *desc = (STORAGE_DEVICE_DESCRIPTOR *)buffer_desc;

        if (desc->ProductIdOffset > 0 && desc->ProductIdOffset < sizeof(buffer_desc)) {
            strncpy(info->model, (char*)buffer_desc + desc->ProductIdOffset, sizeof(info->model) - 1);
            info->model[sizeof(info->model)-1] = '\0';
            trim_trailing_spaces(info->model);
        }
        
        if (desc->SerialNumberOffset > 0 && desc->SerialNumberOffset < sizeof(buffer_desc)) {
            strncpy(info->serial, (char*)buffer_desc + desc->SerialNumberOffset, sizeof(info->serial) - 1);
            info->serial[sizeof(info->serial)-1] = '\0';
            trim_trailing_spaces(info->serial);
        }

        switch (desc->BusType) {
            case BusTypeSata: strncpy(info->bus_type, "SATA", sizeof(info->bus_type) - 1); break;
            case BusTypeScsi: strncpy(info->bus_type, "SCSI", sizeof(info->bus_type) - 1); break;
            case BusTypeAtapi: strncpy(info->bus_type, "ATAPI", sizeof(info->bus_type) - 1); break;
            case BusTypeAta: strncpy(info->bus_type, "ATA", sizeof(info->bus_type) - 1); break;
            case BusTypeUsb: strncpy(info->bus_type, "USB", sizeof(info->bus_type) - 1); break;
            case BusType1394: strncpy(info->bus_type, "1394", sizeof(info->bus_type) - 1); break;
            case BusTypeSsa: strncpy(info->bus_type, "SSA", sizeof(info->bus_type) - 1); break;
            case BusTypeFibre: strncpy(info->bus_type, "Fibre Channel", sizeof(info->bus_type) - 1); break;
            case BusTypeRAID: strncpy(info->bus_type, "RAID", sizeof(info->bus_type) - 1); break;
            case BusTypeNvme: strncpy(info->bus_type, "NVMe", sizeof(info->bus_type) - 1); break;
            case BusTypeSd: strncpy(info->bus_type, "SD", sizeof(info->bus_type) - 1); break;
            case BusTypeMmc: strncpy(info->bus_type, "MMC", sizeof(info->bus_type) - 1); break;
            case BusTypeVirtual: strncpy(info->bus_type, "Virtual", sizeof(info->bus_type) - 1); break;
            case BusTypeFileBackedVirtual: strncpy(info->bus_type, "File Backed Virtual", sizeof(info->bus_type) - 1); break;
            default: strncpy(info->bus_type, "Other", sizeof(info->bus_type) - 1); break;
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
                              NULL) && bytes_returned_penalty > 0) {
            if (penalty_desc.IncursSeekPenalty) {
                strncpy(info->type, "HDD", sizeof(info->type) - 1);
            } else {
                 if (desc->BusType == BusTypeNvme) {
                     strncpy(info->type, "NVMe", sizeof(info->type) - 1);
                } else {
                     strncpy(info->type, "SSD", sizeof(info->type) - 1);
                }
            }
        } else {
             if (desc->BusType == BusTypeNvme) {
                 strncpy(info->type, "NVMe", sizeof(info->type) - 1);
            } else {
                 strncpy(info->type, "Unknown (SeekTypeFail)", sizeof(info->type) -1);
            }
        }
        info->type[sizeof(info->type)-1] = '\0';

    } else {
        CloseHandle(hDevice);
        return false;
    }
    
    CloseHandle(hDevice);
    return true;
}

#else 
int pal_list_drives() { printf("PAL Windows not available on this OS for physical drive listing.\n"); return 1; }
int pal_get_smart(const char *device_path, struct smart_data *out) { (void)device_path; (void)out; return 1; }

int64_t pal_get_device_size(const char *device_path) {
    (void)device_path; 
    return -1;
}

bool pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info) {
    (void)device_path; 
    if (info) {
        memset(info, 0, sizeof(BasicDriveInfo));
        strncpy(info->model, "Not Implemented", sizeof(info->model) - 1);
        strncpy(info->serial, "Not Implemented", sizeof(info->serial) - 1);
        strncpy(info->type, "Not Implemented", sizeof(info->type) - 1);
        strncpy(info->bus_type, "Not Implemented", sizeof(info->bus_type) - 1);
    }
    return false;
}

#endif
