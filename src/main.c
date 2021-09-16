/*
 *  ____  _     _    ___                 _      
 * |  _ \(_)___| | _/ _ \ _ __ __ _  ___| | ___ 
 * | | | | / __| |/ / | | | '__/ _` |/ __| |/ _ \
 * | |_| | \__ \   <| |_| | | | (_| | (__| |  __/
 * |____/|_|___/_|\_\\___/|_|  \__,_|\___|_|\___|
 *                                      
 * Hard Drive Diagnostic & Prediction Tool v0.5.4
 * "Telling you when your drive is dying, hopefully."
 * 
 * Created by: Augusto César Perin
 * Open source under MIT License
 * © 2020
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <strsafe.h>

// Removed explicit GUID definition and related includes if they were solely for that purpose.
// #include <windows.h> 
// #include <devguid.h> 
// DEFINE_GUID(GUID_DEVINTERFACE_DISK, ...);

#include "pal.h"     
#include "smart.h"   
#include "surface.h" 
#include "info.h"    
#include "logging.h"
#include "report.h"  
#include "nvme_hybrid.h" // Para nvme_health_alerts_t, nvme_analyze_health_alerts
#include "nvme_export.h" // Para nvme_export_to_json e export_format_t

// Declarations for surface_scan and display_drive_info are in their respective .h files.
// The report_generate declaration is in report.h.

static export_format_t g_export_format = EXPORT_FORMAT_UNKNOWN; // Novo global
static char g_output_file_path[MAX_PATH] = {0};       // Novo global

void print_usage(const char *app_name) {
    printf("  ____  _     _    ___                 _      \n");
    printf(" |  _ \\(_)___| | _/ _ \\ _ __ __ _  ___| | ___ \n");
    printf(" | | | | / __| |/ / | | | '__/ _` |/ __| |/ _ \\\n");
    printf(" | |_| | \\__ \\   <| |_| | | | (_| | (__| |  __/\n");
    printf(" |____/|_|___/_|\\_\\\\___/|_|  \\__,_|\\___|_|\\___|\n");
    printf("                                      \n");
    printf(" Hard Drive Diagnostic & Prediction Tool v0.5.4\n");
    printf(" \"Telling you when your drive is dying, hopefully.\"\n");
    printf(" Developed by Augusto Cesar Perin - 2020\n\n");
    printf("HOW TO ROLL:\n");
    printf("  %s <command> [args_if_any]\n\n", app_name);
    printf("THE COMMAND LINEUP:\n");
    printf("  --list-drives                  Shows all your physical disk buddies and where to find 'em.\n");
    printf("                                 Hint: The 'Device Path' shown here is your golden ticket for other commands.\n\n");
    printf("  --smart <device_path>          Fetches and decodes SMART data for the chosen drive.\n\n");
    printf("  --surface <device_path> [--type <quick|deep>]\n");
    printf("                                 Scans the disk surface for any gremlins hiding out.\n");
    printf("                                 Defaults to a 'quick' once-over. \n");
    printf("                                 'deep' is the full detective mode, might take a bit.\n\n");
    printf("  --info <device_path>           Get the lowdown: detailed intel on the specified drive.\n\n");
    printf("  --report <device_path> [--format <json|xml|csv>] [--output <filename>]\n");
    printf("                                 Generates a health report for the specified drive.\n");
    printf("                                 Default format: json. Default output: reports/device_timestamp.format\n\n");
    printf("  --help                         Lost in space? This command is your map! :)\n\n");
    printf("  --export <format>      Export data to specified format (json|xml|csv|html)\n");
    printf("  --output <filepath>    Specify output file for export (default: stdout for some formats if not specified)\n");
    printf("SHOW ME THE ACTION (EXAMPLES):\n");
    printf("  %s --list-drives\n", app_name);
    printf("  %s --smart /dev/sda\n", app_name);
    printf("  %s --smart \\\\.\\PhysicalDrive0\n", app_name);
    printf("  %s --surface /dev/sdb --type deep\n", app_name);
    printf("  %s --info /dev/sda\n", app_name);
    printf("  %s --report /dev/sdc --format csv\n", app_name);
    printf("  %s --report /dev/sdc --output my_custom_report.json\n", app_name);
}

// Função para imprimir alertas de saúde
void print_health_alerts(const nvme_health_alerts_t* health_alerts) {
    if (health_alerts->alert_count > 0) {
        printf("\n-----------------------------------------------------------------\n");
        printf("\n⚠️ Health Alerts Found (%d):\n", health_alerts->alert_count);
        printf("-----------------------------------------------------------------\n");
        for (int i = 0; i < health_alerts->alert_count; ++i) {
            const nvme_alert_info_t* alert = &health_alerts->alerts[i];
            printf("  [%s] %s: %s (Threshold: %s)\n", 
                   alert->is_critical ? "CRITICAL" : "WARNING ", 
                   alert->description, 
                   alert->current_value_str, 
                   alert->threshold_str);
        }
        printf("-----------------------------------------------------------------\n");
    } else {
        printf("\n✅ No critical health alerts detected based on current thresholds.\n");
    }
}

void parse_tool_options(int argc, char *argv[]) {
    // Esta função será chamada no início do main para PREENCHER g_export_format e g_output_file_path
    // mas NÃO deve consumir os argumentos de argc/argv da perspectiva do loop principal de comandos.
    // O loop principal ainda usará argc/argv originais para identificar o comando principal.
    // Esta função apenas "dá uma espiada" nas opções.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--export") == 0) {
            if (i + 1 < argc) {
                // Não incrementar 'i' aqui para não afetar o loop do main
                if (strcmp(argv[i+1], "json") == 0) g_export_format = EXPORT_FORMAT_JSON;
                else if (strcmp(argv[i+1], "xml") == 0) {g_export_format = EXPORT_FORMAT_XML; fprintf(stderr, "Warning: XML export not fully implemented.\n");}
                else if (strcmp(argv[i+1], "csv") == 0) {g_export_format = EXPORT_FORMAT_CSV; fprintf(stderr, "Warning: CSV export not fully implemented.\n");}
                else if (strcmp(argv[i+1], "html") == 0) {g_export_format = EXPORT_FORMAT_HTML; fprintf(stderr, "Warning: HTML export not fully implemented.\n");}
                else { fprintf(stderr, "Error: Unknown export format '%s'\n", argv[i+1]); print_usage(argv[0]); exit(1);}
            } else { fprintf(stderr, "Error: --export requires a format.\n"); print_usage(argv[0]); exit(1); }
        } else if (strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                // Não incrementar 'i' aqui
                StringCchCopyA(g_output_file_path, MAX_PATH, argv[i+1]);
            } else { fprintf(stderr, "Error: --output requires a file path.\n"); print_usage(argv[0]); exit(1); }
        }
    }
}

int main(int argc, char *argv[]) {
    if (pal_initialize() != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Fatal: Failed to initialize platform abstraction layer.\n");
        // log_init/shutdown not called here as PAL failed at the very start.
        return 1; 
    }

    // Attempt to create directories needed by the application.
    // These calls will succeed silently if directories already exist (as per PAL_STATUS_ALREADY_EXISTS).
    int reports_dir_status = pal_create_directory("reports");
    if (reports_dir_status != PAL_STATUS_SUCCESS && reports_dir_status != PAL_STATUS_ALREADY_EXISTS) {
        // Non-fatal warning, report_generate might still work if output_filename_override is used to a different path.
        fprintf(stderr, "Warning: Could not create or access 'reports' directory (Status: %d). Report generation to default path might fail.\n", reports_dir_status);
    }
    
    int logs_dir_status = pal_create_directory("logs");
    if (logs_dir_status != PAL_STATUS_SUCCESS && logs_dir_status != PAL_STATUS_ALREADY_EXISTS) {
         fprintf(stderr, "Warning: Could not create or access 'logs' directory (Status: %d). Logging to file might fail.\n", logs_dir_status);
    }
    
    log_init(NULL); // Initialize logging system (uses "logs" by default if NULL)
    
    log_event("Diskoracle application started.");
    parse_tool_options(argc, argv); // Parsear opções globais PRIMEIRO. Elas preenchem g_export_format, g_output_file_path.

    if (argc < 2) {
        print_usage(argv[0]);
        log_shutdown();
        pal_cleanup();
        return 0;
    }

    int return_code = 0; 
    const char* command = argv[1];
    const char* device_path = NULL;
    int main_command_arg_count = 0; // Para contar quantos args o comando principal consome

    if (strcmp(command, "--help") == 0) {
        main_command_arg_count = 1;
        print_usage(argv[0]);
    } else if (strcmp(command, "--list-drives") == 0) {
        main_command_arg_count = 1;
            pal_list_drives();
    } else if (strcmp(command, "--smart") == 0 || 
               strcmp(command, "--info") == 0 || 
               strcmp(command, "--surface") == 0 ||
               strcmp(command, "--report") == 0) {
        
        if (argc > 2 && argv[2] != NULL && argv[2][0] != '-') {
            device_path = argv[2];
            main_command_arg_count = 2; // --comando + device_path
        } else {
            fprintf(stderr, "Error: Command '%s' requires a <device_path>.\n", command);
            print_usage(argv[0]);
            return_code = 1;
            goto end_main;
        }

        if (strcmp(command, "--smart") == 0) {
            struct smart_data s_data;
            BasicDriveInfo b_info;
            nvme_hybrid_context_t hybrid_context_details; // Declarar aqui
            
            ZeroMemory(&s_data, sizeof(struct smart_data));
            ZeroMemory(&b_info, sizeof(BasicDriveInfo));
            // ZeroMemory para hybrid_context_details é feito dentro de pal_get_smart ao inicializar

            BOOL basic_info_ok = FALSE;
            // Obter BasicDriveInfo se a exportação estiver ativa, para a seção deviceInfo do JSON
            if (g_export_format != EXPORT_FORMAT_UNKNOWN) {
                basic_info_ok = pal_get_basic_drive_info(device_path, &b_info);
            }
            
            // Chamar pal_get_smart com o ponteiro para hybrid_context_details
            pal_status_t smart_status = pal_get_smart(device_path, &s_data, &hybrid_context_details);

            if (smart_status == PAL_STATUS_SUCCESS) {
                // smart_interpret (chamado por report_generate) ou uma chamada direta aqui imprime os dados no console.
                // A chamada report_print_smart_data foi removida para evitar erro de vinculação.
                // Se a saída padrão de smart_interpret não for suficiente para o comando --smart sozinho,
                // precisaremos garantir que smart_interpret seja chamado aqui ou que report_print_smart_data seja restaurada e definida.
                // Por enquanto, o foco é na exportação JSON.

                nvme_health_alerts_t health_alerts = {0};
                if (s_data.is_nvme) {
                    nvme_analyze_health_alerts(&s_data.data.nvme, &health_alerts, s_data.data.nvme.spare_thresh);
                    print_health_alerts(&health_alerts); // Imprime alertas no console
                }

                // Lógica de Exportação
                if (g_export_format == EXPORT_FORMAT_JSON) {
                    fprintf(stderr, "[INFO MAIN] Exporting data to JSON... Output file: %s\n", 
                            (strlen(g_output_file_path) > 0) ? g_output_file_path : "stdout");
                    nvme_export_to_json(device_path, 
                                        basic_info_ok ? &b_info : NULL, 
                                        &s_data, 
                                        (s_data.is_nvme ? &health_alerts : NULL), 
                                        &hybrid_context_details, // Passar o contexto preenchido
                                        (strlen(g_output_file_path) > 0) ? g_output_file_path : NULL);
                } else if (g_export_format != EXPORT_FORMAT_UNKNOWN) {
                    fprintf(stderr, "Export format '%d' selected but not yet implemented for full data export.\n", g_export_format);
                }

            } else { 
                fprintf(stderr, "Error: DiskOracle couldn't fetch SMART data for %s. PAL Status: %d. Effective method: %s, Error: %lu\n", 
                    device_path, smart_status, 
                    hybrid_context_details.last_operation_result.method_name, // Usar o contexto para log de erro
                    hybrid_context_details.last_operation_result.error_code);
                return_code = 1; 
            }
        } else if (strcmp(command, "--info") == 0) {
            display_drive_info(device_path); 
        } else if (strcmp(command, "--surface") == 0) {
            const char *scan_type = "quick"; 
            // Para --surface, precisamos parsear --type SE ele existir APÓS device_path
            // e ANTES de quaisquer opções globais como --export.
            int current_arg_idx = 3; 
            if (current_arg_idx < argc && strcmp(argv[current_arg_idx], "--type") == 0) {
                if (current_arg_idx + 1 < argc) {
                    scan_type = argv[current_arg_idx + 1];
                    main_command_arg_count += 2; 
                } else {
                    fprintf(stderr, "Error: --type option for --surface requires a value (quick|deep).\n");
                    print_usage(argv[0]); return_code = 1; goto end_main;
                }
            }
            if (strcmp(scan_type, "quick") != 0 && strcmp(scan_type, "deep") != 0) {
                 fprintf(stderr, "Error: Invalid scan type '%s' for '--surface'. Choose 'quick' or 'deep'.\n", scan_type);
                 print_usage(argv[0]); return_code = 1; goto end_main;
            }
            
            SurfaceScanResult scan_result; 
            ZeroMemory(&scan_result, sizeof(SurfaceScanResult)); 

            log_event_detailed("Surface scan initiated.", "Device", device_path, "Type", scan_type);
            int scan_status = surface_scan(device_path, scan_type, &scan_result); // Passar o endereço
            log_event_detailed("Surface scan completed.", "Device", device_path, "Status", scan_status == 0 ? "Success" : "Failed");

            printf("\n--- Surface Scan Report for %s (%s) ---\n", device_path, scan_type);
            if (scan_result.scan_performed) {
                printf("Scan Performed: Yes\n");
                printf("Status Message: %s\n", scan_result.status_message);
                printf("Sectors Scanned: %llu\n", (unsigned long long)scan_result.total_sectors_scanned);
                printf("Bad Sectors Found: %llu\n", (unsigned long long)scan_result.bad_sectors_found);
                printf("Read Errors (other): %llu\n", (unsigned long long)scan_result.read_errors);
                printf("Scan Time: %.2f seconds\n", scan_result.scan_time_seconds);
            } else {
                printf("Scan Performed: No\n");
                printf("Status Message: %s\n", scan_result.status_message);
            }
            printf("--------------------------------------------------\n");

            if (scan_status != 0) {
                fprintf(stderr, "Error during surface scan for %s. Status: %d\n", device_path, scan_status);
                // return_code = 1; // Decidir se isso deve ser um erro fatal para o programa
            }
            // surface_scan(device_path, scan_type); 

        } else if (strcmp(command, "--report") == 0) {
            const char *report_format = "json"; 
            const char *output_file = NULL;
            int current_arg_idx = 3; 
            while(current_arg_idx < argc) {
                if (strcmp(argv[current_arg_idx], "--format") == 0 && (current_arg_idx + 1) < argc) {
                    report_format = argv[current_arg_idx+1];
                    main_command_arg_count += 2;
                    current_arg_idx += 2;
                } else if (strcmp(argv[current_arg_idx], "--output") == 0 && (current_arg_idx + 1) < argc) {
                    // Se for o --output global, já foi pego. Se for específico do report, precisa de lógica diferente.
                    // Para evitar conflito com o g_output_file_path global, o --report pode não ter seu próprio --output
                    // ou precisaria talvez de uma forma de distinguir. por enquanto fica assumido que o --output global é usado.
                    // Se o --output aqui é para o report_generate, não deve ser um globa
                    // Esta seção precisa ser mais clarasobre o output local vs global...
                    // ignorar --output local para --report por enquanto e usar o global
                    current_arg_idx += 2; 
                } else {
                    break; // Próximo arg não é --format nem --output, deve ser opção global ou erro
                }
            }
            // Usar g_output_file_path se output_file não foi definido localmente.
            if (strlen(g_output_file_path) > 0) output_file = g_output_file_path;
            report_generate(device_path, report_format, output_file);
        }
    } else {
        fprintf(stderr, "Error: Unknown command: '%s'. Use '--help'.\n", command);
        print_usage(argv[0]);
        return_code = 1;
    }
    // Verificação de argumentos extras não consumidos (após opções globais e de comando)
    // A contagem de argumentos consumidos pelas opções globais é mais complexa de rastrear precisamente aqui

end_main:
    log_event("Diskoracle application ended.");
    log_shutdown();
    pal_cleanup(); 
    return return_code;
}
