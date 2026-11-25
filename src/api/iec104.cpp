#include "iec104/iec104.h"

#ifndef IEC104_VERSION_STRING
#define IEC104_VERSION_STRING "0.0.0"
#endif

extern "C" {

const char* iec104_version_string(void) {
    return IEC104_VERSION_STRING;
}

const char* iec104_status_string(iec104_status status) {
    switch (status) {
    case IEC104_OK:
        return "ok";
    case IEC104_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case IEC104_ERR_BUFFER_TOO_SMALL:
        return "buffer too small";
    case IEC104_ERR_NEED_MORE_DATA:
        return "need more data";
    case IEC104_ERR_MALFORMED:
        return "malformed frame";
    case IEC104_ERR_OUT_OF_RANGE:
        return "value out of range";
    case IEC104_ERR_UNSUPPORTED:
        return "unsupported";
    case IEC104_ERR_INTERNAL:
        return "internal error";
    }
    return "unknown status";
}

} // extern "C"
