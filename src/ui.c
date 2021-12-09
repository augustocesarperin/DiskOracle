#include "ui.h"
#include "style.h"
#include "pal.h"
#include "surface.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

void ui_init(void) {
    style_init();
    // Limpar a tela e esconder o cursor
    printf("\x1b[2J\x1b[?25l");
    fflush(stdout);
}

void ui_cleanup(void) {
    // Mostrar o cursor e resetar o estilo
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
    // --- Título ---
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

    // --- Estatísticas ---
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
    printf("\n--- Surface Scan Report ---\n");
    style_set_bold();
    printf("Device: %s (%s - %s)\n", drive_info->path, drive_info->model, drive_info->serial);
    style_reset();

    time_t now = time(NULL);
    double total_time = difftime(now, state->start_time);

    printf("Scan Time:    %.0f seconds\n", total_time);
    printf("Blocks Scanned: %llu\n", state->scanned_blocks);
    printf("Bad Blocks:   ");
    if (state->bad_blocks > 0) style_set_fg(COLOR_RED);
    printf("%llu\n", state->bad_blocks);
    style_reset();
    printf("---------------------------\n");
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
    printf("  %-15s: ", "Serial");
    style_reset();
    printf("%s\n", drive_info->serial);

    style_set_bold();
    printf("  %-15s: ", "Firmware");
    style_reset();
    printf("%s\n", drive_info->firmware_rev);

    style_set_bold();
    printf("  %-15s: ", "Bus Type");
    style_reset();
    printf("%s\n", drive_info->bus_type);
    
    printf("-------------------------\n");
    fflush(stdout);
} 