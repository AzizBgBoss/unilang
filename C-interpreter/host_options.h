#ifndef UNILANG_HOST_OPTIONS_H
#define UNILANG_HOST_OPTIONS_H

#include <string.h>

static inline int ULVM_desktop_network_flag(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--net") == 0) return 1;
    return 0;
}

#endif
