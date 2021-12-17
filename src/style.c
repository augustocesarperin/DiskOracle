#include "style.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define ANSI_RESET          "\x1b[0m"
#define ANSI_BOLD           "\x1b[1m"
#define ANSI_DIM            "\x1b[2m"
#define ANSI_FG_BLACK       "\x1b[30m"
#define ANSI_FG_RED         "\x1b[31m"
#define ANSI_FG_GREEN       "\x1b[32m"
#define ANSI_FG_YELLOW      "\x1b[33m"
#define ANSI_FG_BLUE        "\x1b[34m"
#define ANSI_FG_MAGENTA     "\x1b[35m"
#define ANSI_FG_CYAN        "\x1b[36m"
#define ANSI_FG_WHITE       "\x1b[37m"
#define ANSI_FG_BRIGHT_BLACK   "\x1b[90m"
#define ANSI_FG_BRIGHT_RED     "\x1b[91m"
#define ANSI_FG_BRIGHT_GREEN   "\x1b[92m"
#define ANSI_FG_BRIGHT_YELLOW  "\x1b[93m"
#define ANSI_FG_BRIGHT_BLUE    "\x1b[94m"
#define ANSI_FG_BRIGHT_MAGENTA "\x1b[95m"
#define ANSI_FG_BRIGHT_CYAN    "\x1b[96m"
#define ANSI_FG_BRIGHT_WHITE   "\x1b[97m"
#define ANSI_BG_BLACK       "\x1b[40m"
#define ANSI_BG_RED         "\x1b[41m"
#define ANSI_BG_GREEN       "\x1b[42m"
#define ANSI_BG_YELLOW      "\x1b[43m"
#define ANSI_BG_BLUE        "\x1b[44m"
#define ANSI_BG_MAGENTA     "\x1b[45m"
#define ANSI_BG_CYAN        "\x1b[46m"
#define ANSI_BG_WHITE       "\x1b[47m"
#define ANSI_BG_BRIGHT_BLACK   "\x1b[100m"
#define ANSI_BG_BRIGHT_RED     "\x1b[101m"
#define ANSI_BG_BRIGHT_GREEN   "\x1b[102m"
#define ANSI_BG_BRIGHT_YELLOW  "\x1b[103m"
#define ANSI_BG_BRIGHT_BLUE    "\x1b[104m"
#define ANSI_BG_BRIGHT_MAGENTA "\x1b[105m"
#define ANSI_BG_BRIGHT_CYAN    "\x1b[106m"
#define ANSI_BG_BRIGHT_WHITE   "\x1b[107m"


static bool g_style_enabled = false;

void style_init(void) {
    #ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        g_style_enabled = false;
        return;
    }

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) {
        g_style_enabled = false;
        return;
    }

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, dwMode)) {
        g_style_enabled = false;
        return;
    }
    #endif

    g_style_enabled = true;
}

bool style_is_enabled(void) {
    return g_style_enabled;
}

static void set_color(term_color_t color, const char* const* fg_codes, const char* const* bg_codes) {
    if (!g_style_enabled) return;
    
    const char* const* codes = (fg_codes != NULL) ? fg_codes : bg_codes;
    if (codes == NULL) return;

    printf("%s", codes[color]);
}

void style_set_fg(term_color_t color) {
    if (!g_style_enabled) return;
    const char* fg_codes[] = {
        [COLOR_DEFAULT] = "", [COLOR_BLACK] = ANSI_FG_BLACK, [COLOR_RED] = ANSI_FG_RED, 
        [COLOR_GREEN] = ANSI_FG_GREEN, [COLOR_YELLOW] = ANSI_FG_YELLOW, [COLOR_BLUE] = ANSI_FG_BLUE,
        [COLOR_MAGENTA] = ANSI_FG_MAGENTA, [COLOR_CYAN] = ANSI_FG_CYAN, [COLOR_WHITE] = ANSI_FG_WHITE,
        [COLOR_BRIGHT_BLACK] = ANSI_FG_BRIGHT_BLACK, [COLOR_BRIGHT_RED] = ANSI_FG_BRIGHT_RED,
        [COLOR_BRIGHT_GREEN] = ANSI_FG_BRIGHT_GREEN, [COLOR_BRIGHT_YELLOW] = ANSI_FG_BRIGHT_YELLOW,
        [COLOR_BRIGHT_BLUE] = ANSI_FG_BRIGHT_BLUE, [COLOR_BRIGHT_MAGENTA] = ANSI_FG_BRIGHT_MAGENTA,
        [COLOR_BRIGHT_CYAN] = ANSI_FG_BRIGHT_CYAN, [COLOR_BRIGHT_WHITE] = ANSI_FG_BRIGHT_WHITE,
        [COLOR_DIM] = ANSI_DIM
    };
    printf("%s", fg_codes[color]);
}

void style_set_bg(term_color_t color) {
    if (!g_style_enabled) return;
    const char* bg_codes[] = {
        [COLOR_DEFAULT] = "", [COLOR_BLACK] = ANSI_BG_BLACK, [COLOR_RED] = ANSI_BG_RED, 
        [COLOR_GREEN] = ANSI_BG_GREEN, [COLOR_YELLOW] = ANSI_BG_YELLOW, [COLOR_BLUE] = ANSI_BG_BLUE,
        [COLOR_MAGENTA] = ANSI_BG_MAGENTA, [COLOR_CYAN] = ANSI_BG_CYAN, [COLOR_WHITE] = ANSI_BG_WHITE,
        [COLOR_BRIGHT_BLACK] = ANSI_BG_BRIGHT_BLACK, [COLOR_BRIGHT_RED] = ANSI_BG_BRIGHT_RED,
        [COLOR_BRIGHT_GREEN] = ANSI_BG_BRIGHT_GREEN, [COLOR_BRIGHT_YELLOW] = ANSI_BG_BRIGHT_YELLOW,
        [COLOR_BRIGHT_BLUE] = ANSI_BG_BRIGHT_BLUE, [COLOR_BRIGHT_MAGENTA] = ANSI_BG_BRIGHT_MAGENTA,
        [COLOR_BRIGHT_CYAN] = ANSI_BG_BRIGHT_CYAN, [COLOR_BRIGHT_WHITE] = ANSI_BG_BRIGHT_WHITE
    };
    printf("%s", bg_codes[color]);
}

void style_set_bold(void) {
    if (g_style_enabled) {
        printf("%s", ANSI_BOLD);
    }
}

void style_reset(void) {
    if (g_style_enabled) {
        printf("%s", ANSI_RESET);
    }
} 