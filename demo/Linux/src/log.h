//
// Created by zhangfuwen on 2022/1/18.
//

#ifndef AUDIO_IME_LOG_H
#define AUDIO_IME_LOG_H
#include <string_view>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#define CONF_SECTION "engine/audio_ime"
#define CONF_NAME_ID "access_id"  // no captal letter allowed
#define CONF_NAME_SECRET "access_secret" // no captal letter allowed
#define CONF_NAME_WUBI "wubi_table"
#define CONF_NAME_PINYIN "pinyin_enable"
#define CONF_NAME_SPEECH "pinyin_enable"

constexpr auto filename(std::string_view path)
{
    return path.substr(path.find_last_of("/\\") + 1);
}

static_assert(filename("/home/user/src/project/src/file.cpp") == "file.cpp");
static_assert(filename(R"(C:\\user\src\project\src\file.cpp)") == "file.cpp");
static_assert(filename("./file.cpp") == "file.cpp");
static_assert(filename("file.cpp") == "file.cpp");

#define LOG_INFO(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now)); \
        if(logfd)                                                              \
        {                                                                      \
            fprintf(logfd, "%s %s:%d %s > " fmt "\n", buff, filename(__FILE__).data(),          \
                    __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
            fflush(logfd);                                                                       \
        }                                                                       \
        printf("%s %s:%d %s > " fmt "\n", buff, filename(__FILE__).data(),          \
                __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
    } while (0)
//if (logfd >= 0) {                                                      \
//    dprintf(logfd, "%s:%d %s > " fmt "\n", filename(__FILE__).data(),  \
//            __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
//}                                                                \

#define LOG_DEBUG LOG_INFO
#define LOG_WARN LOG_INFO
#define LOG_TRACE LOG_INFO
#define LOG_ERROR LOG_INFO

inline FILE * logfd = nullptr;
inline FILE * dumpfd = nullptr;

inline void log_init() {
    logfd = fopen("/home/zhangfuwen/audio_ime_log.txt", "a+");
    dumpfd = fopen("/home/zhangfuwen/audio_ime_dump.txt", "a+");
}

static inline void printBacktrace() {
    char buff[40];
    time_t now = time(NULL);
    strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(dumpfd, "%s crash:\n", buff);
    void *stackBuffer[64];
    int numAddresses = backtrace((void**) &stackBuffer, 64);
    char **addresses = backtrace_symbols(stackBuffer, numAddresses);
    for( int i = 0 ; i < numAddresses ; ++i ) {
        fprintf(dumpfd, "[%2d]: %s\n", i, addresses[i]);
    }
    free(addresses);
}

inline void signal_handler(int signo) {
    printBacktrace();
    signal(signo, SIG_DFL);
    raise(signo);
}

#endif //AUDIO_IME_LOG_H
