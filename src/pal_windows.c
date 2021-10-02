#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Target Windows 10. Adjust if a different minimum is required.
#endif
#include "pal.h"
#include "logging.h"
#include "smart.h"
#include "info.h"
#include "project_config.h"
#include "nvme_hybrid.h"
#include "smart_ata.h" // Incluir o cabeçalho para smart_read_ata
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
#include <ntddscsi.h> // Required for ATA pass-through, IOCTL_ATA_PASS_THROUGH, ATA_PASS_THROUGH_EX
#include <nvme.h>     // For NVME_COMMAND and related definitions, and STORAGE_PROTOCOL_SPECIFIC_DATA related enums
#include <devguid.h>  // For GUID_DEVINTERFACE_DISK (declaration or definition based on initguid.h)
#include <cfgmgr32.h> // Added for CM_Get_Device_ID_ExA, MAX_DEVICE_ID_LEN, CR_SUCCESS

// Explicitly define the storage for GUID_DEVINTERFACE_DISK using its direct name.
#ifdef __cplusplus
extern "C" {
#endif
const GUID GUID_DEVINTERFACE_DISK = 
    { 0x53f56307L, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } };
#ifdef __cplusplus
}
#endif

// Define the standard size for NVMe SMART/Health Information Log Page (LID 02h)
#define NVME_LOG_PAGE_SIZE_BYTES 512

#ifndef NVME_LOG_PAGE_HEALTH_INFO
    #define NVME_LOG_PAGE_HEALTH_INFO 0x02 // Log Page Identifier for SMART/Health Information
#endif

#ifndef NVME_ADMIN_COMMAND_GET_LOG_PAGE
    #define NVME_ADMIN_COMMAND_GET_LOG_PAGE 0x02
#endif

#ifndef NVME_NAMESPACE_ALL
    #define NVME_NAMESPACE_ALL 0xFFFFFFFF
#endif

/* NVME_PSDT_PRP_VALUE is not used by IOCTL_STORAGE_QUERY_PROPERTY approach
#ifndef NVME_PSDT_PRP_VALUE
    #define NVME_PSDT_PRP_VALUE 0x00 
#endif
*/

/* Old SRB related structures and defines are removed
#define NVME_PASS_THROUGH_CONTROL_CODE 0x00CF0800 
#define NVME_SRB_SIGNATURE "NVMEPASS" 

#pragma pack(push, 1) 
typedef struct _NVME_SRB_ADMIN_PASSTHROUGH_WITH_DATA {
    SRB_IO_CONTROL SrbIoCtrl; 
    NVME_COMMAND NvmeCmd;     
    BYTE DataBuffer[NVME_LOG_PAGE_SIZE_BYTES]; 
} NVME_SRB_ADMIN_PASSTHROUGH_WITH_DATA, *PNVME_SRB_ADMIN_PASSTHROUGH_WITH_DATA;
#pragma pack(pop)
*/

// Forward declaration for the NVMe-specific SMART data retrieval function
static int pal_get_smart_data_nvme_query_prop(
    const char* device_path, 
    const nvme_hybrid_context_t* context,
    BYTE* user_buffer, 
    DWORD user_buffer_size, 
    DWORD* bytes_returned,
    nvme_access_result_t* method_result
);
static int pal_get_smart_data_nvme_protocol_cmd(
    const char* device_path,
    const nvme_hybrid_context_t* context,
    BYTE* user_buffer,
    DWORD user_buffer_size,
    DWORD* bytes_returned,
    nvme_access_result_t* method_result
);
// Forward declaration for the hybrid orchestrator
static int pal_get_smart_data_nvme_hybrid(
    const char* device_path, 
    nvme_hybrid_context_t* context, 
    BYTE* user_buffer, 
    DWORD user_buffer_size, 
    DWORD* bytes_returned,
    nvme_access_result_t* overall_result
);

static bool get_device_path_from_instance_id(const char* instance_id, char* device_path_buffer, size_t buffer_size);

static void trim_trailing_spaces(char *str) {
    if (str == NULL) return;
    int i = strlen(str) - 1;
    while (i >= 0 && isspace((unsigned char)str[i])) {
        str[i] = '\0';
        i--;
    }
}

