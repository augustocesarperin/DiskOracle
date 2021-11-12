#include "nvme_hybrid.h"
#include "pal.h"           // Para BasicDriveInfo e PAL_STATUS, etc.
#include "smart.h"         // Para struct nvme_smart_log
#include <stdio.h>
#include <string.h>      // Para ZeroMemory (via windows.h) ou memset
#include <strsafe.h>     // Adicionar este include
#include <windows.h>     // Para StringCchPrintfA, SYSTEMTIME (embora SYSTEMTIME esteja em nvme_hybrid.h)

// Alerta 
// Temperatura em Celsius
#define TEMP_THRESHOLD_WARN_C 60
#define TEMP_THRESHOLD_CRITICAL_C 70

// Percentage Used
#define PERCENTAGE_USED_THRESHOLD_WARN 80
#define PERCENTAGE_USED_THRESHOLD_CRITICAL 90

// Unsafe Shutdowns - Limiares absolutos para warning/critical
#define UNSAFE_SHUTDOWNS_WARN 10
#define UNSAFE_SHUTDOWNS_CRITICAL 50


#define ERROR_LOG_ENTRIES_WARN 5
#define ERROR_LOG_ENTRIES_CRITICAL 20

// Helper para adicionar alerta
static void add_alert(
    nvme_health_alerts_t* health_alerts, 
    nvme_alert_type_t type, 
    BOOL is_critical, 
    const char* desc_format, 
    const char* current_val, 
    const char* threshold_val 
) {
    if (health_alerts->alert_count < MAX_NVME_ALERTS) {
        nvme_alert_info_t* alert = &health_alerts->alerts[health_alerts->alert_count];
        alert->alert_type = type;
        alert->is_critical = is_critical;
        StringCchPrintfA(alert->description, MAX_NVME_ALERT_DESCRIPTION, desc_format, current_val); // Preenche a descrição com o valor atual
        StringCchCopyA(alert->current_value_str, MAX_NVME_ALERT_VALUE_STR, current_val);
        StringCchCopyA(alert->threshold_str, MAX_NVME_ALERT_VALUE_STR, threshold_val);
        health_alerts->alert_count++;
    }
}

