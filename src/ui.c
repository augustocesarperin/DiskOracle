#include "ui.h"
#include "style.h"
#include "pal.h"
#include "surface.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

void ui_display_error_log_entry(const NVMeErrorLogEntry* log_entry, int entry_number) {
    if (!log_entry) {
        return;
    }

    // Status Field components
    uint16_t status_code = log_entry->status_field & 0x7FF; // Bits 0-10
    uint8_t status_code_type = (log_entry->status_field >> 11) & 0x7; // Bits 11-13
    bool more = (log_entry->status_field >> 14) & 0x1;
    bool phase_tag = (log_entry->status_field >> 15) & 0x1;

    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("--- Error Log Entry #%d ---\n", entry_number);
    style_reset();

    printf("  %-25s : %llu\n", "Error Count", (unsigned long long)log_entry->error_count);
    printf("  %-25s : %u\n", "Submission Queue ID", log_entry->sqid);
    printf("  %-25s : 0x%04X\n", "Command ID", log_entry->cmdid);
    
    style_set_fg(COLOR_YELLOW);
    printf("  %-25s : 0x%04X\n", "Status Field", log_entry->status_field);
    style_reset();

    // Decoding Status Field
    printf("    Phase Tag (P)         : %u\n", phase_tag);
    printf("    More (M)              : %u\n", more);
    printf("    Status Code Type (SCT): %u\n", status_code_type);
    printf("    Status Code (SC)      : 0x%03X\n", status_code);


    printf("  %-25s : 0x%016llX\n", "LBA", (unsigned long long)log_entry->lba);
    printf("  %-25s : %u\n", "Namespace", log_entry->nsid);
    printf("  %-25s : 0x%04X\n", "Parameter Error Location", log_entry->param_error_loc);
    printf("  %-25s : %u\n", "Vendor Specific Info", log_entry->vendor_specific_info_avail);
    printf("\n");
}

void ui_init(void) {
    style_init();
    printf("\x1b[2J\x1b[?25l");
    fflush(stdout);
}

void ui_cleanup(void) {
    printf("\x1b[?25h");
    style_reset();
    fflush(stdout);
}

void ui_draw_scan_progress(const scan_state_t* state, const BasicDriveInfo* drive_info) {
    int term_width, term_height;
    if (pal_get_terminal_size(&term_width, &term_height) != PAL_STATUS_SUCCESS) {
        term_width = 80;
    }

    printf("\x1b[H");
    // Título
    style_set_bold();
    printf("DiskOracle v1.0 - Surface Scan\n");
    style_reset();
    printf("Target: %s (%s)\n\n", drive_info->path, drive_info->model);

    // Barra de Progresso 
    double percentage = state->total_blocks > 0 ? (double)state->scanned_blocks / state->total_blocks : 0;
    int bar_width = term_width - 10; // Deixa espaço para " [ 75.5% ]"
    int progress_chars = (int)(percentage * bar_width);

    printf("[");
    style_set_bg(COLOR_GREEN);
    for (int i = 0; i < progress_chars; ++i) printf(" ");
    style_reset();
    for (int i = 0; i < bar_width - progress_chars; ++i) printf(" ");
    printf("] %.1f%%", percentage * 100.0);
    printf("\n\n"); // Espaço extra

    //  Estatísticas
    time_t now = time(NULL);
    double elapsed_seconds = difftime(now, state->start_time);
    double eta_seconds = 0;
    if (percentage > 0.001) { 
        eta_seconds = elapsed_seconds * (1.0 - percentage) / percentage;
    }

    char elapsed_str[12], eta_str[12];
    sprintf_s(elapsed_str, sizeof(elapsed_str), "%02d:%02d:%02d", (int)(elapsed_seconds/3600), (int)(elapsed_seconds/60)%60, (int)elapsed_seconds%60);
    sprintf_s(eta_str, sizeof(eta_str), "%02d:%02d:%02d", (int)(eta_seconds/3600), (int)(eta_seconds/60)%60, (int)eta_seconds%60);

    printf(" Speed: ");
    style_set_bold();
    printf("%6.1f MB/s", state->current_speed_mbps);
    style_reset();
    printf(" | ");

    printf("Elapsed: ");
    style_set_bold();
    printf("%s", elapsed_str);
    style_reset();
    printf(" | ");
    
    printf("ETA: ");
    style_set_bold();
    printf("%s", eta_str);
    style_reset();
    printf(" | ");

    printf("Errors: ");
    style_set_bold();
    if (state->bad_blocks > 0) style_set_fg(COLOR_RED);
    printf("%llu\n", state->bad_blocks);
    style_reset();

    fflush(stdout);
}