// New SetupAPI helper function to get a usable device path from a PNPDeviceID (Device Instance ID)
static bool get_device_path_from_instance_id(const char* instance_id, char* device_path_buffer, size_t buffer_size) {
    if (!instance_id || !device_path_buffer || buffer_size == 0) {
        fprintf(stderr, "[DEBUG SETUPAPI] get_device_path_from_instance_id: Invalid parameters.\n");
        return false;
    }
    *device_path_buffer = '\0'; 

    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[DEBUG SETUPAPI] SetupDiGetClassDevsA failed. Error: %lu\n", GetLastError());
        return false;
    }

    fprintf(stderr, "[DEBUG SETUPAPI] Searching for instance ID: %s\n", instance_id);

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    DWORD i;
    bool found_match = false;

    for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); i++) {
        char current_instance_id[MAX_DEVICE_ID_LEN];
        // Using CM_Get_Device_ID_ExA as SetupDiGetDeviceInstanceId can be problematic with buffer sizes
        if (CM_Get_Device_ID_ExA(deviceInfoData.DevInst, current_instance_id, MAX_DEVICE_ID_LEN, 0, NULL) == CR_SUCCESS) {
             fprintf(stderr, "[DEBUG SETUPAPI] Checking device (DevInst: %p): %s\n", (void*)deviceInfoData.DevInst, current_instance_id);
            if (_stricmp(instance_id, current_instance_id) == 0) {
                fprintf(stderr, "[DEBUG SETUPAPI] Found matching device instance ID: %s\n", current_instance_id);
                found_match = true;
                break; // Found the device, deviceInfoData now points to our target device
            }
        } else {
            // This can happen for some devices if the property is not available, not necessarily a fatal error for the loop.
            fprintf(stderr, "[DEBUG SETUPAPI] CM_Get_Device_ID_ExA failed for device index %lu. This might be normal for some enumerated devices.\n", i);
        }
    }

    if (!found_match) {
        fprintf(stderr, "[DEBUG SETUPAPI] Device with instance ID '%s' not found after checking %lu devices.\n", instance_id, i);
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    // Now get the device interface path for the matched device (pointed to by deviceInfoData)
    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // We use the deviceInfoData from the loop that matched the instance_id
    if (!SetupDiEnumDeviceInterfaces(hDevInfo, &deviceInfoData, &GUID_DEVINTERFACE_DISK, 0, &deviceInterfaceData)) {
        fprintf(stderr, "[DEBUG SETUPAPI] SetupDiEnumDeviceInterfaces failed for instance ID %s. Error: %lu\n", instance_id, GetLastError());
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }
    fprintf(stderr, "[DEBUG SETUPAPI] SetupDiEnumDeviceInterfaces succeeded for %s.\n", instance_id);

    // Get the required buffer size for the detail data
    DWORD required_size = 0;
    // First call to get the size of the buffer
    SetupDiGetDeviceInterfaceDetailA(hDevInfo, &deviceInterfaceData, NULL, 0, &required_size, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        fprintf(stderr, "[DEBUG SETUPAPI] SetupDiGetDeviceInterfaceDetailA (to get size) failed unexpectedly for %s. Error: %lu\n", instance_id, GetLastError());
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }
    fprintf(stderr, "[DEBUG SETUPAPI] Required size for detail data for %s: %lu bytes.\n", instance_id, required_size);

    PSP_DEVICE_INTERFACE_DETAIL_DATA_A deviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required_size);
    if (!deviceInterfaceDetailData) {
        fprintf(stderr, "[DEBUG SETUPAPI] HeapAlloc failed for SP_DEVICE_INTERFACE_DETAIL_DATA_A for %s. Error: %lu\n", instance_id, GetLastError());
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }
    deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A); // Critical: Set cbSize for the structure itself, not the allocated size

    if (!SetupDiGetDeviceInterfaceDetailA(hDevInfo, &deviceInterfaceData, deviceInterfaceDetailData, required_size, NULL, NULL)) {
        fprintf(stderr, "[DEBUG SETUPAPI] SetupDiGetDeviceInterfaceDetailA (to get data) failed for %s. Error: %lu\n", instance_id, GetLastError());
        HeapFree(GetProcessHeap(), 0, deviceInterfaceDetailData);
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    HRESULT hr = StringCchCopyA(device_path_buffer, buffer_size, deviceInterfaceDetailData->DevicePath);
    if (FAILED(hr)) {
        fprintf(stderr, "[DEBUG SETUPAPI] StringCchCopyA failed to copy device path for %s. HRESULT: 0x%08lX\n", instance_id, hr);
        HeapFree(GetProcessHeap(), 0, deviceInterfaceDetailData);
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    fprintf(stderr, "[DEBUG SETUPAPI] Successfully retrieved device path for %s: %s\n", instance_id, device_path_buffer);

    HeapFree(GetProcessHeap(), 0, deviceInterfaceDetailData);
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return true;
}

int pal_list_drives() {
    printf("Available physical drives (Windows):\n");
    printf("%-70s | %-30s | %-25s | %-15s | %s\n", "Device Path", "Model", "Serial Number", "Size (GB)", "Bus Type");
    printf("-----------------------------------------------------------------------------------------------------------------------------------------------------\n");

    BOOL overall_found_any = FALSE;

    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[PAL_LIST_DRIVES_ERROR] SetupDiGetClassDevsA failed. Error: %lu\n", GetLastError());
        printf("Could not enumerate disk devices (SetupDiGetClassDevsA failed).\n");
        return PAL_STATUS_ERROR; 
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    DWORD device_index;

    for (device_index = 0; SetupDiEnumDeviceInfo(hDevInfo, device_index, &deviceInfoData); device_index++) {
        char pnp_instance_id[MAX_DEVICE_ID_LEN];
        if (CM_Get_Device_ID_ExA(deviceInfoData.DevInst, pnp_instance_id, MAX_DEVICE_ID_LEN, 0, NULL) != CR_SUCCESS) {
            fprintf(stderr, "[PAL_LIST_DRIVES_WARN] CM_Get_Device_ID_ExA failed for device index %lu. Skipping.\n", device_index);
            continue;
        }
        fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] Found PnP Instance ID: %s for device index %lu\n", pnp_instance_id, device_index);

        char setupapi_device_path[MAX_PATH];
        if (!get_device_path_from_instance_id(pnp_instance_id, setupapi_device_path, MAX_PATH)) {
            fprintf(stderr, "[PAL_LIST_DRIVES_WARN] Could not get SetupAPI device path for PnP ID: %s. Skipping.\n", pnp_instance_id);
            continue;
        }
        fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] Successfully got SetupAPI path: %s for PnP ID: %s\n", setupapi_device_path, pnp_instance_id);
        fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] Attempting to open %s (via SetupAPI path)\n", setupapi_device_path);

        // per-drive processing
        BOOL current_drive_found_and_processed = FALSE;
        HANDLE hDevice = CreateFileA(setupapi_device_path,
                                   0, 
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL,
                                   OPEN_EXISTING,
                                   0,
                                   NULL);

        if (hDevice == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] CreateFileA failed for %s. Error: %lu. Skipping this device path.\n", setupapi_device_path, GetLastError());

        } else {
            fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] CreateFileA succeeded for %s. Handle: %p\n", setupapi_device_path, hDevice);

        STORAGE_PROPERTY_QUERY query;
        memset(&query, 0, sizeof(query));
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;

            BYTE buffer[2048]; 
        memset(buffer, 0, sizeof(buffer));
            DWORD bytes_returned_ioctl = 0;

            fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] Attempting DeviceIoControl (IOCTL_STORAGE_QUERY_PROPERTY) for %s\n", setupapi_device_path);
            BOOL success_prop = DeviceIoControl(hDevice,
                                     IOCTL_STORAGE_QUERY_PROPERTY,
                                     &query,
                                     sizeof(query),
                                     buffer,
                                     sizeof(buffer),
                                         &bytes_returned_ioctl,
                                     NULL);
            DWORD dioctl_error_prop = success_prop ? 0 : GetLastError();
            fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] DeviceIoControl for %s (Props): CallSuccess=%d, BytesReturned=%lu, WinErrorIfFailed=%lu\n", 
                    setupapi_device_path, success_prop, bytes_returned_ioctl, dioctl_error_prop);

            CloseHandle(hDevice); 

            if (success_prop && bytes_returned_ioctl > 0) {
            STORAGE_DEVICE_DESCRIPTOR *desc = (STORAGE_DEVICE_DESCRIPTOR *)buffer;

                current_drive_found_and_processed = TRUE;
                overall_found_any = TRUE;

            char model[128] = "N/A";
            char serial[128] = "N/A";

                if (desc->ProductIdOffset > 0 && desc->ProductIdOffset < sizeof(buffer) && desc->ProductIdOffset < bytes_returned_ioctl) {
                    char* product_ptr = (char*)buffer + desc->ProductIdOffset;
                    size_t remaining_buffer_for_product = bytes_returned_ioctl - desc->ProductIdOffset;
                    size_t copy_len_product = (sizeof(model) - 1 < remaining_buffer_for_product) ? sizeof(model) - 1 : remaining_buffer_for_product;
                    strncpy(model, product_ptr, copy_len_product);
                    model[copy_len_product] = '\0';
                trim_trailing_spaces(model);
                } else {
                     fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] Invalid or out-of-bounds ProductIdOffset for %s. Offset: %lu, BytesReturned: %lu\n", setupapi_device_path, (unsigned long)desc->ProductIdOffset, bytes_returned_ioctl);
                }
                
                if (desc->SerialNumberOffset > 0 && desc->SerialNumberOffset < sizeof(buffer) && desc->SerialNumberOffset < bytes_returned_ioctl) {
                    char* serial_ptr = (char*)buffer + desc->SerialNumberOffset;
                    size_t remaining_buffer_for_serial = bytes_returned_ioctl - desc->SerialNumberOffset;
                    size_t copy_len_serial = (sizeof(serial) -1 < remaining_buffer_for_serial) ? sizeof(serial) - 1 : remaining_buffer_for_serial;
                    strncpy(serial, serial_ptr, copy_len_serial);
                    serial[copy_len_serial] = '\0';
                trim_trailing_spaces(serial);
                } else {
                    fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] Invalid or out-of-bounds SerialNumberOffset for %s. Offset: %lu, BytesReturned: %lu\n", setupapi_device_path, (unsigned long)desc->SerialNumberOffset, bytes_returned_ioctl);
            }

                int64_t size_bytes = pal_get_device_size(setupapi_device_path); // This uses the same SetupAPI path
            double size_gb = -1.0;
            if (size_bytes > 0) {
                size_gb = (double)size_bytes / (1024.0 * 1024.0 * 1024.0);
            }

            const char* bus_type_str = "Unknown";
                if (bytes_returned_ioctl >= offsetof(STORAGE_DEVICE_DESCRIPTOR, BusType) + sizeof(desc->BusType)) {
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
                } else {
                     fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] Insufficient data to read BusType for %s. BytesReturned: %lu\n", setupapi_device_path, bytes_returned_ioctl);
                }

                // Adjust printf width for device path if necessary, SetupAPI paths can be long
            if (size_gb > 0.0) {
                     printf("%-70s | %-30s | %-25s | %-15.2f | %s\n", setupapi_device_path, model, serial, size_gb, bus_type_str);
            } else {
                     printf("%-70s | %-30s | %-25s | %-15s | %s\n", setupapi_device_path, model, serial, "N/A", bus_type_str);
                }
            } else {
                fprintf(stderr, "[DEBUG PAL_LIST_DRIVES] Skipped full processing for %s: DeviceIoControl_Prop_Success=%d, BytesReturned=%lu\n", 
                        setupapi_device_path, success_prop, bytes_returned_ioctl);
                 // Optionally print a row with N/A for this path if CreateFile succeeded but props failed
                 if (hDevice != INVALID_HANDLE_VALUE) { // If CreateFileA worked but DeviceIoControl failed
                    printf("%-70s | %-30s | %-25s | %-15s | %s\n", setupapi_device_path, "N/A (QueryFail)", "N/A", "N/A", "N/A");
                    overall_found_any = TRUE; // Still count it as a 'found' device path attempt
                 }
            }
        } // End of CreateFileA success block
        // End per-drive processing 
    } // End of SetupDiEnumDeviceInfo loop

    SetupDiDestroyDeviceInfoList(hDevInfo);

    if (!overall_found_any) {
        printf("No physical drives found or accessible via SetupAPI.\n");
    }
    return PAL_STATUS_SUCCESS;
}

#include "smart.h" 

