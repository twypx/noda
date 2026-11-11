#pragma once

#ifndef NPP_VERSION_STRING
#define NPP_VERSION_STRING "1.0.25"
#endif

namespace NppVersion {
    inline const char* currentVersion() {
        return NPP_VERSION_STRING;
    }
}
