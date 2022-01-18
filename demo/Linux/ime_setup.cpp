//
// Created by zhangfuwen on 2022/1/18.
//
#include "log.h"
#include <gtkmm.h>
#include <gtkmm/window.h>
#include <ibus.h>

#define CONF_SECTION "engine/audio_ime"
#define CONF_NAME "xxxx"

int main(int argc, char *argv[]) {
  auto app = Gtk::Application::create("org.gtkmm.examples.base");
  auto builder = Gtk::Builder::create_from_file("/usr/share/ibus/ibus-audio/data/ime_setup.glade");
  Gtk::Window *win1;
  builder->get_widget<Gtk::Window>("win1", win1);
  Gtk::Button *button1;
  builder->get_widget<Gtk::Button>("but1", button1);
  button1->signal_clicked().connect([win1]() {
    printf("clicked");
    ibus_init();
    IBusBus *g_bus = ibus_bus_new();
    g_object_ref_sink(g_bus);

    LOG_DEBUG("bus %p", g_bus);

    if (!ibus_bus_is_connected(g_bus)) {
      LOG_WARN("not connected to ibus");
      exit(0);
    }
    auto conn = ibus_bus_get_connection(g_bus);
    GCancellable cancellable;
    GError *error;
    LOG_DEBUG("");
    auto config = ibus_config_new(conn, NULL, NULL);
    LOG_DEBUG("");
    auto ret = ibus_config_set_value(config, CONF_SECTION, CONF_NAME,
                                     g_variant_new_int32(random()));
    if (!ret) {
      LOG_INFO("failed to set value");
    }
    LOG_DEBUG("");
    //        win1->close();
  });
  win1->set_title("asdfdad");
  // window.set_title("xxxx");
  return app->run(*win1, argc, argv);
}