typedef struct _ATA_PASS_THROUGH_EX_WITH_BUFFER {
    ATA_PASS_THROUGH_EX apt;
    ULONG Filler; 
    UCHAR DataBuf[512];
} ATA_PASS_THROUGH_EX_WITH_BUFFER;

void pal_windows_parse_nvme_health_log(const BYTE* raw_log_page_data, struct nvme_smart_log* nvme_data_out) {
    if (!raw_log_page_data || !nvme_data_out) {
        return;
    }
    nvme_data_out->critical_warning = raw_log_page_data[0];
    nvme_data_out->temperature = *((const uint16_t*)&raw_log_page_data[1]);
    nvme_data_out->avail_spare = raw_log_page_data[3];
    nvme_data_out->spare_thresh = raw_log_page_data[4];
    nvme_data_out->percent_used = raw_log_page_data[5];
    memcpy(nvme_data_out->data_units_read, &raw_log_page_data[32], sizeof(nvme_data_out->data_units_read));
    memcpy(nvme_data_out->data_units_written, &raw_log_page_data[48], sizeof(nvme_data_out->data_units_written));
    memcpy(nvme_data_out->host_read_commands, &raw_log_page_data[64], sizeof(nvme_data_out->host_read_commands));
    memcpy(nvme_data_out->host_write_commands, &raw_log_page_data[80], sizeof(nvme_data_out->host_write_commands));
    memcpy(nvme_data_out->controller_busy_time, &raw_log_page_data[96], sizeof(nvme_data_out->controller_busy_time));
    memcpy(nvme_data_out->power_cycles, &raw_log_page_data[112], sizeof(nvme_data_out->power_cycles));
    memcpy(nvme_data_out->power_on_hours, &raw_log_page_data[128], sizeof(nvme_data_out->power_on_hours));
    memcpy(nvme_data_out->unsafe_shutdowns, &raw_log_page_data[144], sizeof(nvme_data_out->unsafe_shutdowns));
    memcpy(nvme_data_out->media_errors, &raw_log_page_data[160], sizeof(nvme_data_out->media_errors));
    memcpy(nvme_data_out->num_err_log_entries, &raw_log_page_data[176], sizeof(nvme_data_out->num_err_log_entries));
    nvme_data_out->warning_composite_temp_time = *((const uint32_t*)&raw_log_page_data[192]);
    nvme_data_out->critical_composite_temp_time = *((const uint32_t*)&raw_log_page_data[196]);
    nvme_data_out->temp_sensor_1_trans_count = *((const uint16_t*)&raw_log_page_data[218]);
    nvme_data_out->temp_sensor_1_total_time = *((const uint32_t*)&raw_log_page_data[220]);
    nvme_data_out->temp_sensor_2_trans_count = *((const uint16_t*)&raw_log_page_data[224]);
    nvme_data_out->temp_sensor_2_total_time = *((const uint32_t*)&raw_log_page_data[226]);
}

