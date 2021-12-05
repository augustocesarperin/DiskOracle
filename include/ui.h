#ifndef UI_H
#define UI_H

#include "info.h"
#include "surface.h" // Para scan_state_t
#include <stdint.h>
#include <time.h>

// Função para inicializar o ambiente da UI (ex: limpar tela, esconder cursor)
void ui_init(void);

// Função para encerrar a UI e restaurar o terminal
void ui_cleanup(void);

// Função principal para desenhar a interface de scan
void ui_draw_scan_progress(const scan_state_t* state, const BasicDriveInfo* drive_info);

// Função para exibir o relatório final do scan
void ui_display_scan_report(const scan_state_t* state, const BasicDriveInfo* drive_info);

#endif // UI_H 