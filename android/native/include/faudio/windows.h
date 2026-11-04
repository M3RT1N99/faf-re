#pragma once

// What FAudio's FAudio_internal.h wants of <windows.h> on the side of it this build compiles
// (cmake/FafAndroidFAudio.cmake): one function.

#include <string.h>

#define lstrcpynA(destination, source, size) strlcpy(destination, source, size)