pal_status_t pal_get_smart(const char *device_path, struct smart_data *out, nvme_hybrid_context_t *out_hybrid_ctx_details) {
    if (!device_path || !out) {
        fprintf(stderr, "[DEBUG PAL_GET_SMART] Invalid parameters (NULL device_path or out).\n");
        // Se out_hybrid_ctx_details for fornecido, pode preencher um erro nele também.
        if (out_hybrid_ctx_details) {
            ZeroMemory(out_hybrid_ctx_details, sizeof(nvme_hybrid_context_t));
            out_hybrid_ctx_details->last_operation_result.success = FALSE;
            out_hybrid_ctx_details->last_operation_result.error_code = PAL_STATUS_INVALID_PARAMETER;
            StringCchCopyA(out_hybrid_ctx_details->last_operation_result.method_name, 
                           sizeof(out_hybrid_ctx_details->last_operation_result.method_name), 
                           "InitialCheck");
        }
        return PAL_STATUS_INVALID_PARAMETER;
    }
    fprintf(stderr, "[DEBUG PAL_GET_SMART] Attempting to get SMART for device: %s\n", device_path);
    memset(out, 0, sizeof(struct smart_data));

    // Inicializar out_hybrid_ctx_details se fornecido
    if (out_hybrid_ctx_details) {
        ZeroMemory(out_hybrid_ctx_details, sizeof(nvme_hybrid_context_t));
        StringCchCopyA(out_hybrid_ctx_details->device_path, MAX_PATH, device_path);
        
        // ATIVANDO MODO BENCHMARK PARA TESTE:
        out_hybrid_ctx_details->benchmark_mode = TRUE; 
        out_hybrid_ctx_details->benchmark_iterations = 3; 
        out_hybrid_ctx_details->verbose_logging = TRUE;   
        out_hybrid_ctx_details->cache_enabled = FALSE; // Desabilitar cache durante o benchmark

        // As flags try_... continuam como padrão (TRUE para os dois primeiros métodos)
        out_hybrid_ctx_details->try_query_property = TRUE;    
        out_hybrid_ctx_details->try_protocol_command = TRUE;    
        out_hybrid_ctx_details->try_scsi_passthrough = FALSE; 
        out_hybrid_ctx_details->try_ata_passthrough = FALSE;  

        if (out_hybrid_ctx_details->benchmark_mode) {
            nvme_benchmark_init(out_hybrid_ctx_details);
        } 
        // Cache init só faria sentido se cache_enabled fosse true e não tivesse em benchmark
        // mas como cache_enabled é FALSE para benchmark, nvme_cache_init não é chamado aqui.
        // A geração de assinatura também só é relevante para cache...
    }

    // ... (Lógica de detecção de BusType e basic_info_ok)
    // ... (A chamada para pal_get_basic_drive_info para basic_info_ok ainda é útil para deviceInfo na exportação)
    // ... (A geração de assinatura para current_device_signature SÓ é necessária se o cache for usado,
    //      o que n é o caso no modo benchmark. A lógica atual em pal_get_smart que faz isso 
    //      após o if (out_hybrid_ctx_details->cache_enabled) está correta.)


    HANDLE hDeviceForBusType = CreateFileA(device_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    BOOL is_nvme_drive = FALSE;
    BasicDriveInfo basic_info; 
    ZeroMemory(&basic_info, sizeof(BasicDriveInfo));
    BOOL basic_info_ok = FALSE;

    if (hDeviceForBusType == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        fprintf(stderr, "[DEBUG PAL_GET_SMART] CreateFileA (for bus type) failed for %s. Error: %lu\n", device_path, err);
        if (out_hybrid_ctx_details) { 
            out_hybrid_ctx_details->last_operation_result.success = FALSE; out_hybrid_ctx_details->last_operation_result.error_code = err; 
        }
        if (err == ERROR_ACCESS_DENIED) return PAL_STATUS_ACCESS_DENIED;
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return PAL_STATUS_DEVICE_NOT_FOUND;
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
        basic_info_ok = pal_get_basic_drive_info(device_path, &basic_info); 
    }
    CloseHandle(hDeviceForBusType); 

    out->is_nvme = is_nvme_drive ? 1 : 0;
    out->drive_type = is_nvme_drive ? DRIVE_TYPE_NVME : DRIVE_TYPE_ATA;

    // Inicializar cache e benchmark APENAS se o contexto foi fornecido E é NVMe
    if (out_hybrid_ctx_details && is_nvme_drive) {
        if (out_hybrid_ctx_details->benchmark_mode) {
            nvme_benchmark_init(out_hybrid_ctx_details);
            fprintf(stderr, "[INFO PAL_GET_SMART] BENCHMARK MODE ENABLED. Iterations per method: %d\n", out_hybrid_ctx_details->benchmark_iterations);
        } 
        // A inicialização do cache e geração de assinatura deve ocorrer se o cache estiver habilitado,
        // mesmo que não esteja em modo benchmark (o benchmark ignora a leitura do cache de qualquer forma).
        if (out_hybrid_ctx_details->cache_enabled) {
            nvme_cache_init(out_hybrid_ctx_details);
            if (basic_info_ok) {
                nvme_cache_generate_signature(basic_info.model, basic_info.serial, out_hybrid_ctx_details->current_device_signature, sizeof(out_hybrid_ctx_details->current_device_signature));
    } else {
                StringCchCopyA(out_hybrid_ctx_details->current_device_signature, sizeof(out_hybrid_ctx_details->current_device_signature), device_path);
                fprintf(stderr, "[WARN PAL_GET_SMART] Could not get basic drive info for cache signature. Using device_path as signature.\n");
            }
        }
    }
    
    if (is_nvme_drive) {
        if (!out_hybrid_ctx_details) { // Se for NVMe, mas o contexto não foi passado, é um erro de chamada interna
            fprintf(stderr, "[ERROR PAL_GET_SMART] NVMe drive detected but no hybrid_ctx provided to pal_get_smart.\n");
            return PAL_STATUS_INVALID_PARAMETER;
        }
        BYTE nvme_log_buffer[NVME_LOG_PAGE_SIZE_BYTES];
        DWORD nvme_bytes_returned = 0;
        nvme_access_result_t temp_overall_hybrid_result; // Usar um temp para a chamada hybrid
        ZeroMemory(&temp_overall_hybrid_result, sizeof(temp_overall_hybrid_result));

        fprintf(stderr, "[DEBUG PAL_WIN_GET_SMART] NVMe drive detected. Calling hybrid NVMe SMART function for %s.\n", device_path);
        pal_status_t nvme_status = pal_get_smart_data_nvme_hybrid(device_path, out_hybrid_ctx_details, nvme_log_buffer, sizeof(nvme_log_buffer), &nvme_bytes_returned, &temp_overall_hybrid_result);
        
        // Copiar o resultado da operação para o campo last_operation_result do contexto passado
        out_hybrid_ctx_details->last_operation_result = temp_overall_hybrid_result;

        fprintf(stderr, "[INFO PAL_GET_SMART] Hybrid NVMe data fetch completed. Effective Method: %s, Success: %d, Time: %lu ms, Error Code: %lu\n",
                out_hybrid_ctx_details->last_operation_result.method_name, out_hybrid_ctx_details->last_operation_result.success, 
                out_hybrid_ctx_details->last_operation_result.execution_time_ms, out_hybrid_ctx_details->last_operation_result.error_code);

        if (out_hybrid_ctx_details->benchmark_mode) {
            nvme_benchmark_print_report(out_hybrid_ctx_details);
        }

        if (nvme_status == PAL_STATUS_SUCCESS && nvme_bytes_returned > 0 && out_hybrid_ctx_details->last_operation_result.success) {
            fprintf(stderr, "[DEBUG PAL_WIN_GET_SMART] Hybrid NVMe SMART succeeded for %s. Bytes returned: %lu. Parsing log.\n", device_path, nvme_bytes_returned);
            pal_windows_parse_nvme_health_log(nvme_log_buffer, &out->data.nvme);
            out->attr_count = 1; 
            return PAL_STATUS_SUCCESS;
        } else {
            fprintf(stderr, "[PAL_ERROR PAL_WIN_GET_SMART] Hybrid NVMe SMART failed for %s. Final PAL Status: %d, Method Success: %d, Method Error: %lu\n", 
                    device_path, nvme_status, out_hybrid_ctx_details->last_operation_result.success, out_hybrid_ctx_details->last_operation_result.error_code);
            return (nvme_status != PAL_STATUS_SUCCESS && nvme_status != PAL_STATUS_ERROR) ? nvme_status : PAL_STATUS_DEVICE_ERROR; 
        }
    } else { 
        // Lógica ATA (simplificada para retornar UNSUPPORTED por enquanto)
        fprintf(stderr, "[PAL_WARN] Non-NVMe drive detected. Attempting ATA SMART path...\n");
        if(out_hybrid_ctx_details) { // Preencher algum resultado para ATA path
            out_hybrid_ctx_details->last_operation_result.method_used = NVME_ACCESS_METHOD_ATA_PASSTHROUGH;
            StringCchCopyA(out_hybrid_ctx_details->last_operation_result.method_name, sizeof(out_hybrid_ctx_details->last_operation_result.method_name), "ATA Passthrough");
            out_hybrid_ctx_details->last_operation_result.success = FALSE;
            out_hybrid_ctx_details->last_operation_result.error_code = 0;
        }
        
        // Abrir o dispositivo para leitura SMART ATA
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
            DWORD err = GetLastError();
            fprintf(stderr, "[PAL_ERROR] Failed to open device for ATA SMART: %s (Error: %lu)\n", device_path, err);
            if(out_hybrid_ctx_details) {
                out_hybrid_ctx_details->last_operation_result.error_code = err;
            }
            return (err == ERROR_ACCESS_DENIED) ? PAL_STATUS_ACCESS_DENIED : PAL_STATUS_DEVICE_ERROR;
        }
        
        // Chamar smart_read_ata para obter os dados SMART ATA
        DWORD start_time = GetTickCount();
        int ata_result = smart_read_ata(hDevice, out);
        DWORD execution_time = GetTickCount() - start_time;
        
        CloseHandle(hDevice);
        
        if (ata_result == PAL_STATUS_SUCCESS) {
            fprintf(stderr, "[PAL_INFO] ATA SMART data successfully read for %s\n", device_path);
            if(out_hybrid_ctx_details) {
                out_hybrid_ctx_details->last_operation_result.success = TRUE;
                out_hybrid_ctx_details->last_operation_result.execution_time_ms = execution_time;
            }
            return PAL_STATUS_SUCCESS;
        } else {
            fprintf(stderr, "[PAL_ERROR] Failed to read ATA SMART data: %d\n", ata_result);
            if(out_hybrid_ctx_details) {
                out_hybrid_ctx_details->last_operation_result.error_code = ata_result;
            }
            return ata_result;
        }
    }
    // Este retorno só deve ser alcançado se algo der muito errado antes de NVMe/ATA path
    if(out_hybrid_ctx_details && !out_hybrid_ctx_details->last_operation_result.success && out_hybrid_ctx_details->last_operation_result.error_code == 0) {
         out_hybrid_ctx_details->last_operation_result.error_code = PAL_STATUS_ERROR;
    }
    return PAL_STATUS_ERROR;
}

int64_t pal_get_device_size(const char *device_path) {
    if (!device_path) {
        fprintf(stderr, "[DEBUG PAL_GET_SIZE] device_path is NULL.\n"); fflush(stderr);
        return -1; 
    }
    fprintf(stderr, "[DEBUG PAL_GET_SIZE] Attempting to get size for: %s\n", device_path); fflush(stderr);

    // Always attempt to open with GENERIC_READ for IOCTL_DISK_GET_LENGTH_INFO
    fprintf(stderr, "[DEBUG PAL_GET_SIZE] Attempting CreateFileA with GENERIC_READ for %s\n", device_path); fflush(stderr);
    HANDLE hDevice = CreateFileA(device_path,
                               GENERIC_READ, // Use GENERIC_READ for IOCTL_DISK_GET_LENGTH_INFO
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               0,
                               NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[DEBUG PAL_GET_SIZE] CreateFileA (with GENERIC_READ) failed for %s. Error: %lu\n", device_path, GetLastError()); fflush(stderr);
        return -1;
    }
    fprintf(stderr, "[DEBUG PAL_GET_SIZE] CreateFileA (with GENERIC_READ) succeeded for %s. Handle: %p\n", device_path, hDevice); fflush(stderr);

    GET_LENGTH_INFORMATION length_info;
    DWORD bytes_returned_ioctl = 0;

    fprintf(stderr, "[DEBUG PAL_GET_SIZE] Attempting DeviceIoControl (IOCTL_DISK_GET_LENGTH_INFO) for %s with handle %p\n", device_path, hDevice); fflush(stderr);
    BOOL success = DeviceIoControl(hDevice,
                                 IOCTL_DISK_GET_LENGTH_INFO,
                                 NULL,
                                 0,
                                 &length_info,
                                 sizeof(length_info),
                                 &bytes_returned_ioctl,
                                 NULL);
    DWORD dioctl_error = success ? 0 : GetLastError();
    
    fprintf(stderr, "[DEBUG PAL_GET_SIZE] DeviceIoControl for %s: CallSuccess=%d, BytesReturned=%lu, WinErrorIfFailed=%lu\n", 
            device_path, success, bytes_returned_ioctl, dioctl_error); fflush(stderr);

    CloseHandle(hDevice);

    if (!success || bytes_returned_ioctl == 0) {
        fprintf(stderr, "[DEBUG PAL_GET_SIZE] IOCTL_DISK_GET_LENGTH_INFO failed or returned 0 bytes for %s.\n", device_path); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "[DEBUG PAL_GET_SIZE] Successfully got size for %s: %lld bytes\n", device_path, length_info.Length.QuadPart); fflush(stderr);
    return length_info.Length.QuadPart;
}

