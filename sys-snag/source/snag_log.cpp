#include "snag_log.h"
#include <switch.h>
#include <cstdio>
#include <cstdarg>

void SnagLog(const char* fmt, ...)
{
    if (!fmt)
        return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';

    // 19.2 MHz tick -> seconds since boot.
    uint64_t secs = armGetSystemTick() / 19200000ULL;

    FILE* fp = fopen("sdmc:/switch/StreamSnagNX/sys-snag.log", "a");
    if (!fp)
        return;
    fprintf(fp, "[%llu] %s\n", (unsigned long long)secs, buf);
    fclose(fp);
}
