//
// Created by zhangfuwen on 2022/1/22.
//

#include <cctype>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <glib-object.h>
#include <glib.h>
#include <ibus.h>
#include <map>
#include <pulse/simple.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common.h"
#include "Engine.h"
#include "log.h"
#include "wubi.h"

using namespace std::placeholders;

Wubi *g_wubi = nullptr;
pinyin::Pinyin *g_pinyin = nullptr;
SpeechRecognizer *g_speechRecognizer = nullptr;

Engine::Engine(gchar *engine_name, int id, IBusBus *bus) {
    gchar *path = g_strdup_printf("/org/freedesktop/IBus/Engine/%i", id);
    m_engine = ibus_engine_new(engine_name, path, ibus_bus_get_connection(bus));
    g_free(path);
    LOG_INFO("[IM:iBus]: Creating IM Engine with name:%s and id:%d", engine_name, id);
    // Setup Lookup table
    if (!g_wubi) {
        g_wubi = new Wubi(wubi86DictPath);
    }
    m_wubi = g_wubi;

    if (!g_pinyin) {
        g_pinyin = new pinyin::Pinyin();
    }
    m_pinyin = g_pinyin;

    if (!g_speechRecognizer) {
        g_speechRecognizer = new SpeechRecognizer(*this);
    }
    m_speechRecognizer = g_speechRecognizer;
}

