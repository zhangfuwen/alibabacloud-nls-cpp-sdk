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
#include <glib.h>
#include <string>
#include <filesystem>

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
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto gid = getgid();                                                   \
        if(logfd)                                                              \
        {                                                                      \
            fprintf(logfd, "%s %d %d info %s:%d %s > " fmt "\n", buff, gid, tid, filename(__FILE__).data(),          \
                    __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
            fflush(logfd);                                                                       \
        }                                                                       \
        g_info("%s %d %d %s:%d %s > " fmt, buff, gid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

#define LOG_ERROR(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto gid = getgid();                                                   \
        if(logfd)                                                              \
        {                                                                      \
            fprintf(logfd, "%s %d %d error %s:%d %s > " fmt "\n", buff, gid, tid, filename(__FILE__).data(),          \
                    __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
            fflush(logfd);                                                                       \
        }                                                                       \
        g_error("%s %d %d %s:%d %s > " fmt, buff, gid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

#define LOG_DEBUG(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto gid = getgid();                                                   \
        if(logfd)                                                              \
        {                                                                      \
            fprintf(logfd, "%s %d %d debug %s:%d %s > " fmt "\n", buff, gid, tid, filename(__FILE__).data(),          \
                    __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
            fflush(logfd);                                                                       \
        }                                                                       \
        g_debug("%s %d %d %s:%d %s > " fmt, buff, gid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

#define LOG_WARN(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto gid = getgid();                                                   \
        if(logfd)                                                              \
        {                                                                      \
            fprintf(logfd, "%s %d %d warning %s:%d %s > " fmt "\n", buff, gid, tid, filename(__FILE__).data(),          \
                    __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
            fflush(logfd);                                                                       \
        }                                                                       \
        g_warning("%s %d %d %s:%d %s > " fmt, buff, gid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

#define LOG_TRACE(fmt, ...)                                                     \
    do {                                                                       \
        char buff[40]; \
        time_t log_now = time(NULL); \
        strftime(buff, 40, "%Y-%m-%d %H:%M:%S", localtime(&log_now));          \
        auto tid = gettid();                                                   \
        auto gid = getgid();                                                   \
        if(logfd)                                                              \
        {                                                                      \
            fprintf(logfd, "%s %d %d tracing %s:%d %s > " fmt "\n", buff, gid, tid, filename(__FILE__).data(),          \
                    __LINE__, __FUNCTION__, ##__VA_ARGS__);                     \
            fflush(logfd);                                                                       \
        }                                                                       \
        g_debug("%s %d %d %s:%d %s > " fmt, buff, gid, tid, filename(__FILE__).data(),          \
           __LINE__, __FUNCTION__, ##__VA_ARGS__);                        \
    } while (0)

inline FILE * logfd = nullptr;
inline FILE * dumpfd = nullptr;

inline void log_init() {
    auto home = getenv("XDG_CONFIG_HOME");
    g_info("home %s", home);
    std::string base = "/var/log/";
    if(home != nullptr) {
        base = std::string(home);
    }
    if(!std::filesystem::is_directory(base)) {
        std::filesystem::create_directory(base);
    }

    logfd = fopen((base + "/audio_ime_log.txt").c_str(), "a+");
    dumpfd = fopen((base + "/audio_ime_dump.txt").c_str(), "a+");
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
