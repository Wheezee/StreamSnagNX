#include "log.hpp"

#include <cstdio>
#include <mutex>

namespace ssnx
{
namespace
{

std::mutex g_log_mutex;

} // namespace

void Log(const std::string& msg)
{
#ifdef __SWITCH__
    std::lock_guard<std::mutex> lock(g_log_mutex);
    FILE* fp = fopen("sdmc:/switch/StreamSnagNX/ssnx.log", "a");
    if (fp)
    {
        fputs((msg + "\n").c_str(), fp);
        fflush(fp);
        fclose(fp);
    }
#else
    std::printf("%s\n", msg.c_str());
#endif
}

} // namespace ssnx

