#include "smart.h"
#include "pal.h"
#include "style.h" // Incluir a biblioteca de estilo
#include <stdio.h>
#include <string.h>
#include <stdint.h> // For uint64_t, uint8_t etc. used in smart_interpret/raw_to_uint64
#include <inttypes.h> // For PRIu64 etc.
#include "logging.h" 
#include <stdbool.h> // For bool type
#include "nvme_hybrid.h" // Para as funções de cache global NVMe e NVME_CACHE_KEY_MAX_LEN

// Unified smart_read function that calls the PAL to get SMART data.
// The PAL implementation (pal_get_smart_data) will handle platform-specifics.
int smart_read(const char *device_path, const char* device_model, const char* device_serial, struct smart_data *out) {

    if (!device_path || !out) {
        fprintf(stderr, "Erro: device_path ou out_data nulo em smart_read.\n");
        return PAL_STATUS_INVALID_PARAMETER;
    }

    memset(out, 0, sizeof(struct smart_data));

    // A função pal_get_smart_data irá preencher out->is_nvme.
    int pal_status = pal_get_smart_data(device_path, out);

    if (pal_status == PAL_STATUS_SUCCESS && out->is_nvme) {
        if (device_model && device_serial) {
            char cache_key[NVME_CACHE_KEY_MAX_LEN];
            nvme_cache_generate_signature(device_model, device_serial, cache_key, sizeof(cache_key));
            nvme_cache_global_update(cache_key, &out->data.nvme);
        } else {
        }
    }

    return pal_status;
}