IBusEngine *Engine::getIBusEngine() { return m_engine; }
Engine::~Engine() {
    g_object_unref(m_engine);
    //    delete m_pinyin;
    //    delete m_wubi;
    //    delete m_speechRecognizer;
}
void Engine::OnCompleted(std::string text) {
    engine_commit_text(m_engine, ibus_text_new_from_string(text.c_str()));
    ibus_engine_update_preedit_text(m_engine, ibus_text_new_from_string(""), 0, false);
}
void Engine::OnFailed() {
    engine_commit_text(m_engine, ibus_text_new_from_string(""));
    ibus_engine_update_preedit_text(m_engine, ibus_text_new_from_string(""), 0, false);
}
void Engine::OnPartialResult(std::string text) {
    ibus_engine_update_preedit_text(m_engine, ibus_text_new_from_string(text.c_str()), 0, TRUE);
}
void Engine::registerCallbacks() {
    g_signal_connect(m_engine, "process-key-event", G_CALLBACK(OnProcessKeyEvent), this);
    g_signal_connect(m_engine, "enable", G_CALLBACK(OnEnable), this);
    g_signal_connect(m_engine, "disable", G_CALLBACK(OnDisable), this);
    g_signal_connect(m_engine, "focus-out", G_CALLBACK(OnFocusOut), this);
    g_signal_connect(m_engine, "focus-in", G_CALLBACK(OnFocusIn), this);
    g_signal_connect(m_engine, "candidate-clicked", G_CALLBACK(OnCandidateClicked), this);
    g_signal_connect(m_engine, "property-activate", G_CALLBACK(OnPropertyActivate), this);
}
void Engine::SetSpeechAkId(std::string akId) { m_speechRecognizer->setAkId(std::move(akId)); }
void Engine::SetSpeechAkSecret(std::string akSecret) { m_speechRecognizer->setAkSecret(std::move(akSecret)); }
void Engine::Enable() { m_lookupTable = new LookupTable(this->getIBusEngine()); }
void Engine::Disable() { delete m_lookupTable; }
void Engine::IBusUpdateIndicator(long recordingTime) {
    ibus_engine_update_auxiliary_text(
        m_engine, ibus_text_new_from_string(IBusMakeIndicatorMsg(recordingTime).c_str()), TRUE);
}
// early return ?
// return value
std::pair<bool, bool> Engine::ProcessSpeech(guint keyval, guint keycode, guint state) {
    SpeechRecognizer::Status status = m_speechRecognizer->GetStatus();
    if ((state & IBUS_CONTROL_MASK) && keyval == IBUS_KEY_grave) {
        LOG_DEBUG("status = %d", status);
        if (status == SpeechRecognizer::WAITING) {
            LOG_DEBUG("waiting");
            return {true, TRUE};
        }
        if (status != SpeechRecognizer::RECODING) {
            std::thread t1([&]() { m_speechRecognizer->Start(); });
            t1.detach();
        } else {
            m_speechRecognizer->Stop();
        }
        m_lookupTable->Show();
        ibus_engine_show_preedit_text(m_engine);
        ibus_engine_show_auxiliary_text(m_engine);
        return {true, true};
    }
    if (state & IBUS_CONTROL_MASK) {
        return {true, false};
    }

    // other key inputs
    if (status != SpeechRecognizer::IDLE) {
        // don't respond to other key inputs when m_recording or m_waiting
        return {true, true};
    }
    return {false, false};
}
gboolean Engine::ProcessKeyEvent(guint keyval, guint keycode, guint state) {
    if (state & IBUS_RELEASE_MASK) { // only respond to key down
        return FALSE;
    }

    LOG_INFO("engine_process_key_event keycode: %d, keyval:%x", keycode, keyval);

    if (m_speechRecognizer != nullptr) {
        auto ret = ProcessSpeech(keyval, keycode, state);
        if (ret.first) { // early return
            return ret.second;
        }
    }

    if (state & IBUS_LOCK_MASK) {           // previously chinese mode
        if (keyval == IBUS_KEY_Caps_Lock) { // chinese to english mode
            LOG_INFO("caps lock pressed");
            ToggleToEnglishMode();
            return true;
        }

        auto ret = LookupTableNavigate(keyval);
        if (ret) {
            return true;
        }

        if (keyval == IBUS_KEY_space || keyval == IBUS_KEY_Return || isdigit((char)(keyval)) || keycode == 1) {
            if (m_input.empty()) {
                return false;
            }
            int index = isdigit((char)(keyval)) ? (int)(keyval - IBUS_KEY_0) : -1;
            candidateSelected(m_lookupTable->GetGlobalCursor(index), keycode == 1);
            return true;
        }

        LOG_DEBUG("keyval %x, m_input.size:%lu", keyval, m_input.size());
        if (keyval == IBUS_KEY_BackSpace && !m_input.empty()) { // delete
            m_input = m_input.substr(0, m_input.size() - 1);
        } else if (isalpha((char)keyval)) { // append new
            m_input += (char)tolower((int)keyval);
        } else { // don't know how to handle it
            return false;
        }
        // chinese mode
        ibus_engine_update_auxiliary_text(m_engine, ibus_text_new_from_string(m_input.c_str()), true);

        m_lookupTable->Clear();
        WubiPinyinQuery();
        m_lookupTable->Update();
        m_lookupTable->Show();

        return true;

    } else {
        // english mode
        m_lookupTable->Hide();
        ibus_engine_hide_preedit_text(m_engine);
        ibus_engine_hide_auxiliary_text(m_engine);
        return false;
    }
}
void Engine::WubiPinyinQuery() { // get pinyin candidates
    unsigned int nPinyinCandidates = 0;
    if (prop.pinyin) {
        nPinyinCandidates = m_pinyin->Search(m_input);
    }
    LOG_INFO("num candidates %u for %s", nPinyinCandidates, m_input.c_str());

    // get wubi candidates
    TrieNode *wubiSubtree = nullptr;
    if (!prop.wubi_table.empty()) { // no searching , no data
        wubiSubtree = m_wubi->Search(m_input);
    }
    map<uint64_t, string> m;
    if (wubiSubtree != nullptr && wubiSubtree->isEndOfWord) {
        // best exact match first
        auto it = wubiSubtree->values.rbegin();
        string candidate = it->second;
        auto text = ibus_text_new_from_string(candidate.c_str());
        m_lookupTable->Append(text, false);

        // others
        it++;
        while (it != wubiSubtree->values.rend()) {
            m.insert(*it); // insert to reorder
            it++;
        }
    }
    SubTreeTraversal(m, wubiSubtree); // insert subtree and reorder

    // wubi and pinyin candidate one after another
    int j = 0;
    LOG_INFO("map size:%lu", m.size());
    auto it = m.rbegin();
    while (true) {
        if (j >= nPinyinCandidates && it == m.rend()) {
            break;
        }
        if (it != m.rend()) {
            auto value = it->second;
            string &candidate = value;
            auto text = ibus_text_new_from_string(candidate.c_str());
            m_lookupTable->Append(text, false);
            it++;
        }
        if (j < nPinyinCandidates) {
            wstring buffer = m_pinyin->GetCandidate(j);
            glong items_read;
            glong items_written;
            GError *error;
            gunichar *utf32_str = g_utf16_to_ucs4(
                reinterpret_cast<const gunichar2 *>(buffer.data()), buffer.size(), &items_read, &items_written, &error);
            auto text = ibus_text_new_from_ucs4(utf32_str);
            m_lookupTable->Append(text, true);
            j++;
        }
    }
}
void Engine::ToggleToEnglishMode() { // commit input as english
    engine_commit_text(getIBusEngine(), ibus_text_new_from_string(m_input.c_str()));
    m_input = "";
    m_lookupTable->Clear();
    m_lookupTable->Hide();
    ibus_engine_update_auxiliary_text(m_engine, ibus_text_new_from_string(""), true);
    ibus_engine_hide_preedit_text(m_engine);
    ibus_engine_hide_auxiliary_text(m_engine);
}
bool Engine::LookupTableNavigate(guint keyval) {
    bool return1 = false;
    if (keyval == IBUS_KEY_equal || keyval == IBUS_KEY_Right) {
        LOG_DEBUG("equal pressed");
        m_lookupTable->PageDown();
        m_lookupTable->Update();
        return1 = true;
    } else if (keyval == IBUS_KEY_minus || keyval == IBUS_KEY_Left) {
        LOG_DEBUG("minus pressed");
        m_lookupTable->PageUp();
        m_lookupTable->Update();
        return1 = true;
    } else if (keyval == IBUS_KEY_Down) {
        LOG_DEBUG("down pressed");
        m_lookupTable->CursorDown();
        m_lookupTable->Update();
        return1 = true;
    } else if (keyval == IBUS_KEY_Up) {
        LOG_DEBUG("up pressed");
        m_lookupTable->CursorUp();
        m_lookupTable->Update();
        return1 = true;
    }
    return return1;
}