bool pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info) {
    if (!device_path || !info) {
        fprintf(stderr, "[DEBUG PAL_INFO] pal_get_basic_drive_info: Invalid parameters (NULL device_path or info).\n"); fflush(stderr);
        return false;
    }
    fprintf(stderr, "[DEBUG PAL_INFO] Attempting to get basic info for: %s\n", device_path); fflush(stderr);

    memset(info, 0, sizeof(BasicDriveInfo));
    strncpy(info->model, "Unknown", sizeof(info->model) - 1);
    info->model[sizeof(info->model)-1] = '\0'; 
    strncpy(info->serial, "Unknown", sizeof(info->serial) - 1);
    info->serial[sizeof(info->serial)-1] = '\0'; 
    strncpy(info->type, "Unknown", sizeof(info->type) - 1);
    info->type[sizeof(info->type)-1] = '\0'; 
    strncpy(info->bus_type, "Unknown", sizeof(info->bus_type) - 1);
    info->bus_type[sizeof(info->bus_type)-1] = '\0';

    fprintf(stderr, "[DEBUG PAL_INFO] Attempting CreateFileA with GENERIC_READ for %s\n", device_path); fflush(stderr);
    HANDLE hDevice = CreateFileA(device_path,
                               GENERIC_READ, // Changed from 0 to GENERIC_READ
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               0, // dwFlagsAndAttributes, 0 is fine for this purpose
                               NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[DEBUG PAL_INFO] CreateFileA failed for %s. Error: %lu\n", device_path, GetLastError()); fflush(stderr);
        return false;
    }
    fprintf(stderr, "[DEBUG PAL_INFO] CreateFileA succeeded for %s. Handle: %p\n", device_path, hDevice); fflush(stderr);

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
            strncpy(info->model, (char*)buffer_desc + sdd_desc->ProductIdOffset, sizeof(info->model) - 1);
            info->model[sizeof(info->model)-1] = '\0';
            trim_trailing_spaces(info->model);
        }
        if (sdd_desc->SerialNumberOffset > 0 && sdd_desc->SerialNumberOffset < sizeof(buffer_desc)) {
            strncpy(info->serial, (char*)buffer_desc + sdd_desc->SerialNumberOffset, sizeof(info->serial) - 1);
            info->serial[sizeof(info->serial)-1] = '\0';
            trim_trailing_spaces(info->serial);
        }
        switch (sdd_desc->BusType) {
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
                              NULL) && bytes_returned_penalty >= sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR)) {
            if (penalty_desc.IncursSeekPenalty) {
                strncpy(info->type, "HDD", sizeof(info->type) - 1);
            } else {
                 if (sdd_desc->BusType == BusTypeNvme) {
                     strncpy(info->type, "NVMe", sizeof(info->type) - 1);
                } else {
                     strncpy(info->type, "SSD", sizeof(info->type) - 1);
                }
            }
        } else {
             if (sdd_desc->BusType == BusTypeNvme) {
                 strncpy(info->type, "NVMe", sizeof(info->type) - 1);
            } else {
                 strncpy(info->type, "Unknown (SeekTypeFail)", sizeof(info->type) -1);
            }
        }
        info->type[sizeof(info->type)-1] = '\0';
        info->smart_capable = true; 
    } else {
        CloseHandle(hDevice); 
        return false;
    }
    CloseHandle(hDevice);
    return true;
}

pal_status_t pal_create_directory(const char *path) {
    fprintf(stderr, "[DEBUG PAL_WIN] pal_create_directory called with path: '%s'\n", path);
    if (!path || *path == '\0') {
        fprintf(stderr, "[DEBUG PAL_WIN] Path is NULL or empty.\n");
        return PAL_STATUS_INVALID_PARAMETER;
    }
    BOOL create_dir_result = CreateDirectoryA(path, NULL);
    fprintf(stderr, "[DEBUG PAL_WIN] CreateDirectoryA('%s') result: %s\n", 
            path, create_dir_result ? "TRUE (Success)" : "FALSE (Failed)");
    if (create_dir_result) {
        fprintf(stderr, "[DEBUG PAL_WIN] CreateDirectoryA succeeded. Returning PAL_STATUS_SUCCESS.\n");
        return PAL_STATUS_SUCCESS;
    } else {
        DWORD err = GetLastError();
        char error_buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       error_buf, sizeof(error_buf), NULL);
        fprintf(stderr, "[DEBUG PAL_WIN] CreateDirectoryA failed. GetLastError(): %lu - %s\n", err, error_buf);
        if (err == ERROR_ALREADY_EXISTS) {
            fprintf(stderr, "[DEBUG PAL_WIN] Directory already exists, returning PAL_STATUS_SUCCESS.\n");
            return PAL_STATUS_SUCCESS; 
        }
        if (err == ERROR_PATH_NOT_FOUND) {
             fprintf(stderr, "[DEBUG PAL_WIN] Mapping to PAL_STATUS_INVALID_PARAMETER (path component not found).\n");
            return PAL_STATUS_INVALID_PARAMETER; 
        }
        if (err == ERROR_ACCESS_DENIED) { 
            fprintf(stderr, "[DEBUG PAL_WIN] Mapping to PAL_STATUS_ACCESS_DENIED.\n");
            return PAL_STATUS_ACCESS_DENIED; 
        }
        fprintf(stderr, "[DEBUG PAL_WIN] Mapping to PAL_STATUS_ERROR (generic).\n");
        return PAL_STATUS_ERROR; 
    }
}

pal_status_t pal_initialize(void) {
    return PAL_STATUS_SUCCESS;
}

PAL_API void pal_cleanup(void) {
}

