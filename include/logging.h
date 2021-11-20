#ifndef LOGGING_H
#define LOGGING_H

int log_event(const char *event);

// Debug logging macro
#ifdef DEBUG
    // Temporarily changed to printf (stdout) for visibility testing
    #define DEBUG_PRINT(fmt, ...) printf("[DEBUG] %s:%d:%s(): " fmt "\n", \
                                          __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) // Disabled when DEBUG is not defined
#endif

// Verbose debug logging macro
// The 'context' parameter from calls will be ignored by this simple definition.
#if defined(DEBUG) && defined(VERBOSE_DEBUG_LOG) 
    #define DEBUG_PRINT_VERBOSE(context, fmt, ...) printf("[VERBOSE_DEBUG] %s:%d:%s(): " fmt "\n", \
                                               __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT_VERBOSE(context, fmt, ...) ((void)0) // Defined as no-op if not active
#endif

#endif
