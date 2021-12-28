#include "common_headers.h"
#include <locale.h>
#include "commands.h"
#include "pal.h"
#include "style.h"
#include "interactive.h"

#define PROJECT_VERSION "1.0.0"

static const char* disk_oracle_logotype[] = {
 "_____  _     _     ___                 _      ",
 "|  _ \\(_)___| | __/ _ \\ _ __ __ _  ___| | ___ ",
 "| | | | / __| |/ / | | | '__/ _` |/ __| |/ _ \\",
 "| |_| | \\__ \\   <| |_| | | | (_| | (__| |  __/",
 "|____/|_|___/_|\\_\\\\___/|_|  \\__,_|\\___|_|\\___|",
    NULL
};

static const char* disk_oracle_subtitle_text[] = {
    "All your files are temporarily permanent.",
    "Developed by Augusto Cesar Perin",
    NULL
};

static scan_state_t g_final_scan_state;

void print_welcome_screen(void) {
    const int term_width = 80;
    const int logo_width = 48; 
    int padding;

    style_set_fg(COLOR_MAGENTA);
    padding = (term_width - logo_width) / 2;
    for (int i = 0; disk_oracle_logotype[i] != NULL; i++) {
        printf("%*s%s\n", padding, "", disk_oracle_logotype[i]);
    }
    
    printf("\n");

    style_set_fg(COLOR_WHITE);
    padding = (term_width - (int)strlen(disk_oracle_subtitle_text[0])) / 2;
    printf("%*s%s\n", padding, "", disk_oracle_subtitle_text[0]);
    
    printf("\n\n"); 

    style_set_fg(COLOR_DIM);
    const char* author_text = disk_oracle_subtitle_text[1];
    int logo_right_edge = ((term_width - logo_width) / 2) + logo_width;
    padding = logo_right_edge - (int)strlen(author_text);
    printf("%*s%s\n", padding, "", author_text);

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
    printf("--error-log");
    style_reset();
    printf(" ");
    style_set_fg(COLOR_DIM);
    printf("<device_path>\n");
    style_reset();
    printf("    Commands the Oracle to decipher the disk's chronicle of past errors, revealing its deepest scars.\n\n");

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
    fprintf(stderr, "Commands: --list-drives, --surface, --smart, --smart-json, --error-log, --help\n");
    fprintf(stderr, "Try 'diskoracle --help' for more details.\n");
}

int handle_error_log_wrapper(int argc, char* argv[]) {
    if (argc < 3) {
        style_set_fg(COLOR_BRIGHT_YELLOW);
        fprintf(stderr, "The Oracle requires a device path to read its chronicle of errors.\n");
        style_reset();
        fprintf(stderr, "Usage: diskoracle --error-log <device_path>\n");
            return 1;
        }
    handle_error_log_command(argv[2]);
    return 0;
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
    {"--error-log",     handle_error_log_wrapper},
    {"--help",          handle_help},
    {NULL, NULL}  
};

int main(int argc, char* argv[]) {
#ifdef _WIN32
    #if defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    #endif
    setlocale(LC_ALL, ".UTF8");
#endif

    style_init();

    if (!pal_is_running_as_admin()) {
        const char* title = "Administrator Privileges Required";
        const char* line1 = "DiskOracle needs elevated permissions to access low-level hardware information.";
        const char* line2 = "Please restart this terminal as an Administrator.";
        
        int term_width = 80;
        int padding;

        style_set_fg(COLOR_BRIGHT_RED);
        padding = (term_width - (int)strlen(title)) / 2;
        printf("\n%*s%s\n\n", padding, "", title);

        style_set_fg(COLOR_WHITE);
        padding = (term_width - (int)strlen(line1)) / 2;
        printf("%*s%s\n", padding, "", line1);
        padding = (term_width - (int)strlen(line2)) / 2;
        printf("%*s%s\n\n", padding, "", line2);

        style_reset();
                return 1;
            }

    clock_t start_time = clock();

    if (argc > 1) {
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