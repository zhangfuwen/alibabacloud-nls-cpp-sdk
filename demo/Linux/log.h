//
// Created by zhangfuwen on 2022/1/18.
//

#ifndef AUDIO_IME_LOG_H
#define AUDIO_IME_LOG_H
#include <string_view>

constexpr auto filename(std::string_view path)
{
    return path.substr(path.find_last_of("/\\") + 1);
}

static_assert(filename("/home/user/src/project/src/file.cpp") == "file.cpp");
static_assert(filename(R"(C:\\user\src\project\src\file.cpp)") == "file.cpp");
static_assert(filename("./file.cpp") == "file.cpp");
static_assert(filename("file.cpp") == "file.cpp");

#define LOG_INFO(fmt, ...) do {                                                      \
        printf("%s:%d %s > " fmt "\n", filename(__FILE__).data(), __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

#define LOG_DEBUG LOG_INFO
#define LOG_WARN LOG_INFO
#define LOG_ERROR LOG_INFO
#endif //AUDIO_IME_LOG_H