PAL_API int pal_do_surface_scan(void *handle, unsigned long long start_lba, unsigned long long lba_count, pal_scan_callback callback, void *user_data) {
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

static int pal_get_smart_data_nvme_query_prop(
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
    int final_pal_status = PAL_STATUS_ERROR; // Renomeado de ret_status para evitar confusão com method_result
    
    PSTORAGE_PROPERTY_QUERY pQuery = NULL;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA pProtocolDataIn = NULL;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR pProtocolDataDescriptor = NULL;
    BYTE* pInBuffer = NULL;
    BYTE* pOutBuffer = NULL;
    DWORD dwInBufferSz = 0;
    DWORD dwOutBufferSz = 0;

    ULARGE_INTEGER startTime, endTime;
    GetSystemTimeAsFileTime((FILETIME*)&startTime); 

    method_result->method_used = NVME_ACCESS_METHOD_QUERY_PROPERTY;
    StringCchCopyA(method_result->method_name, sizeof(method_result->method_name), NVME_METHOD_NAME_QUERY_PROPERTY);
    method_result->success = FALSE;
    method_result->error_code = 0;
    method_result->execution_time_ms = 0;

    // Usar context->verbose_logging para condicionar logs se necessário (exemplo)
    if (context && context->verbose_logging) {
        fprintf(stderr, "[VERBOSE DEBUG PAL_NVME_SMART_QUERY_PROP] Attempting NVMe SMART via IOCTL_STORAGE_QUERY_PROPERTY for %s\n", device_path); fflush(stderr);
    } else {
        fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Attempting NVMe SMART via IOCTL_STORAGE_QUERY_PROPERTY for %s\n", device_path); fflush(stderr);
    }

    if (!device_path || !user_buffer || !bytes_returned) {
        fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] Invalid parameters.\n"); fflush(stderr);
        final_pal_status = PAL_STATUS_INVALID_PARAMETER;
        goto complete_and_cleanup; 
    }
    if (user_buffer_size < NVME_LOG_PAGE_SIZE_BYTES) {
        fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] User buffer too small. Required: %u, Provided: %lu\n", NVME_LOG_PAGE_SIZE_BYTES, user_buffer_size); fflush(stderr);
        final_pal_status = PAL_STATUS_BUFFER_TOO_SMALL;
        goto complete_and_cleanup; 
    }
    *bytes_returned = 0;

    dwInBufferSz = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    pInBuffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwInBufferSz);
    if (!pInBuffer) {
        fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] HeapAlloc failed for input buffer. Error: %lu\n", GetLastError()); fflush(stderr);
        final_pal_status = PAL_STATUS_NO_MEMORY; 
        goto complete_and_cleanup; 
    }
    fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Allocated input buffer (size %lu B at %p)\n", dwInBufferSz, pInBuffer); fflush(stderr);

    dwOutBufferSz = FIELD_OFFSET(STORAGE_PROTOCOL_DATA_DESCRIPTOR, ProtocolSpecificData) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + NVME_LOG_PAGE_SIZE_BYTES;
    pOutBuffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwOutBufferSz);
    if (!pOutBuffer) {
        fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] HeapAlloc failed for output buffer. Error: %lu\n", GetLastError()); fflush(stderr);
        final_pal_status = PAL_STATUS_NO_MEMORY; 
        goto complete_and_cleanup; 
    }
    fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Allocated output buffer (size %lu B at %p)\n", dwOutBufferSz, pOutBuffer); fflush(stderr);

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

    fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Input Query: PropID=%u, QueryType=%u\n", (UINT)pQuery->PropertyId, (UINT)pQuery->QueryType); fflush(stderr);
    fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Input ProtocolData: ProtoType=%u, DataType=%u, ReqVal=0x%lX, ReqSubVal=0x%lX, DataOff=%lu, DataLen=%lu\n",
            (UINT)pProtocolDataIn->ProtocolType, (UINT)pProtocolDataIn->DataType, 
            (unsigned long)pProtocolDataIn->ProtocolDataRequestValue, (unsigned long)pProtocolDataIn->ProtocolDataRequestSubValue,
            (unsigned long)pProtocolDataIn->ProtocolDataOffset, (unsigned long)pProtocolDataIn->ProtocolDataLength); 
    fflush(stderr);

    hDevice = CreateFileA(device_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        method_result->error_code = GetLastError();
        fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] CreateFileA failed for %s. Error: %lu\n", device_path, method_result->error_code); fflush(stderr);
        final_pal_status = PAL_STATUS_DEVICE_ERROR; 
        goto complete_and_cleanup; 
    }
    fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] CreateFileA Succeeded for %s. Handle: %p\n", device_path, hDevice); fflush(stderr);

    fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Attempting DeviceIoControl (IOCTL_STORAGE_QUERY_PROPERTY) with InBufferSize=%lu, OutBufferSize=%lu\n", dwInBufferSz, dwOutBufferSz); fflush(stderr);
    bResult = DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, pInBuffer, dwInBufferSz, pOutBuffer, dwOutBufferSz, &dwBytesReturnedIoctl, NULL);
    method_result->error_code = GetLastError();

    if (bResult && dwBytesReturnedIoctl > 0) {
        pProtocolDataDescriptor = (PSTORAGE_PROTOCOL_DATA_DESCRIPTOR)pOutBuffer;
        fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Output Descriptor: Version=%lu, Size=%lu\n", 
                (unsigned long)pProtocolDataDescriptor->Version, (unsigned long)pProtocolDataDescriptor->Size); fflush(stderr);

        if (pProtocolDataDescriptor->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR) || pProtocolDataDescriptor->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) {
            fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] Invalid STORAGE_PROTOCOL_DATA_DESCRIPTOR version or size.\n"); fflush(stderr);
            final_pal_status = PAL_STATUS_IO_ERROR;
            // method_result->error_code might already be set if DeviceIoControl failed, or keep it as the API error.
            goto complete_and_cleanup; 
        }

        PSTORAGE_PROTOCOL_SPECIFIC_DATA pProtocolDataOut = &pProtocolDataDescriptor->ProtocolSpecificData;
        fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Output ProtocolData: ProtoType=%u, DataType=%u, ReqVal=0x%lX, ReqSubVal=0x%lX, DataOff=%lu, DataLen=%lu, FixedRetData=0x%lX\n",
            (UINT)pProtocolDataOut->ProtocolType, (UINT)pProtocolDataOut->DataType, 
            (unsigned long)pProtocolDataOut->ProtocolDataRequestValue, (unsigned long)pProtocolDataOut->ProtocolDataRequestSubValue,
            (unsigned long)pProtocolDataOut->ProtocolDataOffset, (unsigned long)pProtocolDataOut->ProtocolDataLength,
            (unsigned long)pProtocolDataOut->FixedProtocolReturnData);
        fflush(stderr);

        if (pProtocolDataOut->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) {
             fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] Invalid ProtocolDataOffset in output descriptor (%lu, expected >= %lu).\n", 
                (unsigned long)pProtocolDataOut->ProtocolDataOffset, (unsigned long)sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)); 
             fflush(stderr);
             final_pal_status = PAL_STATUS_IO_ERROR;
             goto complete_and_cleanup; 
        }
        if (pProtocolDataOut->ProtocolDataLength < NVME_LOG_PAGE_SIZE_BYTES) {
            fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] Insufficient ProtocolDataLength in output descriptor (%lu, expected >= %u).\n", 
                (unsigned long)pProtocolDataOut->ProtocolDataLength, NVME_LOG_PAGE_SIZE_BYTES); 
            fflush(stderr);
            final_pal_status = PAL_STATUS_ERROR_DATA_UNDERFLOW;
            goto complete_and_cleanup; 
        }

        BYTE* pLogDataStart = (BYTE*)pProtocolDataOut + pProtocolDataOut->ProtocolDataOffset;
        DWORD min_bytes_needed = (DWORD)((BYTE*)pLogDataStart - (BYTE*)pOutBuffer) + NVME_LOG_PAGE_SIZE_BYTES;

        if (dwBytesReturnedIoctl < min_bytes_needed) {
            fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] dwBytesReturnedIoctl (%lu) is less than total needed (%lu) to read full log page.\n",
                dwBytesReturnedIoctl, min_bytes_needed);
            fflush(stderr);
            final_pal_status = PAL_STATUS_IO_ERROR;
            goto complete_and_cleanup; 
        }

        fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Successfully retrieved SMART data structure. Copying %u bytes.\n", NVME_LOG_PAGE_SIZE_BYTES); fflush(stderr);
        memcpy(user_buffer, pLogDataStart, NVME_LOG_PAGE_SIZE_BYTES);
        *bytes_returned = NVME_LOG_PAGE_SIZE_BYTES;
        final_pal_status = PAL_STATUS_SUCCESS;
        method_result->success = TRUE; // Mark success for this method
        method_result->error_code = 0; // Explicitly set no error for success case

        fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Raw data from log page (first 128 bytes):\n"); fflush(stderr);
        for (DWORD i = 0; i < NVME_LOG_PAGE_SIZE_BYTES && i < 128; ++i) {
            fprintf(stderr, "%02X ", user_buffer[i]);
            if ((i + 1) % 16 == 0) {
                fprintf(stderr, "\n");
            }
        }
        if (NVME_LOG_PAGE_SIZE_BYTES > 0 && (NVME_LOG_PAGE_SIZE_BYTES < 128 || NVME_LOG_PAGE_SIZE_BYTES % 16 != 0)) {
            fprintf(stderr, "\n");
        }
        fflush(stderr);

    } else { 
        // Error already captured in method_result->error_code from GetLastError() after DeviceIoControl
        fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_QUERY_PROP] DeviceIoControl failed or returned 0 bytes. Error: %lu, BytesReturned: %lu\n", method_result->error_code, dwBytesReturnedIoctl);
        if (method_result->error_code == ERROR_INVALID_PARAMETER) final_pal_status = PAL_STATUS_INVALID_PARAMETER; 
        else if (method_result->error_code == ERROR_NOT_SUPPORTED) final_pal_status = PAL_STATUS_UNSUPPORTED;
        else if (method_result->error_code == ERROR_ACCESS_DENIED) final_pal_status = PAL_STATUS_ACCESS_DENIED;
        else final_pal_status = PAL_STATUS_IO_ERROR;
    }

complete_and_cleanup:
    GetSystemTimeAsFileTime((FILETIME*)&endTime);
    ULARGE_INTEGER elapsed;
    elapsed.QuadPart = endTime.QuadPart - startTime.QuadPart;
    method_result->execution_time_ms = (DWORD)(elapsed.QuadPart / 10000); // Convert 100ns intervals to ms

    if (hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(hDevice); 
    }
    if (pInBuffer) HeapFree(GetProcessHeap(), 0, pInBuffer);
    if (pOutBuffer) HeapFree(GetProcessHeap(), 0, pOutBuffer);

    // If DeviceIoControl failed but we didn't set a PAL_STATUS specifically, ensure error_code reflects it
    if (final_pal_status == PAL_STATUS_SUCCESS && !method_result->success && method_result->error_code == 0 && !bResult) {
         method_result->error_code = GetLastError(); // Should have been captured already, but as a fallback
    } else if (final_pal_status != PAL_STATUS_SUCCESS && method_result->error_code == 0) {
        // If PAL status is error but no Windows error code was explicitly set in method_result for some reason
        // (e.g. CreateFile failure might not have set method_result->error_code if GetLastError wasn't stored there)
        // This is a bit of a fallback, GetLastError() might be stale here.
        // Prefer specific error code capture right after the failing call.
    }

    fprintf(stderr, "[DEBUG PAL_NVME_SMART_QUERY_PROP] Exiting with pal_status: %d, method_success: %d, method_error: %lu, method_time: %lu ms\n", 
            final_pal_status, method_result->success, method_result->error_code, method_result->execution_time_ms); 
    fflush(stderr);
    return final_pal_status;
}

