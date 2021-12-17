#ifndef STYLE_H
#define STYLE_H

#include <stdbool.h>

// Enum para cores básicas para facilitar o uso
typedef enum {
    COLOR_DEFAULT,
    COLOR_BLACK,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_BLUE,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_WHITE,
    COLOR_BRIGHT_BLACK,
    COLOR_BRIGHT_RED,
    COLOR_BRIGHT_GREEN,
    COLOR_BRIGHT_YELLOW,
    COLOR_BRIGHT_BLUE,
    COLOR_BRIGHT_MAGENTA,
    COLOR_BRIGHT_CYAN,
    COLOR_BRIGHT_WHITE,
    COLOR_DIM
} term_color_t;

/**
 * @brief Inicializa o sistema de estilo.
 *
 * Deve ser chamado no início do programa. No Windows, tenta habilitar o
 * processamento de terminal virtual. Define se o estilo está habilitado.
 */
void style_init(void);

/**
 * @brief Verifica se a estilização está habilitada.
 *
 * @return true se a estilização (cores, etc.) estiver ativa, false caso contrário.
 */
bool style_is_enabled(void);

/**
 * @brief Define a cor do texto (foreground) no terminal.
 *
 * @param color A cor desejada da enumeração term_color_t.
 */
void style_set_fg(term_color_t color);

/**
 * @brief Define a cor do fundo (background) no terminal.
 *
 * @param color A cor desejada da enumeração term_color_t.
 */
void style_set_bg(term_color_t color);

/**
 * @brief Ativa o modo de texto em negrito/brilhante.
 */
void style_set_bold(void);

/**
 * @brief Reseta toda a formatação de texto para o padrão do terminal.
 *
 * Essencial para chamar após usar qualquer outra função de estilo para
 * evitar que a formatação "vaze" para o resto do terminal.
 */
void style_reset(void);

#endif // STYLE_H 