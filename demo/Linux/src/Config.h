//
// Created by zhangfuwen on 2022/1/27.
//

#ifndef AUDIO_IME_CONFIG_H
#define AUDIO_IME_CONFIG_H
#include <ibus.h>
#include <string>
#include "log.h"
#include "RuntimeOptions.h"
class Config {
public:
    static void init(IBusBus *bus, RuntimeOptions * opts) {
        delete instance;
        instance = new Config(bus, opts);
    }
    static Config *getInstance() {
        return instance;
    }
    explicit Config(IBusBus *bus, RuntimeOptions * opts) : m_opts(opts) {
        m_config = ibus_bus_get_config(bus);
        g_object_ref_sink(m_config);

        LOG_DEBUG("ibus config %p", m_config);
        RuntimeOptions::get()->speechAkId = GetString(CONF_NAME_ID);
        RuntimeOptions::get()->speechSecret = GetString(CONF_NAME_SECRET);
        ibus_config_watch(m_config, CONF_SECTION, CONF_NAME_ID);
        ibus_config_watch(m_config, CONF_SECTION, CONF_NAME_SECRET);
        g_signal_connect(m_config, "value-changed", G_CALLBACK(OnValueChanged), this);
        LOG_INFO("config value-changed signal connected");
    }
    ~Config() {
        g_object_unref(m_config);
    }
    [[nodiscard]] std::string GetString(const std::string &name) const;
    void SetString(const std::string& name, const std::string& val);
public:
private:
    IBusConfig *m_config;
    RuntimeOptions *m_opts;

    static Config * instance;
    static void OnValueChanged(IBusConfig *config, gchar *section, gchar *name, GVariant *value, gpointer user_data);
};

#endif // AUDIO_IME_CONFIG_H