#include "smart.h"
#include "pal.h"
#include "logging.h"
#include "smart_ata.h"
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h> 
#include <stdio.h>
#include <string.h>
#include <stddef.h> // offsetof

// Comandos SMART ATA
#define ATA_CMD_SMART           0xB0    // SMART
#define SMART_CMD_READ_DATA     0xD0    // READ DATA
#define SMART_CMD_READ_THRESH   0xD1    // THRESHOLDS
#define SMART_CMD_RETURN_STATUS 0xDA    // RETURN STATUS

typedef struct _ATA_PASS_THROUGH_EX_WITH_BUFFER {
    ATA_PASS_THROUGH_EX apt;
    ULONG Filler; 
    UCHAR DataBuf[512];
} ATA_PASS_THROUGH_EX_WITH_BUFFER;

int smart_read_ata(PAL_DEV device, struct smart_data* out)
{
    if (!device || !out) {
        fprintf(stderr, "[ERROR] Invalid device handle or output buffer\n");
        return PAL_STATUS_INVALID_PARAMETER;
    }

    // Checando se o SMART tá on
    ATA_PASS_THROUGH_EX_WITH_BUFFER smart_status_buf = {0};
    
    smart_status_buf.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    smart_status_buf.apt.AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
    smart_status_buf.apt.DataTransferLength = 0;
    smart_status_buf.apt.TimeOutValue = 5; // timeout de 5 segundos
    smart_status_buf.apt.DataBufferOffset = 0;
    
    // Registradores
    smart_status_buf.apt.CurrentTaskFile[0] = 0xDA; // SMART RETURN STATUS
    smart_status_buf.apt.CurrentTaskFile[1] = 0;    // Sector Count
    smart_status_buf.apt.CurrentTaskFile[2] = 0;    // LBA Low
    smart_status_buf.apt.CurrentTaskFile[3] = 0x4F; // LBA Mid (sector num)
    smart_status_buf.apt.CurrentTaskFile[4] = 0xC2; // LBA High (cylinder low)
    smart_status_buf.apt.CurrentTaskFile[5] = 0xA0; // Device register (master)
    smart_status_buf.apt.CurrentTaskFile[6] = 0xB0; // Command reg: SMART
    
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
        fprintf(stderr, "[ERROR] SMART RETURN STATUS command failed, error: %lu\n", GetLastError());
        return PAL_STATUS_IO_ERROR;
    }
    
    fprintf(stderr, "[DEBUG] SMART status check bytes returned: %lu\n", bytesReturned);
    fprintf(stderr, "[DEBUG] LBA Mid: 0x%02X, LBA High: 0x%02X\n", 
              smart_status_buf.apt.CurrentTaskFile[3],
              smart_status_buf.apt.CurrentTaskFile[4]);
    
    // Checagem do status SMaRT
    // 0x4F/0xC2 = ok, 0xF4/0x2C = não ok 
    if (smart_status_buf.apt.CurrentTaskFile[3] == 0xF4 && 
        smart_status_buf.apt.CurrentTaskFile[4] == 0x2C) {
        fprintf(stderr, "[WARNING] SMART threshold exceeded! Drive may fail soon!\n");
    } else if (smart_status_buf.apt.CurrentTaskFile[3] != 0x4F || 
               smart_status_buf.apt.CurrentTaskFile[4] != 0xC2) {
        fprintf(stderr, "[WARNING] Unexpected SMART status values: %02X %02X\n", 
                   smart_status_buf.apt.CurrentTaskFile[3],
                   smart_status_buf.apt.CurrentTaskFile[4]);
    } else {
        fprintf(stderr, "[DEBUG] SMART status check passed, drive health good\n");
    }

    // Leitura dos dados SMART
    ATA_PASS_THROUGH_EX_WITH_BUFFER smart_read_buf = {0};
    
    smart_read_buf.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    smart_read_buf.apt.AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
    smart_read_buf.apt.DataTransferLength = sizeof(smart_read_buf.DataBuf);
    smart_read_buf.apt.TimeOutValue = 5;
    smart_read_buf.apt.DataBufferOffset = FIELD_OFFSET(ATA_PASS_THROUGH_EX_WITH_BUFFER, DataBuf);
    
    smart_read_buf.apt.CurrentTaskFile[0] = 0xD0; 
    smart_read_buf.apt.CurrentTaskFile[1] = 1;    
    smart_read_buf.apt.CurrentTaskFile[2] = 0;    
    smart_read_buf.apt.CurrentTaskFile[3] = 0x4F; 
    smart_read_buf.apt.CurrentTaskFile[4] = 0xC2; 
    smart_read_buf.apt.CurrentTaskFile[5] = 0xA0; 
    smart_read_buf.apt.CurrentTaskFile[6] = 0xB0; 
    
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
    
    fprintf(stderr, "[DEBUG] SMART READ DATA command succeeded, bytes returned: %lu\n", bytesReturned);
    
    // Debug dump dos primeiros bytes
    fprintf(stderr, "[DEBUG] First 16 bytes of SMART data: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
              smart_read_buf.DataBuf[0], smart_read_buf.DataBuf[1], 
              smart_read_buf.DataBuf[2], smart_read_buf.DataBuf[3],
              smart_read_buf.DataBuf[4], smart_read_buf.DataBuf[5],
              smart_read_buf.DataBuf[6], smart_read_buf.DataBuf[7],
              smart_read_buf.DataBuf[8], smart_read_buf.DataBuf[9],
              smart_read_buf.DataBuf[10], smart_read_buf.DataBuf[11],
              smart_read_buf.DataBuf[12], smart_read_buf.DataBuf[13],
              smart_read_buf.DataBuf[14], smart_read_buf.DataBuf[15]);
    
    if (smart_read_buf.DataBuf[0] != 0x00 || smart_read_buf.DataBuf[1] != 0x00) {
        fprintf(stderr, "[WARNING] Invalid SMART data structure, expected 0x0000 at beginning\n");
    }
    
    memset(out, 0, sizeof(struct smart_data));
    out->drive_type = DRIVE_TYPE_ATA;
    out->is_nvme = 0;
    
    fprintf(stderr, "[DEBUG] Parsing SMART attribute table\n");
    int attr_count = 0;
    
    // (offset 2, 12 bytes por atributo)
    for (int i = 0; i < MAX_SMART_ATTRIBUTES; i++) {
        int offset = 2 + (i * 12);
        BYTE attr_id = smart_read_buf.DataBuf[offset];
        
        if (attr_id == 0) {
            continue;
        }
        
        BYTE flags_lo = smart_read_buf.DataBuf[offset + 1];
        BYTE flags_hi = smart_read_buf.DataBuf[offset + 2];
        BYTE value = smart_read_buf.DataBuf[offset + 3];
        BYTE worst = smart_read_buf.DataBuf[offset + 4];
        BYTE threshold = smart_read_buf.DataBuf[offset + 5];
        
        // Raw value (6 bytes)
        UINT64 raw_value = 0;
        for (int j = 0; j < 6; j++) {
            raw_value |= ((UINT64)smart_read_buf.DataBuf[offset + 6 + j]) << (j * 8);
        }
        
        fprintf(stderr, "[DEBUG] SMART Attribute %d: ID=%d, Value=%d, Worst=%d, Threshold=%d, Raw=%llu\n", 
                 i, attr_id, value, worst, threshold, raw_value);
        
        // Guarda
        if (attr_count < MAX_SMART_ATTRIBUTES) {
            out->data.attrs[attr_count].id = attr_id;
            out->data.attrs[attr_count].value = value;
            out->data.attrs[attr_count].worst = worst;
            out->data.attrs[attr_count].threshold = threshold;
            
            // Raw bytes
            for (int j = 0; j < 6; j++) {
                out->data.attrs[attr_count].raw[j] = smart_read_buf.DataBuf[offset + 6 + j];
            }
            
            out->data.attrs[attr_count].flags = (flags_hi << 8) | flags_lo;
            
            // Nome do atributo
            switch(attr_id) {
                case 1: strcpy(out->data.attrs[attr_count].name, "Raw_Read_Error_Rate"); break;
                case 2: strcpy(out->data.attrs[attr_count].name, "Throughput_Performance"); break;
                case 3: strcpy(out->data.attrs[attr_count].name, "Spin_Up_Time"); break;
                case 4: strcpy(out->data.attrs[attr_count].name, "Start_Stop_Count"); break;
                case 5: strcpy(out->data.attrs[attr_count].name, "Reallocated_Sector_Ct"); break;
                case 7: strcpy(out->data.attrs[attr_count].name, "Seek_Error_Rate"); break;
                case 8: strcpy(out->data.attrs[attr_count].name, "Seek_Time_Performance"); break;
                case 9: strcpy(out->data.attrs[attr_count].name, "Power_On_Hours"); break;
                case 10: strcpy(out->data.attrs[attr_count].name, "Spin_Retry_Count"); break;
                case 11: strcpy(out->data.attrs[attr_count].name, "Calibration_Retry_Count"); break;
                case 12: strcpy(out->data.attrs[attr_count].name, "Power_Cycle_Count"); break;
                case 13: strcpy(out->data.attrs[attr_count].name, "Read_Soft_Error_Rate"); break;
                case 183: strcpy(out->data.attrs[attr_count].name, "Runtime_Bad_Block"); break;
                case 184: strcpy(out->data.attrs[attr_count].name, "End-to-End_Error"); break;
                case 187: strcpy(out->data.attrs[attr_count].name, "Reported_Uncorrect"); break;
                case 188: strcpy(out->data.attrs[attr_count].name, "Command_Timeout"); break;
                case 190: strcpy(out->data.attrs[attr_count].name, "Airflow_Temperature_Cel"); break;
                case 194: strcpy(out->data.attrs[attr_count].name, "Temperature_Celsius"); break;
                case 196: strcpy(out->data.attrs[attr_count].name, "Reallocated_Event_Count"); break;
                case 197: strcpy(out->data.attrs[attr_count].name, "Current_Pending_Sector"); break;
                case 198: strcpy(out->data.attrs[attr_count].name, "Offline_Uncorrectable"); break;
                case 199: strcpy(out->data.attrs[attr_count].name, "UDMA_CRC_Error_Count"); break;
                case 200: strcpy(out->data.attrs[attr_count].name, "Multi_Zone_Error_Rate"); break;
                default: 
                    snprintf(out->data.attrs[attr_count].name, sizeof(out->data.attrs[attr_count].name), "Unknown_Attribute_%d", attr_id);
                    break;
            }
            
            attr_count++;
        }
    }
    
    out->attr_count = attr_count;
    fprintf(stderr, "[DEBUG] Parsed %d SMART attributes\n", attr_count);
    
    return PAL_STATUS_SUCCESS;
}