// static
gboolean Engine::OnProcessKeyEvent(IBusEngine *engine, guint keyVal, guint keycode, guint state, void *userdata) {
    auto *myengine = (Engine *)userdata;
    myengine->ProcessKeyEvent(keyVal, keycode, state);
}

// static
void Engine::OnEnable([[maybe_unused]] IBusEngine *engine, gpointer userdata) { ((Engine *)userdata)->Enable(); }

// static
void Engine::OnDisable([[maybe_unused]] IBusEngine *engine, gpointer userdata) { ((Engine *)userdata)->Disable(); }

// static
void Engine::OnFocusOut(IBusEngine *engine, gpointer userdata) {}

// static
void Engine::OnFocusIn([[maybe_unused]] IBusEngine *engine, gpointer userdata) { ((Engine *)userdata)->FocusIn(); }

// static
void Engine::OnCandidateClicked(IBusEngine *engine, guint index, guint button, guint state, gpointer userdata) {
    ((Engine *)userdata)->candidateSelected(index);
}

// static
void Engine::IBusConfig_OnValueChanged(
    IBusConfig *config,
    gchar *section,
    gchar *name,
    GVariant *value,
    gpointer user_data) {
    LOG_TRACE("Entry");
    Engine *engine = (Engine *)user_data;
    LOG_DEBUG("section:%s, name:%s", section, name);

    // to make thing easier, all watched configs are of string type
    auto nameVal = g_variant_get_string(value, nullptr);
    if (nameVal == nullptr) {
        LOG_ERROR("failed to get variant");
        return;
    }

    if (string(name) == CONF_NAME_ID) {
        engine->SetSpeechAkId(nameVal);
    } else if (string(name) == CONF_NAME_SECRET) {
        engine->SetSpeechAkSecret(nameVal);
    }
    LOG_TRACE("Exit");
}
void Engine::IBusConfigSetup(GDBusConnection *conn) {
    if (m_config) {
        g_object_unref(m_config);
    }
    m_config = ibus_config_new(conn, nullptr, nullptr);
    if (!m_config) {
        LOG_WARN("ibus config not accessible");
    } else {
        g_object_ref_sink(m_config);
    }
    LOG_DEBUG("ibus config %p", m_config);

    string speechAkId = ConfGetString(CONF_NAME_ID);
    string speechAkSecret = ConfGetString(CONF_NAME_SECRET);
    SetSpeechAkId(speechAkId);
    SetSpeechAkSecret(speechAkSecret);
    ibus_config_watch(m_config, CONF_SECTION, CONF_NAME_ID);
    ibus_config_watch(m_config, CONF_SECTION, CONF_NAME_SECRET);
    g_signal_connect(m_config, "value-changed", G_CALLBACK(IBusConfig_OnValueChanged), this);
    LOG_INFO("config value-changed signal connected");
}

