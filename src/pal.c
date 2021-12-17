#include "pal.h"
#include <stdio.h>
#include <stdlib.h>

// Implementação da PAL deve conter apenas funções INDEPENDENTES de plataforma.
// A função de tradução de erros é um exemplo perfeito.

const char* pal_get_error_string(pal_status_t status) {
    switch (status) {
        case PAL_STATUS_SUCCESS:
            return "The operation was successful, to the Oracle's mild surprise.";
        case PAL_STATUS_ERROR:
            return "A vague and unsettling error has occurred in the digital ether.";
        case PAL_STATUS_INVALID_PARAMETER:
            return "The Oracle rejects your offering. The parameters provided are flawed.";
        case PAL_STATUS_IO_ERROR:
            return "The disk-spirit is unresponsive or there was a disturbance in the data stream (I/O Error).";
        case PAL_STATUS_NO_MEMORY:
            return "The Oracle's mind is vast, but the machine's memory is finite. More is required.";
        case PAL_STATUS_DEVICE_NOT_FOUND:
            return "The Oracle searched the astral plane, but the specified device could not be found.";
        case PAL_STATUS_ACCESS_DENIED:
            return "You lack the proper incantations (or Administrator rights) to command this device.";
        case PAL_STATUS_UNSUPPORTED:
            return "The Oracle scoffs at this primitive or unknown technology. It is unsupported.";
        case PAL_STATUS_BUFFER_TOO_SMALL:
            return "The vessel is too small to contain the requested knowledge (Buffer too small).";
        case PAL_STATUS_NO_DRIVES_FOUND:
            return "The machine is empty. The Oracle finds no disk-spirits to consult.";
        case PAL_STATUS_ERROR_DATA_UNDERFLOW:
            return "The disk-spirit promised more knowledge than it delivered (Data Underflow).";
        case PAL_STATUS_DEVICE_ERROR:
            return "The disk-spirit itself reports a critical error.";
        default:
            return "An unknown omen has been received from the depths of the machine.";
    }
} 