// Implementação de pal_get_smart_data_nvme_protocol_cmd
static int pal_get_smart_data_nvme_protocol_cmd(
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
    int final_pal_status = PAL_STATUS_ERROR;
    
    BYTE* pCommandBuffer = NULL; 
    DWORD dwCommandBufferSz = 0;
    DWORD command_fam_offset = FIELD_OFFSET(STORAGE_PROTOCOL_COMMAND, Command);

    ULARGE_INTEGER startTime, endTime;
    GetSystemTimeAsFileTime((FILETIME*)&startTime);

    method_result->method_used = NVME_ACCESS_METHOD_PROTOCOL_COMMAND;
    StringCchCopyA(method_result->method_name, sizeof(method_result->method_name), NVME_METHOD_NAME_PROTOCOL_COMMAND);
    method_result->success = FALSE;
    method_result->error_code = 0;
    method_result->execution_time_ms = 0;

    if (context && context->verbose_logging) {
        fprintf(stderr, "[VERBOSE DEBUG PAL_NVME_SMART_IOCTL_PROTO] Attempting NVMe SMART via IOCTL_STORAGE_PROTOCOL_COMMAND for %s\n", device_path); fflush(stderr);
    } else {
        fprintf(stderr, "[DEBUG PAL_NVME_SMART_IOCTL_PROTO] Attempting NVMe SMART via IOCTL_STORAGE_PROTOCOL_COMMAND for %s\n", device_path); fflush(stderr);
    }

    if (!device_path || !user_buffer || !bytes_returned) {
        final_pal_status = PAL_STATUS_INVALID_PARAMETER;
        goto complete_and_cleanup_proto;
    }
    if (user_buffer_size < NVME_LOG_PAGE_SIZE_BYTES) {
        final_pal_status = PAL_STATUS_BUFFER_TOO_SMALL;
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

    // Logging for this method can be added here if context->verbose_logging

    hDevice = CreateFileA(device_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        method_result->error_code = GetLastError();
        final_pal_status = PAL_STATUS_DEVICE_ERROR; 
        goto complete_and_cleanup_proto;
    }

    DWORD nInBufferSize_for_ioctl = command_fam_offset + sizeof(NVME_COMMAND);
    bResult = DeviceIoControl(hDevice, IOCTL_STORAGE_PROTOCOL_COMMAND, pCommandBuffer, nInBufferSize_for_ioctl, pCommandBuffer, dwCommandBufferSz, &dwBytesReturnedIoctl, NULL);
    method_result->error_code = GetLastError();

    // LOGS ANTERIORES DOS CAMPOS INTERNOS DO DRIVER AINDA SÃO ÚTEIS PARA DEBUG
    fprintf(stderr, "[DEBUG PAL_NVME_SMART_IOCTL_PROTO] Post-Call ProtocolCommand.ReturnStatus: %lu\n", (unsigned long)spt_command->ReturnStatus);
    fprintf(stderr, "[DEBUG PAL_NVME_SMART_IOCTL_PROTO] Post-Call ProtocolCommand.ErrorCode: %lu\n", (unsigned long)spt_command->ErrorCode);
    fprintf(stderr, "[DEBUG PAL_NVME_SMART_IOCTL_PROTO] Post-Call ProtocolCommand.DataFromDeviceTransferLength: %lu\n", (unsigned long)spt_command->DataFromDeviceTransferLength);
    fflush(stderr);

    if (bResult) { // DeviceIoControl call itself reported success
        method_result->error_code = 0; 
        if (spt_command->ReturnStatus == STORAGE_PROTOCOL_STATUS_SUCCESS || spt_command->ReturnStatus == 0) {
            if (spt_command->DataFromDeviceTransferLength >= NVME_LOG_PAGE_SIZE_BYTES && dwBytesReturnedIoctl >= (command_fam_offset + sizeof(NVME_COMMAND) + NVME_LOG_PAGE_SIZE_BYTES)) {
                // Dump raw buffer ANTES de copiar, para ver o que o DeviceIoControl realmente colocou lá
                fprintf(stderr, "[DEBUG PAL_NVME_SMART_IOCTL_PROTO] DeviceIoControl SUCCESS. Raw data in pCommandBuffer (before copy to user_buffer) offset 0x%lX, size %lu B:\n", 
                    (unsigned long)(command_fam_offset + sizeof(NVME_COMMAND)), 
                    (unsigned long)spt_command->DataFromDeviceTransferLength);
                fflush(stderr);
                if (data_buffer_ptr_in_cmd_buff && spt_command->DataFromDeviceTransferLength > 0) {
                    DWORD bytes_to_dump_proto = (spt_command->DataFromDeviceTransferLength < 128) ? spt_command->DataFromDeviceTransferLength : 128;
                    for (DWORD i = 0; i < bytes_to_dump_proto; ++i) {
                        fprintf(stderr, "%02X ", data_buffer_ptr_in_cmd_buff[i]);
                        if ((i + 1) % 16 == 0) { fprintf(stderr, "\n"); }
                    }
                    if (bytes_to_dump_proto > 0 && (bytes_to_dump_proto % 16 != 0)) { fprintf(stderr, "\n"); }
                    fflush(stderr);
                }

                memcpy(user_buffer, data_buffer_ptr_in_cmd_buff, NVME_LOG_PAGE_SIZE_BYTES); 
                *bytes_returned = NVME_LOG_PAGE_SIZE_BYTES; 
                final_pal_status = PAL_STATUS_SUCCESS; 
                method_result->success = TRUE;
            } else { 
                fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_IOCTL_PROTO] DeviceIoControl success, Protocol success, but data length/bytes returned inconsistent. DataLen: %lu, BytesRet: %lu\n", 
                    (unsigned long)spt_command->DataFromDeviceTransferLength, dwBytesReturnedIoctl);
                fflush(stderr);
                final_pal_status = PAL_STATUS_ERROR_DATA_UNDERFLOW; 
            }
        } else { 
            fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_IOCTL_PROTO] DeviceIoControl success, but protocol command failed. ReturnStatus: %lu, ErrorCode: %lu\n",
                (unsigned long)spt_command->ReturnStatus, (unsigned long)spt_command->ErrorCode);
            fflush(stderr);
            final_pal_status = PAL_STATUS_DEVICE_ERROR; 
            method_result->error_code = spt_command->ReturnStatus; // Atribuir o erro do protocolo
        }
    } else { // DeviceIoControl call itself failed (bResult is FALSE)
        // method_result->error_code já foi definido por GetLastError() após DeviceIoControl
        fprintf(stderr, "[PAL_ERROR PAL_NVME_SMART_IOCTL_PROTO] DeviceIoControl call FAILED. GetLastError: %lu. Protocol Internal Status: RetStatus=%lu, ErrorCode=%lu, DataTransferLen=%lu\n", 
                method_result->error_code, 
                (unsigned long)spt_command->ReturnStatus, 
                (unsigned long)spt_command->ErrorCode, 
                (unsigned long)spt_command->DataFromDeviceTransferLength); 
        fflush(stderr);
        // Mapear o erro da API para o status PAL
        if (method_result->error_code == ERROR_INVALID_PARAMETER) final_pal_status = PAL_STATUS_INVALID_PARAMETER; 
        else if (method_result->error_code == ERROR_NOT_SUPPORTED) final_pal_status = PAL_STATUS_UNSUPPORTED;
        else if (method_result->error_code == ERROR_ACCESS_DENIED) final_pal_status = PAL_STATUS_ACCESS_DENIED;
        else final_pal_status = PAL_STATUS_IO_ERROR; // Erro genérico de IO para outras falhas de DeviceIoControl
        // method_result->success permanece FALSE (já inicializado)
    }

