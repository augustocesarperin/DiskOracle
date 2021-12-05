#include "ui.h"
#include "style.h"
#include "pal.h"
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
        // Fallback para um tamanho padrão se não conseguir obter o tamanho do terminal
        term_width = 80;
    }

    // Mover o cursor para o topo e limpar a linha para evitar artefatos
    printf("\x1b[H");

    // --- Título ---
    style_set_bold();
    printf("DiskOracle v1.0 - Surface Scan\n");
    style_reset();
    printf("Target: %s (%s)\n\n", drive_info->path, drive_info->model);

    // --- Barra de Progresso ---
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
    if (percentage > 0.001) { // Evita divisão por zero e ETA infinito no início
        eta_seconds = elapsed_seconds * (1.0 - percentage) / percentage;
    }

    // Formata o tempo (Elapsed e ETA)
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