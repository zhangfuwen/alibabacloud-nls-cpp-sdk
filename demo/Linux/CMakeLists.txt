cmake_minimum_required(VERSION 3.1)
project(ibus-fun)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_C_FLAGS "-O0 -fexceptions -fPIC -MMD -pg -g -ggdb")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fpermissive")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-narrowing")


MESSAGE("c flag is ${CMAKE_C_FLAGS}")
MESSAGE("os is ${CMAKE_SYSTEM}")

MESSAGE("PROFECT_SOURCE_DIR " ${PROJECT_SOURCE_DIR})
MESSAGE("CMAKE_SOURCE_DIR " ${CMAKE_SOURCE_DIR})

include_directories(${CMAKE_SOURCE_DIR}/../../build/install/NlsSdk3.X_LINUX/include)
link_directories(${CMAKE_SOURCE_DIR}/../../build/install/NlsSdk3.X_LINUX/lib)


add_executable(pulse_record examples/pulse_record.cpp)
target_link_libraries(pulse_record pulse pulse-simple)

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

add_executable(wubitest examples/wubitest.cpp)
add_executable(pinyintest examples/pinyintest.cpp)
target_link_libraries(pinyintest pinyin ${GLib2_LIBRARIES})

add_executable(ibus-fun
        src/Engine.cpp
        src/Config.cpp
        src/RuntimeOptions.cpp
        src/ibus_fun_engine.cpp
        src/DictPinyin.cpp
        src/DictWubi.cpp
        src/ibus_fun_main.cpp
        src/DictSpeech.cpp)
target_link_libraries(ibus-fun
    alibabacloud-idst-speech.a
    pthread z dl psl brotlidec pulse pulse-simple
    ${IBus_LIBRARIES} ${GLib2_LIBRARIES}
        pinyin)
target_compile_definitions(ibus-fun PRIVATE _GLIBCXX_USE_CXX11_ABI=0)

pkg_check_modules(GTKMM3 REQUIRED gtkmm-3.0)
add_executable(fun-setup src/ibus_fun_setup.cpp)
target_include_directories(fun-setup PRIVATE ${GTKMM3_INCLUDE_DIRS})
message("gtkmm ${GTKMM3_INCLUDE_DIRS}")
target_link_libraries(fun-setup ${GTKMM3_LIBRARIES}
        ${IBus_LIBRARIES} ${GLib2_LIBRARIES}
        )

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Dean") #required
set(CPACK_PACKAGE_NAME "ibus-fun") # debian package name
set(CPACK_PACKAGE_VERSION "1.1")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA, "scripts/prerm;scripts/postrm;scripts/preinst;scripts/postinst")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libpsl-dev, libpulse-dev, libbrotli-dev, ibus")

INSTALL(FILES res/ibus-fun.desktop DESTINATION share/applications PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(FILES res/ibus-fun.xml DESTINATION /usr/share/ibus/component PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(TARGETS ibus-fun DESTINATION /usr/bin/ PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(TARGETS fun-setup DESTINATION /usr/bin/ PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(FILES res/ibus_fun_setup.glade DESTINATION /usr/share/ibus/ibus-fun/data PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ )
INSTALL(FILES res/dict_pinyin.dat DESTINATION /usr/share/ibus-table/data PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ )
INSTALL(FILES res/wubi86.txt DESTINATION /usr/share/ibus-table/data PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ )
INSTALL(FILES res/wubi98.txt DESTINATION /usr/share/ibus-table/data PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ )
INSTALL(FILES res/ibus-fun.png DESTINATION /usr/share/icons/hicolor/scalable/apps/ PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(FILES res/ibus-fun.conf DESTINATION /etc/rsyslog.d/ PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ WORLD_EXECUTE)
INSTALL(FILES language/zh_CN/LC_MESSAGES/messages.mo DESTINATION /usr/share/ibus/ibus-fun/data/language/zh_CN/LC_MESSAGES/ PERMISSIONS OWNER_READ OWNER_WRITE WORLD_READ)

INCLUDE(CPack)
# xgettext -k_ -o language/zh_CN/LC_MESSAGES/messages.po ./src/Engine.cpp
#  msgfmt -o language/zh_CN/LC_MESSAGES/messages.mo language/zh_CN/LC_MESSAGES/messages.po