complete_and_cleanup_proto:
    GetSystemTimeAsFileTime((FILETIME*)&endTime);
    ULARGE_INTEGER elapsed;
    elapsed.QuadPart = endTime.QuadPart - startTime.QuadPart;
    method_result->execution_time_ms = (DWORD)(elapsed.QuadPart / 10000);

    if (hDevice != INVALID_HANDLE_VALUE) CloseHandle(hDevice);
    if (pCommandBuffer) HeapFree(GetProcessHeap(), 0, pCommandBuffer);
    
    // Atualiza error_code em method_result se um erro PAL foi setado mas nenhum erro WinAPI foi capturado
    if (final_pal_status != PAL_STATUS_SUCCESS && method_result->success == FALSE && method_result->error_code == 0) {
        method_result->error_code = (DWORD)final_pal_status; 
    }

    const char* log_prefix = (context && context->verbose_logging) ? 
                             "[VERBOSE DEBUG PAL_NVME_SMART_IOCTL_PROTO]" : 
                             "[DEBUG PAL_NVME_SMART_IOCTL_PROTO]";
    fprintf(stderr, "%s Exiting with pal_status: %d, method_success: %d, method_error: %lu, method_time: %lu ms\n", 
            log_prefix,
            final_pal_status, method_result->success, method_result->error_code, method_result->execution_time_ms); 
    fflush(stderr);
    return final_pal_status;
}

// Modificar pal_get_smart_data_nvme_hybrid para ativar o benchmark
int pal_get_smart_data_nvme_hybrid(
    const char* device_path, 
    nvme_hybrid_context_t* context, 
    BYTE* user_buffer, 
    DWORD user_buffer_size, 
    DWORD* bytes_returned,
    nvme_access_result_t* overall_result 
) {
    fprintf(stderr, "[DEBUG PAL_NVME_HYBRID] In pal_get_smart_data_nvme_hybrid for %s. BenchmarkMode: %d, CacheEnabled: %d\n", 
        device_path, context ? context->benchmark_mode : -1, context ? context->cache_enabled : -1 ); fflush(stderr);

    if (!context || !overall_result || !device_path || !user_buffer || !bytes_returned) {
        if(overall_result) {
            overall_result->method_used = NVME_ACCESS_METHOD_NONE;
            if (context) StringCchCopyA(overall_result->method_name, sizeof(overall_result->method_name), NVME_METHOD_NAME_NONE);
            else StringCchCopyA(overall_result->method_name, sizeof(overall_result->method_name), "Context_NULL");
            overall_result->success = FALSE;
            overall_result->error_code = (DWORD)PAL_STATUS_INVALID_PARAMETER;
        }
        return PAL_STATUS_INVALID_PARAMETER;
    }

    int final_pal_status_for_function = PAL_STATUS_ERROR; 

    ZeroMemory(overall_result, sizeof(nvme_access_result_t));
    overall_result->method_used = NVME_ACCESS_METHOD_NONE;
    StringCchCopyA(overall_result->method_name, sizeof(overall_result->method_name), "NoMethodAttempted");
    overall_result->success = FALSE;

    // 1. Tentar obter do cache primeiro (APENAS se não estiver em modo benchmark)
    if (context->cache_enabled && !context->benchmark_mode) {
        nvme_access_result_t cache_read_result;
        ZeroMemory(&cache_read_result, sizeof(nvme_access_result_t));
        if (nvme_cache_get(context, user_buffer, bytes_returned, &cache_read_result)) {
            *overall_result = cache_read_result;
            context->last_operation_result = cache_read_result;
            fprintf(stderr, "[INFO PAL_NVME_HYBRID] Using CACHED data. Method: %s, Time: %lu ms\n", overall_result->method_name, overall_result->execution_time_ms);
            fflush(stderr);
            return PAL_STATUS_SUCCESS; 
        }
        fprintf(stderr, "[DEBUG PAL_NVME_HYBRID] Cache MISS or expired for %s. Proceeding to live query.\n", device_path); fflush(stderr);
    }

    BOOL data_actually_fetched_for_user = FALSE;

    struct {
        BOOL should_try;
        nvme_specific_access_func_t func;
        nvme_access_method_t method_enum; // Para registrar no benchmark_result
        const char* name_str; // Para logging
    } methods_to_try[] = {
        {context->try_query_property, pal_get_smart_data_nvme_query_prop, NVME_ACCESS_METHOD_QUERY_PROPERTY, NVME_METHOD_NAME_QUERY_PROPERTY},
        {context->try_protocol_command, pal_get_smart_data_nvme_protocol_cmd, NVME_ACCESS_METHOD_PROTOCOL_COMMAND, NVME_METHOD_NAME_PROTOCOL_COMMAND}
        // Adicionar SPTD aqui no futuro: {context->try_scsi_passthrough, pal_get_smart_data_nvme_sptd, ...}
    };
    int num_total_methods = sizeof(methods_to_try) / sizeof(methods_to_try[0]);

    for (int i = 0; i < num_total_methods; ++i) {
        if (!methods_to_try[i].should_try) {
            if (context->verbose_logging) {
                 fprintf(stderr, "[VERBOSE DEBUG PAL_NVME_HYBRID] Skipping method: %s (disabled by context)\n", methods_to_try[i].name_str); fflush(stderr);
            }
            continue;
        }

        nvme_access_result_t attempt_result_detail; 
        int attempt_pal_status = PAL_STATUS_ERROR;
        DWORD attempt_bytes_returned = 0;

        if (context->benchmark_mode) {
            fprintf(stderr, "[INFO PAL_NVME_HYBRID_BENCHMARK] Benchmarking method: %s (%d iterations)\n", 
                methods_to_try[i].name_str, context->benchmark_iterations); fflush(stderr);
            for (int iter = 0; iter < context->benchmark_iterations; ++iter) {
                ZeroMemory(&attempt_result_detail, sizeof(nvme_access_result_t)); // Limpar para cada iteração
                attempt_pal_status = methods_to_try[i].func(device_path, context, user_buffer, user_buffer_size, &attempt_bytes_returned, &attempt_result_detail);
                nvme_benchmark_record_result(context, &attempt_result_detail);
                
                if (iter == 0 && attempt_result_detail.success && !data_actually_fetched_for_user) {
                    *overall_result = attempt_result_detail;
                    *bytes_returned = attempt_bytes_returned;
                    final_pal_status_for_function = attempt_pal_status;
                    data_actually_fetched_for_user = TRUE;
                }
            }
        } else { // Modo Não-Benchmark
            ZeroMemory(&attempt_result_detail, sizeof(nvme_access_result_t));
            attempt_pal_status = methods_to_try[i].func(device_path, context, user_buffer, user_buffer_size, &attempt_bytes_returned, &attempt_result_detail);
            *overall_result = attempt_result_detail; 
            context->last_operation_result = attempt_result_detail;

            if (attempt_result_detail.success) {
                fprintf(stderr, "[INFO PAL_NVME_HYBRID] Method '%s' SUCCESSFUL.\n", attempt_result_detail.method_name); fflush(stderr);
                *bytes_returned = attempt_bytes_returned;
                if (context->cache_enabled) {
                    nvme_cache_update(context, user_buffer, *bytes_returned);
                }
                return attempt_pal_status; 
            }
            fprintf(stderr, "[WARN PAL_NVME_HYBRID] Method '%s' failed (pal_status: %d, method_error: %lu). Trying next method if available.\n", 
                    attempt_result_detail.method_name, attempt_pal_status, attempt_result_detail.error_code); fflush(stderr);
            final_pal_status_for_function = attempt_pal_status; 
        }
    } // Fim do loop métodos

    // Lógica de retorno final para a função híbrida
    if (data_actually_fetched_for_user) { 
        if (context->cache_enabled && context->benchmark_mode) { // Atualizar cache com os dados da primeira tentativa bem-sucedida do benchmark
             nvme_cache_update(context, user_buffer, *bytes_returned);
        }
        return final_pal_status_for_function; // Retorna o status do primeiro sucesso que preencheu user_buffer
    }
    
    // Se chegou aqui, parabéns, tudo deu absolutamente errado
    if (!overall_result->success) { 
        fprintf(stderr, "[ERROR PAL_NVME_HYBRID] All configured/attempted NVMe access methods failed for %s. Last method tried: %s, PAL_Status: %d, ErrorCode: %lu\n", 
            device_path, overall_result->method_name, final_pal_status_for_function, overall_result->error_code); 
        fflush(stderr);
    }
    return final_pal_status_for_function; 
}

#endif // _WIN32
