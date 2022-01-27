/*
 * Copyright 2021 Alibaba Group Holding Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "common.h"
#include "pinyinime.h"
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <glib-object.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <ibus.h>
#include <iostream>
#include <locale.h>
#include <map>
#include <pthread.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FunEngine.h"
#include "PinyinIME.h"
#include "SpeechRecognizer.h"
#include "log.h"
#include "nlsClient.h"
#include "nlsEvent.h"
#include "nlsToken.h"
#include "speechRecognizerRequest.h"
#include "wubi.h"
#include "engine.h"
#include "Config.h"
#include <functional>
using namespace std::placeholders;
IBusBus *g_bus;
FunEngine *g_engine = nullptr;
gint id = 0;
IBusConfig *g_config = nullptr;

void sigterm_cb(int sig) {
    LOG_ERROR("sig term %d", sig);
    exit(-1);
}

static void IBusOnDisconnectedCb([[maybe_unused]] IBusBus *bus, [[maybe_unused]] gpointer user_data) {
    LOG_TRACE("Entry");
    ibus_quit();
    LOG_TRACE("Exit");
}

int main([[maybe_unused]] gint argc, gchar **argv) {
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, "/usr/share/ibus/ibus-audio/data/language");
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    signal(SIGTERM, sigterm_cb);
    signal(SIGINT, sigterm_cb);
    signal(SIGSEGV, signal_handler);
    log_init();

    LOG_INFO("ibus_init");

    ibus_init();
    g_bus = ibus_bus_new();
    g_object_ref_sink(g_bus);

    LOG_INFO("ibus %p", g_bus);

    if (!ibus_bus_is_connected(g_bus)) {
        LOG_WARN("not connected to ibus");
        exit(0);
    } else {
        LOG_INFO("ibus connected");
    }

    Config::init(g_bus, RuntimeOptions::get());
    g_signal_connect(g_bus, "disconnected", G_CALLBACK(IBusOnDisconnectedCb), nullptr);

    IBusFactory *factory = ibus_factory_new(ibus_bus_get_connection(g_bus));
    g_object_ref_sink(factory);
    LOG_DEBUG("factory %p", factory);

    LOG_DEBUG("xx");
    GType type = IBUS_TYPE_FUN_ENGINE;
    LOG_DEBUG("typename: %s", g_type_name(type));
    ibus_factory_add_engine(factory, "FunEngine", type);
    LOG_DEBUG("xx");

    IBusComponent *component;

    if (g_bus) {
        if (!ibus_bus_request_name(g_bus, "org.freedesktop.IBus.Fun", 0)) {
            LOG_ERROR("error requesting bus name");
            exit(1);
        } else {
            LOG_INFO("ibus_bus_request_name success");
        }
    } else {
        component = ibus_component_new(
            "org.freedesktop.IBus.Fun",
            "Fun input method",
            "1.1",
            "MIT",
            "zhangfuwen",
            "http://xjbcode.fun/",
            "/usr/bin/ibus-fun --ibus",
            "ibus-fun");
        LOG_DEBUG("component %p", component);
        ibus_component_add_engine(
            component,
            ibus_engine_desc_new(
                "FunEngine",
                "Fun input method",
                "Fun input method",
                "zh_CN",
                "MIT",
                "zhangfuwen",
                "ibus-fun",
                "default"));
        ibus_bus_register_component(g_bus, component);

        ibus_bus_set_global_engine_async(g_bus, "FunEgine", -1, nullptr, nullptr, nullptr);
    }

    LOG_INFO("entering ibus main");
    ibus_main();
    LOG_INFO("exiting ibus main");
}
