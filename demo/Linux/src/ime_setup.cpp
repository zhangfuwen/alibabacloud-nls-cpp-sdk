//
// Created by zhangfuwen on 2022/1/18.
//
#include "log.h"
#include <gtkmm.h>
#include <gtkmm/window.h>
#include <ibus.h>

int main(int argc, char *argv[]) {
    auto app = Gtk::Application::create("org.gtkmm.examples.base");
    auto builder = Gtk::Builder::create_from_file("/usr/share/ibus/ibus-audio/data/ime_setup.glade");

    Gtk::Window *win1;
    builder->get_widget<Gtk::Window>("win1", win1);
    Gtk::Button *but1;
    builder->get_widget<Gtk::Button>("but1", but1);
    Gtk::Label *page1;
    builder->get_widget<Gtk::Label>("page1", page1);
    Gtk::Entry *id_text;
    builder->get_widget<Gtk::Entry>("id_text", id_text);
    Gtk::Entry *secret_text;
    builder->get_widget<Gtk::Entry>("secret_text", secret_text);
    Gtk::Button *but_set_config;
    builder->get_widget<Gtk::Button>("but_set_config", but_set_config);
    Gtk::Label *page2;
    builder->get_widget<Gtk::Label>("page2", page2);
    IBusBus *g_bus;
    IBusConfig *config;

    but_set_config->signal_clicked().connect([&]() {
        auto id_ustr = id_text->get_text();
        auto secret_ustr = secret_text->get_text();
        auto ret = ibus_config_set_value(config, CONF_SECTION, CONF_NAME_ID, g_variant_new_string(id_ustr.c_str()));
        if (!ret) {
            LOG_INFO("failed to set value");
        }
        ret = ibus_config_set_value(config, CONF_SECTION, CONF_NAME_SECRET, g_variant_new_string(secret_ustr.c_str()));
        if (!ret) {
            LOG_INFO("failed to set value");
        }
    });

    but1->signal_clicked().connect([&]() {
        printf("clicked");
        ibus_init();
        g_bus = ibus_bus_new();
        g_object_ref_sink(g_bus);

        LOG_DEBUG("bus %p", g_bus);

        if (!ibus_bus_is_connected(g_bus)) {
            LOG_WARN("not connected to ibus");
            return;
        }
        config = ibus_bus_get_config(g_bus);
        if (!config) {
            LOG_ERROR("failed to get config from bus:%p", g_bus);
            return;
        } else {
            g_object_ref_sink(config);
        }

        auto id = ibus_config_get_value(config, CONF_SECTION, CONF_NAME_ID);
        if (id != nullptr) {
            auto akId = g_variant_get_string(id, nullptr);
            if (akId != nullptr) {
                id_text->set_text(akId);
            } else {
                LOG_ERROR("failed to get akId");
            }
        } else {
            LOG_ERROR("failed to get akId");
        }

        auto secret = ibus_config_get_value(config, CONF_SECTION, CONF_NAME_SECRET);
        if (secret != nullptr) {
            auto akSecret = g_variant_get_string(secret, nullptr);
            if (akSecret != nullptr) {
                secret_text->set_text(akSecret);
            } else {
                LOG_ERROR("failed to get akSecret");
            }
        } else {
            LOG_ERROR("failed to get akSecret");
        }

        if (config) {
            g_object_unref(config);
        }
        g_object_unref(g_bus);
    });
    win1->set_title("audio_ime configuration");

    // return app->run(*win1, argc, argv);
    return app->run(*win1, argc, argv);
}