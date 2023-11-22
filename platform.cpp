#include "platform.h"

#ifdef _WIN32
#include "platform/platform_win32.cpp"
#else
#include "platform/platform_posix.cpp"
#endif