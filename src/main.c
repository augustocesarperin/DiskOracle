#include <stdio.h>
#include <string.h>
#include <locale.h>
#include "pal.h"
#include "style.h"
#include "ui.h"
#include "interactive.h"
#include "info.h"        
#include <stdlib.h>
#include <stdbool.h>
#include "config.h"
#include "smart.h"
#include "surface.h"
#include "logging.h"
#include "report.h"
#include "../include/nvme_hybrid.h"
#include "nvme_export.h"
#include "nvme_orchestrator.h"
#include "commands.h"

#ifdef _WIN32
#include <windows.h>
#define _CRT_SECURE_NO_WARNINGS
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include <time.h> 
#endif

#define PROJECT_VERSION "1.0.0"

// "Synthwave Grid"
static const char* disk_oracle_art_sun[] = {
    "                       __",
    "                   _--\"  \"--_",
    "                 -\"         \"-",
    "                -\"             \"-",
    NULL
};

static const char* disk_oracle_art_horizon[] = {
    "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",
    "|           D I S K  :::  O R A C L E           |",
    "=================================================",
    NULL
};

static const char* disk_oracle_subtitle_text[] = {
    "\"Telling you when your drive is dying, hopefully.\"",
    "   Developed by Augusto Cesar Perin",
    NULL
};

static scan_state_t g_final_scan_state;

void print_welcome_screen(void) {
    style_set_fg(COLOR_BRIGHT_MAGENTA);
    for (int i = 0; disk_oracle_art_sun[i] != NULL; i++) {
        printf("%s\n", disk_oracle_art_sun[i]);
    }
    style_reset();

    style_set_fg(COLOR_MAGENTA);
    for (int i = 0; disk_oracle_art_horizon[i] != NULL; i++) {
        printf("%s\n", disk_oracle_art_horizon[i]);
    }
    style_reset();

    style_set_fg(COLOR_CYAN);
    for (int i = 0; disk_oracle_subtitle_text[i] != NULL; i++) {
        printf("    %s\n", disk_oracle_subtitle_text[i]);
    }
    style_reset();
    printf("\n");
}

void print_brief_usage(void);
void print_full_help(void);

/**
 * @brief Displays the full, detailed help message for the program.
 */
void print_full_help(void) {
    print_welcome_screen();

    style_set_fg(COLOR_MAGENTA);
    printf("HOW TO ROLL:\n");
    style_reset();
    printf("  diskoracle <command> [args_if_any]\n\n");

    style_set_fg(COLOR_MAGENTA); 
    printf("THE COMMAND LINEUP:\n");
    style_reset();

    printf("  ");
    style_set_fg(COLOR_MAGENTA); 
    printf("> ");
    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("--list-drives\n");
    style_reset();
    printf("    Reveals all physical disk-spirits bound to this machine.\n");
    printf("    Hint: The 'Device Path' is the true name you must use for other incantations.\n\n");

    printf("  ");
    style_set_fg(COLOR_MAGENTA); 
    printf("> ");
    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("--surface");
    style_reset();
    printf(" ");
    style_set_fg(COLOR_DIM);
    printf("<device_path>\n");
    style_reset();
    printf("    Commands the Oracle to gaze upon the disk's physical plane, seeking out weary or corrupted sectors.\n\n");

    printf("  ");
    style_set_fg(COLOR_MAGENTA); 
    printf("> ");
    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("--smart\n");
    style_reset();
    printf("    Interprets the disk's inner whispers (S.M.A.R.T.), revealing its self-diagnosed health and portents of its future.\n\n");

    printf("  ");
    style_set_fg(COLOR_MAGENTA); 
    printf("> ");
    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("--smart-json\n");
    style_reset();
    printf("    Translates the disk's whispers into the universal machine tongue of JSON.\n\n");

    printf("  ");
    style_set_fg(COLOR_MAGENTA); 
    printf("> ");
    style_set_fg(COLOR_BRIGHT_CYAN);
    printf("--help\n");
    style_reset();
    printf("    Displays this sacred manuscript, guiding your journey into the machine's depths.\n\n");

    style_set_fg(COLOR_MAGENTA); 
    printf("EXAMPLES OF POWERFUL INCANTATIONS:\n");
    style_reset();
    printf("  diskoracle --list-drives\n");
    printf("  diskoracle --surface \\\\.\\PhysicalDrive0    (Windows example)\n");
    printf("  diskoracle --surface /dev/sda              (Linux/macOS example)\n\n");
    printf("===============================================================================\n");
}

/**
 * @brief Displays a brief usage message, for command errors.
 */
void print_brief_usage(void) {
    fprintf(stderr, "Usage: diskoracle <command>\n");
    fprintf(stderr, "Commands: --list-drives, --surface, --smart, --smart-json, --help\n");
    fprintf(stderr, "Try 'diskoracle --help' for more details.\n");
}


void cleanup_platform(void);
void display_drive_info(const char *device_path);
void print_usage(void);

// Command Dispatch System
const command_t commands[] = {
    {"--list-drives",   handle_list_drives},
    {"--surface",       handle_surface_scan},
    {"--smart",         handle_smart},
    {"--smart-json",    handle_smart_json},
    {"--help",          handle_help},
    {NULL, NULL} 
};



// Entry Point
int main(int argc, char* argv[]) {
#ifdef _WIN32
    #if defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    #endif
    setlocale(LC_ALL, ".UTF8");
#endif

    clock_t start_time = clock();

    style_init();

    if (argc > 1) {
        // Modo de linha de comando
        const char* command_name = argv[1];

        for (int i = 0; commands[i].name != NULL; i++) {
            if (strcmp(command_name, commands[i].name) == 0) {
                return commands[i].handler(argc, argv);
            }
        }

        style_set_fg(COLOR_BRIGHT_RED);
        fprintf(stderr, "The Oracle does not recognize the incantation '%s'.\n", command_name);
        style_reset();
        fprintf(stderr, "Consult the sacred texts with 'diskoracle --help'.\n");
            return 1;
    } else {
        return start_interactive_mode();
    }
}