void Engine::FocusIn() {
    LOG_TRACE("Entry");
    PropertySetup();
    LOG_TRACE("Exit");
}
void Engine::PropertySetup() {
    prop.wubi_table = ConfGetString(CONF_NAME_WUBI);
    auto pinyin = ConfGetString(CONF_NAME_PINYIN);
    if (pinyin.empty()) { // not configured yet
        pinyin = "true";
        ConfSetString(CONF_NAME_PINYIN, pinyin);
    }
    prop.pinyin = pinyin == "true";
    auto prop_list = ibus_prop_list_new();
    auto prop_pinyin = ibus_property_new(
        "pinyin",
        PROP_TYPE_TOGGLE,
        ibus_text_new_from_string(_("label_pinyin")),
        "audio_ime",
        ibus_text_new_from_string("tooltip_pinyin"),
        true,
        true,
        prop.pinyin ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED,
        nullptr);
    auto prop_speech = ibus_property_new(
        "preference",
        PROP_TYPE_NORMAL,
        ibus_text_new_from_string(_("preference")),
        "audio_ime",
        ibus_text_new_from_string("preference_tool_tip"),
        true,
        true,
        PROP_STATE_CHECKED,
        nullptr);

    auto wubi_prop_sub_list = ibus_prop_list_new();
    auto prop_wubi_table_no = ibus_property_new(
        "wubi_table_no",
        PROP_TYPE_RADIO,
        ibus_text_new_from_string(_("label_wubi_table_no")),
        "audio_ime",
        ibus_text_new_from_string("tooltip_wubi_table_no"),
        true,
        true,
        prop.wubi_table.empty() ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED,
        nullptr);
    auto prop_wubi_table_86 = ibus_property_new(
        "wubi_table_86",
        PROP_TYPE_RADIO,
        ibus_text_new_from_string(_("label_wubi_table_86")),
        "audio_ime",
        ibus_text_new_from_string("tooltip_wubi_table_86"),
        true,
        true,
        prop.wubi_table == wubi86DictPath ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED,
        nullptr);
    auto prop_wubi_table_98 = ibus_property_new(
        "wubi_table_98",
        PROP_TYPE_RADIO,
        ibus_text_new_from_string(_("label_wubi_table_98")),
        "audio_ime",
        ibus_text_new_from_string("tooltip_wubi_table_98"),
        true,
        true,
        prop.wubi_table == wubi98DictPath ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED,
        nullptr);
    ibus_prop_list_append(wubi_prop_sub_list, prop_wubi_table_no);
    ibus_prop_list_append(wubi_prop_sub_list, prop_wubi_table_86);
    ibus_prop_list_append(wubi_prop_sub_list, prop_wubi_table_98);

    auto prop_wubi = ibus_property_new(
        "wubi",
        PROP_TYPE_MENU,
        ibus_text_new_from_string(_("wubi")),
        "audio_ime",
        ibus_text_new_from_string("wubi"),
        true,
        true,
        PROP_STATE_CHECKED,
        wubi_prop_sub_list);
    g_object_ref_sink(prop_list);
    ibus_prop_list_append(prop_list, prop_wubi);
    ibus_prop_list_append(prop_list, prop_pinyin);
    ibus_prop_list_append(prop_list, prop_speech);
    ibus_engine_register_properties(m_engine, prop_list);
}

