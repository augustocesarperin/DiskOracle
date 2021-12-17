#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>   
#include "../include/interactive.h"
#include "pal.h"
#include "ui.h"
#include "info.h"
#include "style.h"
#include "surface.h" 


typedef enum {
    STATE_EXIT,
    STATE_DRIVE_SELECTION,
    STATE_ACTION_SELECTION
} interactive_state_t;

static interactive_state_t display_drive_selection_menu(DriveInfo* selected_drive);
static interactive_state_t display_action_menu(const DriveInfo* drive);
static void run_surface_scan_interactive(const DriveInfo* drive);
static void interactive_scan_callback(const scan_state_t* state, void* user_data);

/**
 * @brief Ponto de entrada e máquina de estados para o modo interativo.
 */
int start_interactive_mode(void) {
    interactive_state_t current_state = STATE_DRIVE_SELECTION;
    DriveInfo selected_drive;
    memset(&selected_drive, 0, sizeof(DriveInfo));

    while (current_state != STATE_EXIT) {
        switch (current_state) {
            case STATE_DRIVE_SELECTION:
                current_state = display_drive_selection_menu(&selected_drive);
                break;
            case STATE_ACTION_SELECTION:
                current_state = display_action_menu(&selected_drive);
                break;
            case STATE_EXIT:
                break;
        }
    }

    style_set_fg(COLOR_MAGENTA);
    printf("The Oracle falls silent.\n");
    style_reset();
    return 0;
}

/**
 * @brief Exibe o menu de seleção de drives e processa a entrada do usuário.
 * @param selected_drive Ponteiro para armazenar o drive selecionado.
 * @return O próximo estado para a máquina de estados.
 */
static interactive_state_t display_drive_selection_menu(DriveInfo* selected_drive) {
    pal_clear_screen();
    print_welcome_screen();

    DriveInfo drives[MAX_DRIVES];
    int drive_count = 0;
    pal_status_t status = pal_list_drives(drives, MAX_DRIVES, &drive_count);

    if (status != PAL_STATUS_SUCCESS || drive_count == 0) {
        style_set_fg(COLOR_BRIGHT_RED);
        printf("Oracle's Whisper: %s\n", pal_get_error_string(status != PAL_STATUS_SUCCESS ? status : PAL_STATUS_NO_DRIVES_FOUND));
        style_reset();
        pal_wait_for_keypress();
        return STATE_EXIT;
    }

    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("=== SELECT A DRIVE ===\n");
    style_reset();

    for (int i = 0; i < drive_count; i++) {
        printf("  ");
        style_set_fg(COLOR_BRIGHT_CYAN);
        printf("[%d] ", i + 1);
        style_set_fg(COLOR_WHITE);
        printf("%s (%s)\n", drives[i].model, drives[i].device_path);
    }
    style_reset();
    printf("  ");
    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("[0] ");
    style_set_fg(COLOR_WHITE);
    printf("Exit\n\n");
    
    printf("Enter drive number and press Enter: ");
    
    char input_buffer[16];
    if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
        return STATE_EXIT; // Erro de leitura
    }

    int choice = atoi(input_buffer);

    if (choice == 0) {
        return STATE_EXIT;
    }

    if (choice > 0 && choice <= drive_count) {
        *selected_drive = drives[choice - 1];
        return STATE_ACTION_SELECTION;
    }

    style_set_fg(COLOR_YELLOW);
    printf("\nThe Oracle does not understand your input. Contemplate your choice and try again.");
    style_reset();
    pal_wait_for_keypress();
    return STATE_DRIVE_SELECTION;
}

/**
 * @brief Callback para atualizar a UI durante o scan de superfície no modo interativo.
 *        Esta função é o coração do feedback em tempo real.
 */
static void interactive_scan_callback(const scan_state_t* state, void* user_data) {
    BasicDriveInfo* drive_info = (BasicDriveInfo*)user_data;
    ui_draw_scan_progress(state, drive_info);
}

/**
 * @brief Orquestra o teste de superfície a partir do modo interativo.
 *        Prepara a UI, executa o scan com feedback e limpa a UI.
 */
static void run_surface_scan_interactive(const DriveInfo* drive) {
    // 1. Obter informações básicas para exibir na tela de progresso.
    BasicDriveInfo basic_info;
    memset(&basic_info, 0, sizeof(BasicDriveInfo));
    if (pal_get_basic_drive_info(drive->device_path, &basic_info) != PAL_STATUS_SUCCESS) {
        // Se falhar, usa o que já temos para não mostrar uma tela vazia.
        strncpy_s(basic_info.path, sizeof(basic_info.path), drive->device_path, _TRUNCATE);
        strncpy_s(basic_info.model, sizeof(basic_info.model), drive->model, _TRUNCATE);
        strncpy_s(basic_info.serial, sizeof(basic_info.serial), drive->serial, _TRUNCATE);
    }
    
    // 2. Prepara o terminal para a UI (limpa a tela, esconde o cursor).
    ui_init();

    // 3. Inicia o scan, passando nossa função de callback e uma variável para o resultado.
    scan_state_t final_scan_state;
    memset(&final_scan_state, 0, sizeof(scan_state_t));
    surface_scan(drive->device_path, "quick", interactive_scan_callback, &basic_info, &final_scan_state);

    // 4. Restaura o terminal ao seu estado normal.
    ui_cleanup();

    // 5. Exibe o relatório final.
    ui_display_scan_report(&final_scan_state, &basic_info);

    printf("\n\n");
    style_set_fg(COLOR_MAGENTA);
    printf("The divination is complete. Press any key to return to the actions menu.");
    style_reset();
    pal_wait_for_keypress();
}

/**
 * @brief Exibe o menu de ações para um drive selecionado.
 * @param drive O drive para o qual exibir ações.
 * @return O próximo estado para a máquina de estados.
 */
static interactive_state_t display_action_menu(const DriveInfo* drive) {
    pal_clear_screen();
    print_welcome_screen();

    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("=== ACTIONS FOR: %s ===\n", drive->model);
    style_reset();

    printf("  ");
    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("[1] ");
    style_set_fg(COLOR_WHITE);
    printf("Start Surface Test\n");

    printf("  ");
    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("[2] ");
    style_set_fg(COLOR_WHITE);
    printf("View Detailed Information (S.M.A.R.T.)\n");

    printf("  ");
    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("[0] ");
    style_set_fg(COLOR_WHITE);
    printf("Back to Drive Selection\n\n");
    
    style_reset();
    printf("Choose an action: ");
    
    char input_buffer[16];
    if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
        return STATE_DRIVE_SELECTION; // Erro, volta ao menu anterior
    }

    int choice = atoi(input_buffer);

    switch(choice) {
        case 0:
            return STATE_DRIVE_SELECTION;
        case 1:
            // Chama o nosso novo orquestrador com a UI correta
            run_surface_scan_interactive(drive);
            return STATE_ACTION_SELECTION; // Volta para este menu de ações
        case 2:
            // Agora esta opção chama a função correta
            display_drive_info(drive->device_path);
            pal_wait_for_keypress();
            return STATE_ACTION_SELECTION;
        default:
            style_set_fg(COLOR_YELLOW);
            printf("\nThe Oracle does not understand your input. Contemplate your choice and try again.");
            style_reset();
            pal_wait_for_keypress();
            return STATE_ACTION_SELECTION;
    }
} 