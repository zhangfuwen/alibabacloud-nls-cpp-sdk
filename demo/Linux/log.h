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
        if(logfd)                                                              \
        {                                                                      \
            fprintf(logfd, "%s:%d %s > " fmt "\n", filename(__FILE__).data(),          \
                   __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
            fflush(logfd);                                                                       \
        }                                                                       \
        printf("%s:%d %s > " fmt "\n", filename(__FILE__).data(),          \
               __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
    } while (0)
//if (logfd >= 0) {                                                      \
//    dprintf(logfd, "%s:%d %s > " fmt "\n", filename(__FILE__).data(),  \
//            __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
//}                                                                \

#define LOG_DEBUG LOG_INFO
#define LOG_WARN LOG_INFO
#define LOG_ERROR LOG_INFO

inline FILE * logfd = nullptr;
inline int dumpfd = -1;

inline void log_init() {
    logfd = fopen("/home/zhangfuwen/audio_ime_log.txt", "w+");
    dumpfd = open("/home/zhangfuwen/audio_ime_dump.txt", O_CREAT|O_RDWR | O_APPEND, 0644);
}

inline void dump() {
    void *array[40];
    size_t size;

    if(dumpfd >= 0) {
        // get void*'s for all entries on the stack
        size = backtrace(array, 40);
        // print out all the frames to stderr
        backtrace_symbols_fd(array, size, dumpfd);
    }
}

inline void signal_handler(int signo) {
    dump();
    signal(signo, SIG_DFL);
    raise(signo);
}

#endif //AUDIO_IME_LOG_H