// static
void Engine::OnPropertyActivate(IBusEngine *engine, gchar *name, guint state, gpointer user_data) {
    LOG_TRACE("Entry");
    LOG_INFO("property changed, name:%s, state:%d", name, state);
    auto ime = (Engine *)user_data;
    auto oldProp = ime->prop;
    if (std::string(name) == "wubi_table_no") {
        if (state == 1) {
            ime->prop.wubi_table = "";
        }
    } else if (std::string(name) == "wubi_table_86") {
        if (state == 1) {
            ime->prop.wubi_table = ime->wubi86DictPath;
        }
    } else if (std::string(name) == "wubi_table_98") {
        if (state == 1) {
            ime->prop.wubi_table = ime->wubi98DictPath;
        }
    } else if (std::string(name) == "pinyin_table") {
        ime->prop.pinyin = state;
    }
    if (ime->prop.wubi_table != oldProp.wubi_table) {
        ime->ConfSetString(CONF_NAME_WUBI, ime->prop.wubi_table);
    }

    if (ime->prop.pinyin != oldProp.pinyin) {
        ime->ConfSetString(CONF_NAME_PINYIN, ime->prop.pinyin ? "true" : "false");
    }
    if (std::string(name) == "preference") {
        g_spawn_command_line_async("audio_ime_setup", nullptr);
    }
    LOG_TRACE("Exit");
}
void Engine::engine_commit_text(IBusEngine *engine, IBusText *text) {
    ibus_engine_commit_text(engine, text);
    ibus_engine_hide_preedit_text(engine);
    ibus_engine_hide_auxiliary_text(engine);
    m_lookupTable->Clear();
    m_lookupTable->Hide();
}
std::string Engine::IBusMakeIndicatorMsg(long recordingTime) {
    std::string msg = "press C-` to toggle record[";
    SpeechRecognizer::Status status = m_speechRecognizer->GetStatus();
    if (status == SpeechRecognizer::RECODING) {
        msg += "m_recording " + std::to_string(recordingTime);
    }
    if (status == SpeechRecognizer::WAITING) {
        msg += "m_waiting";
    }
    msg += "]";
    return msg;
}
void Engine::candidateSelected(guint index, bool ignoreText) {
    auto cand = m_lookupTable->GetCandidateGlobal(index);

    if (cand.isPinyin) {
        std::string code = m_wubi->CodeSearch(cand.text->text);
        if (code.empty()) {
            ibus_engine_hide_auxiliary_text(m_engine);
        } else {
            std::string hint = "五笔[" + code + "]";
            ibus_engine_update_auxiliary_text(m_engine, ibus_text_new_from_string(hint.c_str()), true);
            LOG_INFO("cursor:%d, text:%s, wubi code:%s - %lu", index, cand.text->text, code.c_str(), code.size());
        }
    } else {
        LOG_INFO("cursor:%d, text:%s, is not pinyin", index, cand.text->text);
        ibus_engine_hide_auxiliary_text(m_engine);
    }
    if (!ignoreText) { // which means escape
        ibus_engine_commit_text(m_engine, cand.text);
    }
    m_lookupTable->Clear();
    m_lookupTable->Update();
    m_lookupTable->Hide();
    ibus_engine_hide_preedit_text(m_engine);
    m_input.clear();
}
std::string Engine::ConfGetString(const std::string &name) const {
    std::string val;
    auto akId = ibus_config_get_value(m_config, CONF_SECTION, name.c_str());
    if (akId != nullptr) {
        auto nameVal = g_variant_get_string(akId, nullptr);
        if (nameVal == nullptr) {
            LOG_ERROR("failed to get variant");
            val = "";
        } else {
            val = nameVal;
            LOG_DEBUG("value:%s", nameVal);
        }
    } else {
        LOG_ERROR("failed to get config value for %s", name.c_str());
        val = "";
    }
    return val;
}

void Engine::ConfSetString(std::string name, std::string val) {
    auto ret = ibus_config_set_value(m_config, CONF_SECTION, name.c_str(), g_variant_new_string(val.c_str()));
    if (!ret) {
        LOG_ERROR("failed to set config %s", name.c_str());
    }
}

LookupTable::LookupTable(IBusEngine *engine) {
    m_engine = engine;
    m_table = ibus_lookup_table_new(10, 0, TRUE, TRUE);
    LOG_INFO("table %p", m_table);
    g_object_ref_sink(m_table);

    ibus_lookup_table_set_round(m_table, true);
    ibus_lookup_table_set_page_size(m_table, 5);
    ibus_lookup_table_set_orientation(m_table, IBUS_ORIENTATION_VERTICAL);
}
LookupTable::~LookupTable() {
    Clear();
    Hide();
    g_object_unref(m_table);
}
void LookupTable::Append(IBusText *text, bool pinyin) {
    ibus_lookup_table_append_candidate(m_table, text);
    m_candidateAttrs.emplace_back(pinyin);
}
void LookupTable::Show() { ibus_engine_show_lookup_table(m_engine); }
void LookupTable::Hide() { ibus_engine_hide_lookup_table(m_engine); }
void LookupTable::PageUp() { ibus_lookup_table_page_up(m_table); }
void LookupTable::PageDown() { ibus_lookup_table_page_down(m_table); }
void LookupTable::CursorDown() {
    bool ret = ibus_lookup_table_cursor_down(m_table);
    if (!ret) {
        LOG_ERROR("failed to put cursor down");
    }
}
void LookupTable::CursorUp() {
    bool ret = ibus_lookup_table_cursor_up(m_table);
    if (!ret) {
        LOG_ERROR("failed to put cursor up");
    }
}

Candidate LookupTable::GetCandidateGlobal(guint globalCursor) {
    Candidate cand;
    auto text = ibus_lookup_table_get_candidate(m_table, globalCursor);
    auto attr = m_candidateAttrs[globalCursor];
    cand.isPinyin = attr._isPinyin;
    cand.text = text;
    return cand;
}
guint LookupTable::GetGlobalCursor(int index) {
    guint cursor = ibus_lookup_table_get_cursor_pos(m_table);
    if (index >= 0) {
        guint cursor_page = ibus_lookup_table_get_cursor_in_page(m_table);
        cursor = cursor + (index - cursor_page) - 1;
    }
    return cursor;
}
void LookupTable::Update() { ibus_engine_update_lookup_table_fast(m_engine, m_table, true); }
void LookupTable::Clear() {
    ibus_lookup_table_clear(m_table);
    m_candidateAttrs.clear();
}
