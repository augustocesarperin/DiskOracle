#ifndef UI_H
#define UI_H

#include "pal.h"
#include "info.h" 
#include "surface.h"


/**
 * @brief Exibe a tela de boas-vindas com a arte ASCII.
 */
void print_welcome_screen(void);

/**
 * @brief Exibe a mensagem de ajuda completa.
 */
void print_full_help(void);

/**
 * @brief Exibe uma mensagem curta de uso (para erros de comando).
 */
void print_usage(void);

/**
 * @brief Exibe uma lista formatada de drives.
 *
 * @param drives Um array de estruturas DriveInfo.
 * @param drive_count O número de drives no array.
 */
void display_drive_list(const DriveInfo* drives, int drive_count);

/**
 * @brief Exibe uma tabela formatada com as informações básicas de um drive.
 *
 * @param drive_info Um ponteiro para a estrutura BasicDriveInfo.
 */
void ui_draw_drive_info(const BasicDriveInfo* drive_info);

void ui_draw_scan_progress(const scan_state_t* state, const BasicDriveInfo* drive_info);
void ui_display_scan_report(const scan_state_t* state, const BasicDriveInfo* drive_info);
void ui_display_error_log_entry(const NVMeErrorLogEntry* log_entry, int entry_number);

void ui_init(void);
void ui_cleanup(void);

#endif // UI_H 