// Analisa os dados SMART NVMe e preenche a estrutura de alertas.
void nvme_analyze_health_alerts(
    const struct smart_nvme* smart_log, 
    nvme_health_alerts_t* health_alerts_out,
    BYTE device_spare_threshold // Vem do Identify Controller data (padrão 10% se não disponível)
) {
    if (!smart_log || !health_alerts_out) {
        return;
    }

    ZeroMemory(health_alerts_out, sizeof(nvme_health_alerts_t));
    // health_alerts_out->alert_count = 0; // ZeroMemory já faz isso


    char current_val_str[MAX_NVME_ALERT_VALUE_STR];
    char threshold_str[MAX_NVME_ALERT_VALUE_STR];

    // 1. Critical Warning Flags
    if (smart_log->critical_warning != 0) {
        StringCchPrintfA(current_val_str, sizeof(current_val_str), "0x%02X", smart_log->critical_warning);
        
        char desc[MAX_NVME_ALERT_DESCRIPTION] = "Critical Warning Flags Set: ";
        if (smart_log->critical_warning & 0x01) StringCchCatA(desc, sizeof(desc), "Spare_Low; ");
        if (smart_log->critical_warning & 0x02) StringCchCatA(desc, sizeof(desc), "Temp_High; ");
        if (smart_log->critical_warning & 0x04) StringCchCatA(desc, sizeof(desc), "Reliability; ");
        if (smart_log->critical_warning & 0x08) StringCchCatA(desc, sizeof(desc), "ReadOnly; ");
        if (smart_log->critical_warning & 0x10) StringCchCatA(desc, sizeof(desc), "VolatileMemBackupFail; ");
        // Remover o último "; "
        if(strlen(desc) > 2 && desc[strlen(desc)-2] == ';') desc[strlen(desc)-2] = '\0'; 

        add_alert(health_alerts_out, NVME_ALERT_CRITICAL_WARNING_FLAGS, TRUE, desc, current_val_str, "!= 0x00");
    }

    // 2. Temperature (em Kelvin, converter para Celsius para avaliação)
    // smart_log->temperature é uint16_t. [0] é LSB, [1] é MSB. A struct já deve lidar com isso eu acho.
    uint16_t temp_kelvin = 0;
    memcpy(&temp_kelvin, smart_log->temperature, sizeof(uint16_t)); // Correctly copy 2 bytes

    int temp_celsius = (int)temp_kelvin - 273; 
    StringCchPrintfA(current_val_str, sizeof(current_val_str), "%d C", temp_celsius);
    if (temp_celsius > TEMP_THRESHOLD_CRITICAL_C) {
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d C", TEMP_THRESHOLD_CRITICAL_C);
        add_alert(health_alerts_out, NVME_ALERT_TEMPERATURE_HIGH, TRUE, "Temperature Critical", current_val_str, threshold_str);
    } else if (temp_celsius > TEMP_THRESHOLD_WARN_C) {
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d C", TEMP_THRESHOLD_WARN_C);
        add_alert(health_alerts_out, NVME_ALERT_TEMPERATURE_HIGH, FALSE, "Temperature Warning", current_val_str, threshold_str);
    }

    BYTE current_spare = smart_log->avail_spare;
    StringCchPrintfA(current_val_str, sizeof(current_val_str), "%u%%", current_spare);
    if (current_spare < device_spare_threshold) {
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "< %u%%", device_spare_threshold);
        add_alert(health_alerts_out, NVME_ALERT_SPARE_LOW, TRUE, "Available Spare Critical", current_val_str, threshold_str);
    } else if (current_spare < (device_spare_threshold + 5)) { // Warning se estiver 5% acima do limiar crítico
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "< %u%% (Warn)", device_spare_threshold + 5);
        add_alert(health_alerts_out, NVME_ALERT_SPARE_LOW, FALSE, "Available Spare Low", current_val_str, threshold_str);
    }

    // % Used
    BYTE percent_used = smart_log->percent_used;
    StringCchPrintfA(current_val_str, sizeof(current_val_str), "%u%%", percent_used);
    if (percent_used > PERCENTAGE_USED_THRESHOLD_CRITICAL) {
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d%%", PERCENTAGE_USED_THRESHOLD_CRITICAL);
        add_alert(health_alerts_out, NVME_ALERT_PERCENTAGE_USED_HIGH, TRUE, "Percentage Used Critical", current_val_str, threshold_str);
    } else if (percent_used > PERCENTAGE_USED_THRESHOLD_WARN) {
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d%%", PERCENTAGE_USED_THRESHOLD_WARN);
        add_alert(health_alerts_out, NVME_ALERT_PERCENTAGE_USED_HIGH, FALSE, "Percentage Used High", current_val_str, threshold_str);
    }
    // 5. Unsafe Shutdowns (handle full 128-bit field)
    uint64_t unsafe_shutdowns_low = 0, unsafe_shutdowns_high = 0;
    memcpy(&unsafe_shutdowns_low, smart_log->unsafe_shutdowns, sizeof(uint64_t));
    memcpy(&unsafe_shutdowns_high, smart_log->unsafe_shutdowns + 8, sizeof(uint64_t)); // Next 8 bytes

    if (unsafe_shutdowns_high > 0) {
        StringCchPrintfA(current_val_str, sizeof(current_val_str), "Value > 2^64-1");
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d", UNSAFE_SHUTDOWNS_CRITICAL);
        add_alert(health_alerts_out, NVME_ALERT_UNSAFE_SHUTDOWNS_HIGH, TRUE, "Unsafe Shutdowns Critical (Value Exceeds 64-bit)", current_val_str, threshold_str);
    } else {
        StringCchPrintfA(current_val_str, sizeof(current_val_str), "%llu", unsafe_shutdowns_low);
        if (unsafe_shutdowns_low > UNSAFE_SHUTDOWNS_CRITICAL) {
            StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d", UNSAFE_SHUTDOWNS_CRITICAL);
            add_alert(health_alerts_out, NVME_ALERT_UNSAFE_SHUTDOWNS_HIGH, TRUE, "Unsafe Shutdowns Critical", current_val_str, threshold_str);
        } else if (unsafe_shutdowns_low > UNSAFE_SHUTDOWNS_WARN) {
            StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d", UNSAFE_SHUTDOWNS_WARN);
            add_alert(health_alerts_out, NVME_ALERT_UNSAFE_SHUTDOWNS_HIGH, FALSE, "Unsafe Shutdowns High", current_val_str, threshold_str);
        }
    }

    // 6. Media Errors (handle full 128-bit field)
    uint64_t media_errors_low = 0, media_errors_high = 0;
    memcpy(&media_errors_low, smart_log->media_errors, sizeof(uint64_t));
    memcpy(&media_errors_high, smart_log->media_errors + 8, sizeof(uint64_t)); // Next 8 bytes

    if (media_errors_high > 0) {
        StringCchPrintfA(current_val_str, sizeof(current_val_str), "Value > 2^64-1");
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "> 0");
        add_alert(health_alerts_out, NVME_ALERT_MEDIA_ERRORS_HIGH, TRUE, "Media Errors Detected (Value Exceeds 64-bit)", current_val_str, threshold_str);
    } else if (media_errors_low > 0) { // Only check low part if high part is zero
        StringCchPrintfA(current_val_str, sizeof(current_val_str), "%llu", media_errors_low);
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "> 0");
        add_alert(health_alerts_out, NVME_ALERT_MEDIA_ERRORS_HIGH, TRUE, "Media Errors Detected", current_val_str, threshold_str);
    }

    // 7. Number of Error Log Entries (handle full 128-bit field)
    uint64_t num_err_log_entries_low = 0, num_err_log_entries_high = 0;
    memcpy(&num_err_log_entries_low, smart_log->num_err_log_entries, sizeof(uint64_t));
    memcpy(&num_err_log_entries_high, smart_log->num_err_log_entries + 8, sizeof(uint64_t)); // Next 8 bytes

    if (num_err_log_entries_high > 0) {
        StringCchPrintfA(current_val_str, sizeof(current_val_str), "Value > 2^64-1");
        StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d", ERROR_LOG_ENTRIES_CRITICAL);
        add_alert(health_alerts_out, NVME_ALERT_ERROR_LOG_ENTRIES_HIGH, TRUE, "Error Log Entries Critical (Value Exceeds 64-bit)", current_val_str, threshold_str);
    } else {
        StringCchPrintfA(current_val_str, sizeof(current_val_str), "%llu", num_err_log_entries_low);
        if (num_err_log_entries_low > ERROR_LOG_ENTRIES_CRITICAL) {
            StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d", ERROR_LOG_ENTRIES_CRITICAL);
            add_alert(health_alerts_out, NVME_ALERT_ERROR_LOG_ENTRIES_HIGH, TRUE, "Error Log Entries Critical", current_val_str, threshold_str);
        } else if (num_err_log_entries_low > ERROR_LOG_ENTRIES_WARN) {
            StringCchPrintfA(threshold_str, sizeof(threshold_str), "> %d", ERROR_LOG_ENTRIES_WARN);
            add_alert(health_alerts_out, NVME_ALERT_ERROR_LOG_ENTRIES_HIGH, FALSE, "Error Log Entries High", current_val_str, threshold_str);
        }
    }

    // fprintf(stderr, "[DEBUG NVME_ALERTS] Health analysis complete. Alerts generated: %d\n", health_alerts_out->alert_count);
    // fflush(stderr);
}
