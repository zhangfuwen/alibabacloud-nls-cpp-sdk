//
// Created by zhangfuwen on 2022/1/18.
//

#ifndef AUDIO_IME_COMMON_LOG_H
#define AUDIO_IME_COMMON_LOG_H
#include <string_view>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <glib.h>
#include <string>
#include <filesystem>
#include <syslog.h>

constexpr auto filename(std::string_view path)
{
    return path.substr(path.find_last_of("/\\") + 1);
}

static_assert(filename("/home/user/src/project/src/file.cpp") == "file.cpp");
static_assert(filename(R"(C:\\user\src\project\src\file.cpp)") == "file.cpp");
static_assert(filename("./file.cpp") == "file.cpp");
static_assert(filename("file.cpp") == "file.cpp");

#define FUN_INFO(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto pid = getpid();                                                   \
        syslog(LOG_DAEMON | LOG_INFO, "%s %d %d info %s:%d %s > " fmt "\n", buff, pid, tid, filename(__FILE__).data(),          \
                __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
        g_info("%s %d %d %s:%d %s > " fmt, buff, pid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

#define FUN_ERROR(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto pid = getpid();                                                   \
        syslog(LOG_DAEMON | LOG_INFO, "%s %d %d info %s:%d %s > " fmt "\n", buff, pid, tid, filename(__FILE__).data(),          \
                __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
        g_error("%s %d %d %s:%d %s > " fmt, buff, pid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

#define FUN_DEBUG(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto pid = getpid();                                                   \
        syslog(LOG_DAEMON | LOG_INFO, "%s %d %d info %s:%d %s > " fmt "\n", buff, pid, tid, filename(__FILE__).data(),          \
                __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
        g_debug("%s %d %d %s:%d %s > " fmt, buff, pid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

#define FUN_WARN(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto pid = getpid();                                                   \
        syslog(LOG_DAEMON | LOG_INFO, "%s %d %d info %s:%d %s > " fmt "\n", buff, pid, tid, filename(__FILE__).data(),          \
                __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
        g_warning("%s %d %d %s:%d %s > " fmt, buff, pid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

#define FUN_TRACE(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto pid = getpid();                                                   \
        syslog(LOG_DAEMON | LOG_INFO, "%s %d %d info %s:%d %s > " fmt "\n", buff, pid, tid, filename(__FILE__).data(),          \
                __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
        g_debug("%s %d %d %s:%d %s > " fmt, buff, pid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

static inline void printBacktrace() {
    char buff[40];
    time_t now = time(nullptr);
    strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&now));

    syslog(LOG_DAEMON | LOG_ERR, "crash: %s\n", buff);
    void *stackBuffer[64];
    int numAddresses = backtrace((void**) &stackBuffer, 64);
    char **addresses = backtrace_symbols(stackBuffer, numAddresses);
    for( int i = 0 ; i < numAddresses ; ++i ) {
        syslog(LOG_DAEMON | LOG_ERR, "[%2d]: %s\n", i, addresses[i]);
    }
    free(addresses);
}

inline void signal_handler(int signo) {
    printBacktrace();
    signal(signo, SIG_DFL);
    raise(signo);
}

#endif // AUDIO_IME_COMMON_LOG_H