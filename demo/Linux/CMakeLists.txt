
project(audio-ime)

set(CMAKE_VERBOSE_MAKEFILE ON)
#set(CMAKE_C_FLAGS "-O2 -fexceptions -fPIC -MMD")
set(CMAKE_C_FLAGS "-O0 -fexceptions -fPIC -MMD -pg -g -ggdb")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fpermissive")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-narrowing")


MESSAGE("c flag is ${CMAKE_C_FLAGS}")
MESSAGE("os is ${CMAKE_SYSTEM}")

MESSAGE("build speech-demo enter..")
MESSAGE("PROFECT_SOURCE_DIR " ${PROJECT_SOURCE_DIR})
MESSAGE("CMAKE_SOURCE_DIR " ${CMAKE_SOURCE_DIR})

include_directories(${CMAKE_SOURCE_DIR}/../../build/install/NlsSdk3.X_LINUX/include)
link_directories(${CMAKE_SOURCE_DIR}/../../build/install/NlsSdk3.X_LINUX/lib)


add_executable(pulse_record pulse_record.cpp)
target_link_libraries(pulse_record pulse pulse-simple)

# 一句话识别
include(GNUInstallDirs)

find_package(PkgConfig)
pkg_check_modules(IBus REQUIRED ibus-1.0)
if(IBus_FOUND)
    include_directories(${IBus_INCLUDE_DIRS})
    message("IBus_INCLUDE_DIRS ${IBus_INCLUDE_DIRS}")
    link_directories(${IBus_LIBRARY_DIRS})
endif(IBus_FOUND)

pkg_check_modules(GLib2 REQUIRED glib-2.0)

if(GLib2_FOUND)
    include_directories(${GLib2_INCLUDE_DIRS})
    link_directories(${GLib2_LIBRARY_DIRS})
    message("GLib2_INCLUDE_DIRS ${GLib2_INCLUDE_DIRS}")
else()
    message("glib2 not found")
endif(GLib2_FOUND)

include_directories(pinyin/include)
add_library(pinyin STATIC
        pinyin/dictbuilder.cpp
        pinyin/dictlist.cpp
        pinyin/dicttrie.cpp
        pinyin/lpicache.cpp
        pinyin/matrixsearch.cpp
        pinyin/mystdlib.cpp
        pinyin/ngram.cpp
        pinyin/pinyinime.cpp
        pinyin/searchutility.cpp
        pinyin/spellingtable.cpp
        pinyin/spellingtrie.cpp
        pinyin/splparser.cpp
        pinyin/sync.cpp
        pinyin/userdict.cpp
        pinyin/utf16char.cpp
        pinyin/utf16reader.cpp
        )

add_executable(wubitest wubitest.cpp)
add_executable(pinyintest pinyintest.cpp)
target_link_libraries(pinyintest pinyin ${GLib2_LIBRARIES})

add_executable(audio_ime profile_scan.cpp speechRecognizerDemo.cpp)
target_link_libraries(audio_ime
    alibabacloud-idst-speech.a
    pthread z dl psl brotlidec pulse pulse-simple
    ${IBus_LIBRARIES} ${GLib2_LIBRARIES}
        pinyin)
target_compile_definitions(audio_ime PRIVATE _GLIBCXX_USE_CXX11_ABI=0)

pkg_check_modules(GTKMM3 REQUIRED gtkmm-3.0)
add_executable(audio_ime_setup ime_setup.cpp)
target_include_directories(audio_ime_setup PRIVATE ${GTKMM3_INCLUDE_DIRS})
message("gtkmm ${GTKMM3_INCLUDE_DIRS}")
target_link_libraries(audio_ime_setup ${GTKMM3_LIBRARIES}
        ${IBus_LIBRARIES} ${GLib2_LIBRARIES}
        )

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Dean") #required
set(CPACK_PACKAGE_NAME "audio_ime") # debian package name
set(CPACK_PACKAGE_VERSION "1.1")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA, "scripts/prerm;scripts/postrm;scripts/preinst;scripts/postinst")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libpsl-dev, libpulse-dev, libbrotli-dev, ibus")

INSTALL(FILES audio_ime.desktop DESTINATION share/applications PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(FILES audio_ime.xml DESTINATION /usr/share/ibus/component PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(TARGETS audio_ime DESTINATION /usr/bin/ PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(TARGETS audio_ime_setup DESTINATION /usr/bin/ PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(FILES ime_setup.glade DESTINATION /usr/share/ibus/ibus-audio/data PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ )
INSTALL(FILES dict_pinyin.dat DESTINATION /usr/share/ibus-table/data PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ )
INSTALL(FILES wubi86.txt DESTINATION /usr/share/ibus-table/data PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ )
INSTALL(FILES wubi98.txt DESTINATION /usr/share/ibus-table/data PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ )
INSTALL(FILES audio_ime.png DESTINATION share/icons/hicolor/scalable/apps/ PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)

INCLUDE(CPack)
# 实时识别
#add_executable(stDemo profile_scan.cpp speechTranscriberDemo.cpp)
#target_link_libraries(stDemo
#    alibabacloud-idst-speech
#    pthread z dl)
#
## 转写
#add_executable(syDemo profile_scan.cpp speechSynthesizerDemo.cpp)
#target_link_libraries(syDemo
#    alibabacloud-idst-speech
#    pthread z dl)
#
## 对话助手
#add_executable(daDemo dialogAssistantDemo.cpp)
#target_link_libraries(daDemo
#    alibabacloud-idst-speech
#    pthread z dl)