// --- START STATIC UTILITY FUNCTIONS ---
static void get_ata_attribute_name(uint8_t id, char* buffer, int buffer_len) {
    // Basic implementation - expand as needed
    // Ensure to use snprintf for safety
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
// Function to convert raw value (6 bytes) to a human-readable 64-bit integer
// Assumes little-endian format as is common for SMART raw values.
static uint64_t raw_to_uint64(const unsigned char* raw_value) {
    uint64_t result = 0;
    for (int i = 0; i < 6; ++i) {
        result |= ((uint64_t)raw_value[i] << (i * 8));
    }
    return result;
}

// Helper function to determine if a new attribute entry is "better"
static bool is_better_attribute_entry(const struct smart_attr *new_attr, const struct smart_attr *old_attr) {
    if (old_attr->id != new_attr->id) {
        // This case should ideally not be reached if called correctly
        return false; 
    }

    // Priority 1: Non-zero value is generally preferred over zero value.
    if (old_attr->value == 0 && new_attr->value != 0) {
        return true;
    }
    if (new_attr->value == 0 && old_attr->value != 0) {
        // Old entry has a non-zero value, new one is zero. Old is better.
        return false;
    }

    // Priority 2: If values are similar (e.g., both zero or both non-zero),
    // prefer entry with non-zero flags if the other has zero flags.
    // This can indicate a more "complete" or specific entry.
    if (old_attr->flags == 0x0000 && new_attr->flags != 0x0000) {
        return true;
    }
    if (new_attr->flags == 0x0000 && old_attr->flags != 0x0000) {
        // Old entry has non-zero flags, new one has zero flags. Old is better.
        return false;
    }
    
    // If no clear heuristic makes the new attribute "better", keep the old one.
    return false; 
}

// Helper function to convert 16-byte array (NVMe counter) to uint64_t
// (assuming little-endian and we usually care about the lower 64 bits)
static uint64_t nvme_counter_to_uint64(const uint8_t counter[16]) {
    uint64_t val = 0;
    memcpy(&val, counter, sizeof(uint64_t));
    return val;
}
// Helper function to convert 2-byte array (NVMe temperature) to uint16_t
static uint16_t nvme_temp_to_uint16(const uint8_t temp[2]) {
    uint16_t val = 0;
    memcpy(&val, temp, sizeof(uint16_t));
    return val;
}

// Improved smart_interpret function
int smart_interpret(const char *device_path, struct smart_data *data) {
    printf("\n### SMART Health Report for %s ###\n", device_path);

    if (data == NULL) {
        fprintf(stderr, "Error: smart_interpret received NULL data for %s.\n", device_path);
        printf("  Could not retrieve SMART data.\n");
      
        return 1;
    } else if (data->is_nvme) {
        printf("Drive Type: NVMe\n\n");
        printf("NVMe SMART Log:\n");
        printf("+-----------------------------------------------------------------------------+\n");
        const struct smart_nvme *nvme = &data->data.nvme;

        char firmware_str[9];
        memcpy(firmware_str, nvme->firmware, 8);
        firmware_str[8] = '\0';
        for (int i = 7; i >= 0 && firmware_str[i] == ' '; i--) {
            firmware_str[i] = '\0';
        }
        printf("  %-35s : %s\n", "Firmware Revision", firmware_str);

        printf("  %-35s : 0x%02X\n", "Critical Warning Flags", nvme->critical_warning);
        // TODO: Decode critical_warning bits for more detailed output
        // Example:
        // if (nvme->critical_warning & 0x01) printf("    [Bit 0] Available spare is below threshold\n");
        // if (nvme->critical_warning & 0x02) printf("    [Bit 1] Temperature has exceeded threshold\n");
        // etc.

        uint16_t temp_k = nvme_temp_to_uint16(nvme->temperature);
        printf("  %-35s : %u K (%.1f C)\n", "Temperature", temp_k, (double)temp_k - 273.15);
        
        printf("  %-35s : %u%%\n", "Available Spare", nvme->avail_spare);
        printf("  %-35s : %u%%\n", "Available Spare Threshold", nvme->spare_thresh);
        printf("  %-35s : %u%%\n", "Percentage Used", nvme->percent_used);
        
        printf("\n  ~~~ Data Units ~~~\n");
        printf("  %-35s : %" PRIu64 " (x 1000 blocks of 512 bytes)\n", "Data Units Read", nvme_counter_to_uint64(nvme->data_units_read));
        printf("  %-35s : %" PRIu64 " (x 1000 blocks of 512 bytes)\n", "Data Units Written", nvme_counter_to_uint64(nvme->data_units_written));
        
        printf("\n  ~~~ Command Counts ~~~\n");
        printf("  %-35s : %" PRIu64 "\n", "Host Read Commands", nvme_counter_to_uint64(nvme->host_read_commands));
        printf("  %-35s : %" PRIu64 "\n", "Host Write Commands", nvme_counter_to_uint64(nvme->host_write_commands));

        printf("\n  ~~~ Controller Activity ~~~\n");
        printf("  %-35s : %" PRIu64 " minutes\n", "Controller Busy Time", nvme_counter_to_uint64(nvme->controller_busy_time));

        printf("\n  ~~~ Power Statistics ~~~\n");
        printf("  %-35s : %" PRIu64 "\n", "Power Cycles", nvme_counter_to_uint64(nvme->power_cycles));
        printf("  %-35s : %" PRIu64 " hours\n", "Power On Hours", nvme_counter_to_uint64(nvme->power_on_hours));
        printf("  %-35s : %" PRIu64 "\n", "Unsafe Shutdowns", nvme_counter_to_uint64(nvme->unsafe_shutdowns));
        
        printf("\n  ~~~ Error Information ~~~\n");
        printf("  %-35s : %" PRIu64 "\n", "Media and Data Integrity Errors", nvme_counter_to_uint64(nvme->media_errors));
        printf("  %-35s : %" PRIu64 "\n", "Error Information Log Entries", nvme_counter_to_uint64(nvme->num_err_log_entries));
        printf("+-----------------------------------------------------------------------------+\n");

    } else { // SEÇÃO ATA/SATA (Lógica original movida para cá)
        printf("Drive Type: ATA/SATA\n\n");
        
        // A verificação de data->attr_count == 0 deve vir após sabermos que é ATA
        // e ANTES de tentar processar ou imprimir cabeçalhos de tabela.
        if (data->attr_count == 0) { // data aqui não pode ser NULL por causa do 'else if' anterior
            printf("  No SMART attributes to display for this ATA/SATA drive.\n");
    } else {
            printf("Classic SMART Attributes:\n");
#ifdef _WIN32
            printf("  +-----+--------------------------+--------+------+------+-------+---------------------+----------+\n");
            printf("  | ID# | ATTRIBUTE_NAME           | FLAGS  | VALUE| WORST| THRESH| RAW_VALUE           | STATUS   |\n");
            printf("  +-----+--------------------------+--------+------+------+-------+---------------------+----------+\n");
#else
            printf("  ┌─────┬──────────────────────────┬────────┬──────┬──────┬───────┬─────────────────────┬──────────┐\n");
            printf("  │ ID# │ ATTRIBUTE_NAME           │ FLAGS  │ VALUE│ WORST│ THRESH│ RAW_VALUE           │ STATUS   │\n");
            printf("  ├─────┼──────────────────────────┼────────┼──────┼──────┼───────┼─────────────────────┼──────────┤\n");
#endif

            // ***** INÍCIO DA LÓGICA ORIGINAL DE PROCESSAMENTO E IMPRESSÃO ATA PRESERVADA *****
            // (Corresponde aproximadamente às linhas 242-339 do arquivo original src/smart.c ANTES de qualquer modificação minha)

            int current_data_attr_count = data->attr_count;
            if (data->attr_count > MAX_SMART_ATTRIBUTES) {
                current_data_attr_count = MAX_SMART_ATTRIBUTES;
            }

            struct smart_attr final_attributes[MAX_SMART_ATTRIBUTES];
            int final_attr_count = 0;
            memset(final_attributes, 0, sizeof(final_attributes)); 

            for (int i = 0; i < current_data_attr_count; i++) {
                if (i >= MAX_SMART_ATTRIBUTES) { 
                    break;
                }
                const struct smart_attr *current_raw_attr = &data->data.attrs[i];
                uint8_t current_id = current_raw_attr->id;

                if (current_id == 0 || current_id == 0xFF) { 
                    continue;
                }

                int existing_idx = -1;
                for (int j = 0; j < final_attr_count; j++) {
                    if (final_attributes[j].id == current_id) {
                        existing_idx = j;
                        break;
                    }
                }

                if (existing_idx != -1) { 
                    if (is_better_attribute_entry(current_raw_attr, &final_attributes[existing_idx])) {
                        final_attributes[existing_idx] = *current_raw_attr;
                    } else {
                    }
                } else { 
                    if (final_attr_count < MAX_SMART_ATTRIBUTES) {
                        final_attributes[final_attr_count++] = *current_raw_attr;
                    } else {
                        fprintf(stderr, "Warning: Exceeded MAX_SMART_ATTRIBUTES (%d) in final_attributes list. Some unique attributes might be lost.\n", MAX_SMART_ATTRIBUTES);
                    }
                }
            }
            
            memcpy(data->data.attrs, final_attributes, final_attr_count * sizeof(struct smart_attr));
            data->attr_count = final_attr_count;

            for (int i = 0; i < data->attr_count; i++) {
                const struct smart_attr *attr = &data->data.attrs[i];
                char attr_name_str[30];
                get_ata_attribute_name(attr->id, attr_name_str, sizeof(attr_name_str));

            uint64_t raw_val = raw_to_uint64(attr->raw);
                char status_str[15] = "OK"; 
                
                // Lógica para determinar o status
                if (attr->flags & 0x0001) { // Prefailure attribute
                    if (attr->value <= attr->threshold) {
                        strcpy(status_str, "Prefail");
                    }
                }

#ifdef _WIN32
                printf("  | %-3u | %-24s | 0x%04X | %-4u | %-4u | %-5u | %-19" PRIu64 " |",
#else
                printf("  │ %-3u │ %-24s │ 0x%04X │ %-4u │ %-4u │ %-5u │ %-19" PRIu64 " │",
#endif
                    attr->id, attr_name_str, attr->flags, attr->value, attr->worst,
                    attr->threshold, raw_val);
                
                // Aplicar cor com base no status
                if (strcmp(status_str, "Prefail") == 0) {
                    style_set_fg(COLOR_BRIGHT_YELLOW);
                } else {
                    style_set_fg(COLOR_BRIGHT_GREEN);
                }
#ifdef _WIN32
                printf(" %-8s |", status_str);
#else
                printf(" %-8s │", status_str);
#endif
                style_reset(); // Resetar cor para o padrão
                printf("\n");
            }
            // ***** FIM DA LÓGICA ORIGINAL DE PROCESSAMENTO E IMPRESSÃO ATA PRESERVADA *****
#ifdef _WIN32
            printf("  +-----+--------------------------+--------+------+------+-------+---------------------+----------+\n");
#else
            printf("  └─────┴──────────────────────────┴────────┴──────┴──────┴───────┴─────────────────────┴──────────┘\n");
#endif
        }
    } // Fim do else para ATA/SATA

    // Rodapé final da função, comum a todos os casos (exceto data == NULL que já retorna)
    if (data != NULL) { // Só imprime rodapé completo se data não era NULL inicialmente
        printf("===================== End of Report for %s ======================\n\n", device_path);
    }
    fflush(stdout); 
    return (data == NULL) ? 1 : 0; // Retorna 1 se data era NULL, 0 caso contrário.
}

// Function to provide a general health summary based on SMART data
// TODO: This function needs to be significantly more sophisticated
// to provide a truly reliable health summary.
SmartStatus smart_get_health_summary(const struct smart_data *data) {
    if (!data) {
        return SMART_HEALTH_UNKNOWN;
    }

    if (data->is_nvme) {
        SmartStatus nvme_status = SMART_HEALTH_OK;
        const struct smart_nvme *nvme = &data->data.nvme;

        if (nvme->critical_warning & 0x01) { // Available spare is below threshold
            nvme_status = SMART_HEALTH_WARNING;
        }
        if (nvme->critical_warning & 0x02) { // Temperature is above threshold
            nvme_status = SMART_HEALTH_WARNING; // Could be FAILNG depending on policy
        }
        if (nvme->critical_warning & 0x04) { // Reliability is degraded
            nvme_status = SMART_HEALTH_WARNING;
        }
        if (nvme->critical_warning & 0x08) { // Media is in read-only mode
            nvme_status = SMART_HEALTH_FAILING;
        }
        if (nvme->critical_warning & 0x10) { // Volatile memory backup device has failed
            nvme_status = SMART_HEALTH_FAILING;
        }

        // Check percentage used (this is more of a wear indicator than immediate failure)
        if (nvme->percent_used >= 90 && nvme_status == SMART_HEALTH_OK) {
            // Not changing status to WARNING unless other critical flags are set, 
            // as high usage is expected over time.
        } 
        // If it's already warning/failing, no need to downgrade to OK if percent_used is low
        
        return nvme_status; // Return the most severe status found for NVMe

    } else {
        // ATA/SATA specific health summary
        SmartStatus overall_status = SMART_HEALTH_OK;

        for (int i = 0; i < data->attr_count; ++i) {
            const struct smart_attr *attr = &data->data.attrs[i];
            uint64_t raw_val = raw_to_uint64(attr->raw); 
            SmartStatus current_attr_status = SMART_HEALTH_OK;

            // Primary logic: based on normalized value and drive's threshold
            if (attr->threshold != 0 && attr->value <= attr->threshold) {
                if (attr->flags & 0x0001) { // SMART_PREFAIL_WARRANTY_ATTRIBUTE
                    current_attr_status = SMART_HEALTH_PREFAIL;
                } else {
                    current_attr_status = SMART_HEALTH_WARNING;
                }
            } 

            // Specific checks for critical attributes based on RAW values,
            // potentially overriding previous status if more severe.
            if (attr->id == 5 && raw_val > 0) { // Reallocated Sectors
                if (SMART_HEALTH_PREFAIL > current_attr_status) { // Only elevate if current status is less severe
                    current_attr_status = SMART_HEALTH_PREFAIL;
                }
            } else if (attr->id == 197 && raw_val > 0) { // Current Pending Sector
                if (SMART_HEALTH_PREFAIL > current_attr_status) { // Only elevate if current status is less severe
                    current_attr_status = SMART_HEALTH_PREFAIL;
                }
            } else if (attr->id == 198 && raw_val > 0) { // Offline Uncorrectable
                if (SMART_HEALTH_PREFAIL > current_attr_status) { // Only elevate if current status is less severe
                    current_attr_status = SMART_HEALTH_PREFAIL;
                }
            }

            // Update overall_status if current_attr_status is more severe
            if (current_attr_status > overall_status) {
                overall_status = current_attr_status;
            }
        }
   
        return overall_status;
    }
}

// Ensure the following utility functions are available (either in this file or linked
// from an external file like smart_utils.c and declared in a header or via extern below):
// extern void get_ata_attribute_name(uint8_t id, char* buffer, int buffer_len);
// extern uint64_t raw_to_uint64(const uint8_t raw[6]); // This is static in this file, no extern needed.
// extern uint8_t get_ata_threshold_value(uint8_t id);

// raw_to_uint64 is defined as static in this file.
// get_ata_attribute_name and get_ata_threshold_value are expected to be linked externally.