void display_drive_list(const DriveInfo* drives, int count) {
    style_set_bold();
    printf("\n--- Available Drives ---\n");
    style_reset();

    if (count == 0) {
        printf("No physical drives found.\n");
        return;
    }

    // Print table header
    style_set_bold();
    printf("%-4s %-25s %-30s %-10s %s\n", "Idx", "Device Path", "Model", "Type", "Size (GB)");
    style_reset();
    
    for (int i = 0; i < count; ++i) {
        double size_gb = (double)drives[i].size_bytes / (1024 * 1024 * 1024);
        printf("%-4d %-25s %-30s %-10s %.2f\n",
               i,
               drives[i].device_path,
               drives[i].model,
               drives[i].type,
               size_gb);
    }
    printf("\n");
}

void ui_display_scan_report(const scan_state_t* state, const BasicDriveInfo* drive_info) {
    if (!state || !drive_info) return;

    // Calcula as estatísticas
    time_t now = time(NULL);
    double total_time = (state->start_time > 0) ? difftime(now, state->start_time) : 0;
    if (total_time < 1.0) total_time = 1.0; // Evita divisão por zero e mostra pelo menos 1 segundo

    // Desenha o relatório
    printf("\n");
    style_set_fg(COLOR_MAGENTA);
    printf("+-----------------------------------------------------------------------------+\n");
    printf("|                     The Oracle's Divination Results                         |\n");
    printf("+-----------------------------------------------------------------------------+\n");
    style_reset();

    printf("| ");
    style_set_bold();
    printf("Device Scrutinized : ");
    style_reset();
    printf("%s (%s)\n", drive_info->path, drive_info->model);
    printf("|\n");

    printf("| ");
    style_set_fg(COLOR_CYAN);
    printf("The silent watch lasted for %.0f seconds.\n", total_time);
    
    printf("| ");
    style_set_fg(COLOR_CYAN);
    printf("The Oracle peered at %llu sectors of the digital ether.\n", state->scanned_blocks);
    style_reset();
    printf("|\n");
    
    printf("| ");
    style_set_bold();
    printf("Portent of Corruption:\n");
    style_reset();

    printf("|   ");
    if (state->bad_blocks > 0) {
        style_set_fg(COLOR_BRIGHT_RED);
        printf("> %llu sectors have succumbed to the creeping decay.\n", state->bad_blocks);
    } else {
        style_set_fg(COLOR_BRIGHT_GREEN);
        printf("> 0 sectors were found to be lost to the void.\n");
    }
    style_reset();
    printf("|\n");

    printf("| ");
    style_set_bold();
    printf("Final Verdict:\n");
    style_reset();

    printf("|   ");
    if (state->bad_blocks > 0) {
        style_set_fg(COLOR_BRIGHT_YELLOW);
        printf("A shadow looms over this disk-spirit. Heed this warning.\n");
    } else {
        style_set_fg(COLOR_WHITE);
        printf("The disk-spirit appears vigorous and untainted.\n");
    }
    style_reset();

    style_set_fg(COLOR_MAGENTA);
    printf("+-----------------------------------------------------------------------------+\n");
    style_reset();
    // A chamada para pal_wait_for_keypress() foi removida daqui para evitar duplicidade.
    // O menu que chama esta função (run_surface_scan_interactive) agora é responsável pela pausa.
}

/**
 * @brief Imprime uma mensagem de uso curta para comandos inválidos.
 */
void print_usage(void) {
    style_set_fg(COLOR_YELLOW);
    printf("Uso: diskoracle <comando> [argumentos]\n");
    printf("Use 'diskoracle --help' para ver a lista de comandos.\n");
    style_reset();
} 

/**
 * @brief Exibe uma tabela formatada com as informações básicas de um drive.
 */
void ui_draw_drive_info(const BasicDriveInfo* drive_info) {
    if (!drive_info) return;

    printf("\n--- Drive Information ---\n");
    style_set_bold();
    printf("  %-15s: ", "Device Path");
    style_reset();
    printf("%s\n", drive_info->path);

    style_set_bold();
    printf("  %-15s: ", "Model");
    style_reset();
    printf("%s\n", drive_info->model);

    style_set_bold();
    printf("  %-15s: ", "Serial Number");
    style_reset();
    printf("%s\n", drive_info->serial);

    style_set_bold();
    printf("  %-15s: ", "Firmware");
    style_reset();
    printf("%s\n", drive_info->firmware_rev);

    style_set_bold();
    printf("  %-15s: ", "Type");
    style_reset();
    printf("%s\n", drive_info->type);

    style_set_bold();
    printf("  %-15s: ", "Bus Type");
    style_reset();
    printf("%s\n", drive_info->bus_type);

    style_set_bold();
    printf("  %-15s: ", "Size (GB)");
    style_reset();
    printf("%.2f GB\n", (double)drive_info->size_bytes / (1024 * 1024 * 1024));

    printf("\n");
    fflush(stdout);
}