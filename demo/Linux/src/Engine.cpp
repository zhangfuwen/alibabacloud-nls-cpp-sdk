//
// Created by zhangfuwen on 2022/1/22.
//

#include "Engine.h"
#include "log.h"
#include "nlsClient.h"
#include "nlsEvent.h"
#include "nlsToken.h"
#include "speechRecognizerRequest.h"
#include "wubi.h"
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <glib-object.h>
#include <glib.h>
#include <ibus.h>
#include <iostream>
#include <map>
#include <pinyinime.h>
#include <pthread.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

Engine::Engine(gchar *engine_name, int id, IBusBus *bus) {
    gchar *path = g_strdup_printf("/org/freedesktop/IBus/Engine/%i", id);
    m_engine = ibus_engine_new(engine_name, path, ibus_bus_get_connection(bus));
    LOG_INFO("[IM:iBus]: Creating IM Engine with name:%s and id:%d", engine_name, id);
    // Setup Lookup table
    m_wubi = new Wubi(wubi86DictPath);
    m_pinyin = new pinyin::Pinyin();
    m_speechRecognizer = new SpeechRecognizer(*this);
    s_engineMap[engine_name] = this;

    registerCallbacks();
}

std::map<std::string, Engine *> Engine::s_engineMap = {};
Engine *Engine::IBusEngineToInputMethod(IBusEngine *engine) { return s_engineMap[ibus_engine_get_name(engine)]; }

IBusEngine *Engine::getIBusEngine() { return m_engine; }
Engine::~Engine() {
    delete m_pinyin;
    delete m_wubi;
}
void Engine::OnCompleted(std::string text) {
    engine_commit_text(m_engine, ibus_text_new_from_string(text.c_str()));
    ibus_lookup_table_clear(m_table);
    ibus_engine_update_preedit_text(m_engine, ibus_text_new_from_string(""), 0, false);
}
void Engine::OnFailed() {
    engine_commit_text(m_engine, ibus_text_new_from_string(""));
    ibus_lookup_table_clear(m_table);
    ibus_engine_update_preedit_text(m_engine, ibus_text_new_from_string(""), 0, false);
}
void Engine::OnPartialResult(std::string text) {
    ibus_engine_update_preedit_text(m_engine, ibus_text_new_from_string(text.c_str()), 0, TRUE);
}
void Engine::registerCallbacks() {
    g_signal_connect(m_engine, "process-key-event", G_CALLBACK(OnProcessKeyEvent), nullptr);
    g_signal_connect(m_engine, "enable", G_CALLBACK(OnEnable), nullptr);
    g_signal_connect(m_engine, "disable", G_CALLBACK(OnDisable), nullptr);
    g_signal_connect(m_engine, "focus-out", G_CALLBACK(OnFocusOut), nullptr);
    g_signal_connect(m_engine, "focus-in", G_CALLBACK(OnFocusIn), nullptr);
    g_signal_connect(m_engine, "candidate-clicked", G_CALLBACK(OnCandidateClicked), nullptr);
    g_signal_connect(m_engine, "property-activate", G_CALLBACK(OnPropertyActivate), nullptr);
}
void Engine::SetSpeechAkId(std::string akId) { m_speechRecognizer->setAkId(std::move(akId)); }
void Engine::SetSpeechAkSecret(std::string akSecret) { m_speechRecognizer->setAkSecret(std::move(akSecret)); }
void Engine::Enable() {
    m_table = ibus_lookup_table_new(10, 0, TRUE, TRUE);
    LOG_INFO("table %p", m_table);
    g_object_ref_sink(m_table);

    ibus_lookup_table_set_round(m_table, true);
    ibus_lookup_table_set_page_size(m_table, 5);
    ibus_lookup_table_set_orientation(m_table, IBUS_ORIENTATION_VERTICAL);
}
void Engine::Disable() {}
void Engine::IBusUpdateIndicator(long recordingTime) {
    ibus_engine_update_auxiliary_text(
        m_engine, ibus_text_new_from_string(IBusMakeIndicatorMsg(recordingTime).c_str()), TRUE);
}
// early return ?
// return value
std::pair<bool, bool> Engine::ProcessSpeech(guint keyval, guint keycode, guint state) {
    SpeechRecognizer::Status status = m_speechRecognizer->GetStatus();
    if ((state & IBUS_CONTROL_MASK) && keycode == 41) {
        if (status == SpeechRecognizer::WAITING) {
            return {true, TRUE};
        }
        if (status != SpeechRecognizer::RECODING) {
            std::thread t1([&]() { m_speechRecognizer->Start(); });
            t1.detach();
        } else {
            m_speechRecognizer->Stop();
        }
        ibus_engine_hide_lookup_table(m_engine);
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
    LOG_TRACE("Entry");
    LOG_INFO("engine_process_key_event keycode: %d, keyval:%x", keycode, keyval);

    if (state & IBUS_RELEASE_MASK) {
        return FALSE;
    }

    if (m_speechRecognizer != nullptr) {
        auto ret = ProcessSpeech(keyval, keycode, state);
        if (ret.first) {
            return ret.second;
        }
    }

    if (state & IBUS_LOCK_MASK) {
        if (keycode == 58) {
            LOG_INFO("caps lock pressed");
            // caps lock
            m_input = "";
            ibus_lookup_table_clear(m_table);
            ibus_engine_update_auxiliary_text(m_engine, ibus_text_new_from_string(""), true);
            ibus_engine_hide_lookup_table(m_engine);
            ibus_engine_hide_preedit_text(m_engine);
            ibus_engine_hide_auxiliary_text(m_engine);
            return true;
        }
        if (keyval == IBUS_KEY_equal || keyval == IBUS_KEY_Right) {
            LOG_DEBUG("equal pressed");
            ibus_lookup_table_page_down(m_table);
            guint cursor = ibus_lookup_table_get_cursor_in_page(m_table);
            LOG_INFO("cursor pos %d", cursor);
            cursor = ibus_lookup_table_get_cursor_pos(m_table);
            LOG_INFO("cursor pos(global) %d", cursor);
            ibus_engine_update_lookup_table_fast(m_engine, m_table, true);
            //            ibus_engine_forward_key_event(m_engine, keyval,
            //            keycode, state);
            return true;
        }
        if (keyval == IBUS_KEY_minus || keyval == IBUS_KEY_Left) {
            LOG_DEBUG("minus pressed");
            ibus_lookup_table_page_up(m_table);
            guint cursor = ibus_lookup_table_get_cursor_in_page(m_table);
            LOG_INFO("cursor pos %d", cursor);
            cursor = ibus_lookup_table_get_cursor_pos(m_table);
            LOG_INFO("cursor pos(global) %d", cursor);
            ibus_lookup_table_set_cursor_pos(m_table, 3);
            ibus_engine_update_lookup_table_fast(m_engine, m_table, true);
            return true;
        }
        if (keyval == IBUS_KEY_Down) {
            LOG_DEBUG("down pressed");
            // ibus_lookup_table_cursor_down(m_table);
            bool ret = ibus_lookup_table_cursor_down(m_table);
            if (!ret) {
                LOG_ERROR("failed to put cursor down");
            }
            guint cursor = ibus_lookup_table_get_cursor_in_page(m_table);
            LOG_INFO("cursor pos %d", cursor);
            cursor = ibus_lookup_table_get_cursor_pos(m_table);
            LOG_INFO("cursor pos(global) %d", cursor);
            ibus_engine_update_lookup_table_fast(m_engine, m_table, true);
            return true;
        }
        if (keyval == IBUS_KEY_Up) {
            LOG_DEBUG("up pressed");
            bool ret = ibus_lookup_table_cursor_up(m_table);
            if (!ret) {
                LOG_ERROR("failed to put cursor up");
            }
            guint cursor = ibus_lookup_table_get_cursor_in_page(m_table);
            LOG_INFO("cursor pos %d", cursor);
            cursor = ibus_lookup_table_get_cursor_pos(m_table);
            LOG_INFO("cursor pos(global) %d", cursor);
            ibus_engine_update_lookup_table_fast(m_engine, m_table, true);
            return true;
        }
        if (keyval == IBUS_KEY_space || keyval == IBUS_KEY_Return || isdigit((char)(keyval)) || keycode == 1) {
            if (m_input.empty()) {
                return false;
            }
            LOG_DEBUG("space pressed");
            guint cursor = ibus_lookup_table_get_cursor_pos(m_table);
            if (isdigit((char)keyval)) {
                guint cursor_page = ibus_lookup_table_get_cursor_in_page(m_table);
                int index = (int)(keyval - IBUS_KEY_0);
                cursor = cursor + (index - cursor_page) - 1;
                LOG_DEBUG("cursor_page:%d, index:%d, cursor:%d", cursor_page, index, cursor);
                ibus_lookup_table_set_cursor_pos(m_table, cursor);
            }
            candidateSelected(cursor, keycode == 1);
            return true;
        }

        LOG_DEBUG("keyval %x, m_input.size:%lu", keyval, m_input.size());
        if (keyval == IBUS_KEY_BackSpace && !m_input.empty()) {
            m_input = m_input.substr(0, m_input.size() - 1);
        } else {
            if (!isalpha((char)keyval)) {
                // only process letters and numbers
                return false;
            }
            m_input += (char)tolower((int)keyval);
        }
        // chinese mode
        ibus_engine_update_auxiliary_text(m_engine, ibus_text_new_from_string(m_input.c_str()), true);

        // get pinyin candidates
        unsigned int numCandidates = 0;
        if (g_pinyin_table) {
            numCandidates = m_pinyin->Search(m_input);
        }
        LOG_INFO("num candidates %u for %s", numCandidates, m_input.c_str());

        // get wubi candidates
        LOG_DEBUG("");
        TrieNode *x = m_wubi->Search(m_input);
        std::map<uint64_t, std::string> m;
        SubTreeTraversal(m, x); // insert subtree and reorder

        ibus_lookup_table_clear(m_table);
        candidates.clear();
        if (x != nullptr && x->isEndOfWord) {
            auto it = x->values.rbegin();
            std::string candidate = it->second;
            // best exact match first
            auto text = ibus_text_new_from_string(candidate.c_str());
            candidates.emplace_back(*text);
            ibus_lookup_table_append_candidate(m_table, text);
            LOG_INFO("first %s", text->text);
            it++;
            while (it != x->values.rend()) {
                m.insert(*it); // insert to reorder
                it++;
            }
        }

        int j = 0;
        LOG_INFO("map size:%lu", m.size());
        auto it = m.rbegin();
        while (true) {
            if (j >= numCandidates && it == m.rend()) {
                break;
            }
            if (it != m.rend()) {
                auto value = it->second;
                std::string &candidate = value;
                auto text = ibus_text_new_from_string(candidate.c_str());
                candidates.emplace_back(*text);
                ibus_lookup_table_append_candidate(m_table, text);
                it++;
            }
            if (j < numCandidates) {
                std::wstring buffer = m_pinyin->GetCandidate(j);
                glong items_read;
                glong items_written;
                GError *error;
                gunichar *utf32_str = g_utf16_to_ucs4(
                    reinterpret_cast<const gunichar2 *>(buffer.data()),
                    buffer.size(),
                    &items_read,
                    &items_written,
                    &error);
                auto text = ibus_text_new_from_ucs4(utf32_str);
                candidates.emplace_back(*text, true);
                ibus_lookup_table_append_candidate(m_table, text);
                j++;
            }
        }
        /*
        for(auto cand : candidates) {
             ibus_lookup_table_append_candidate(m_table, &cand._text);
        }
         */

        ibus_engine_update_lookup_table_fast(m_engine, m_table, TRUE);
        ibus_engine_show_lookup_table(m_engine);

        return true;

    } else {
        // english mode
        ibus_engine_hide_lookup_table(m_engine);
        ibus_engine_hide_preedit_text(m_engine);
        ibus_engine_hide_auxiliary_text(m_engine);
        return false;
    }
}

// static
gboolean Engine::OnProcessKeyEvent(IBusEngine *engine, guint keyval, guint keycode, guint state) {
    IBusEngineToInputMethod(engine)->ProcessKeyEvent(keyval, keycode, state);
}

// static
void Engine::OnEnable([[maybe_unused]] IBusEngine *engine) { IBusEngineToInputMethod(engine)->Enable(); }

// static
void Engine::OnDisable([[maybe_unused]] IBusEngine *engine) { IBusEngineToInputMethod(engine)->Disable(); }

// static
void Engine::OnFocusOut(IBusEngine *engine) {}

// static
void Engine::OnFocusIn([[maybe_unused]] IBusEngine *engine) { IBusEngineToInputMethod(engine)->FocusIn(); }

// static
void Engine::OnCandidateClicked(IBusEngine *engine, guint index, guint button, guint state) {
    IBusEngineToInputMethod(engine)->candidateSelected(index);
}

void Engine::FocusIn() {
    LOG_TRACE("Entry");
    PropertySetup();
    LOG_TRACE("Exit");
}
void Engine::PropertySetup() const {
    auto prop_list = ibus_prop_list_new();
    auto prop_pinyin = ibus_property_new(
        "pinyin",
        PROP_TYPE_TOGGLE,
        ibus_text_new_from_string("label_pinyin"),
        "audio_ime",
        ibus_text_new_from_string("tooltip_pinyin"),
        true,
        true,
        g_pinyin_table ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED,
        nullptr);
    auto prop_speech = ibus_property_new(
        "preference",
        PROP_TYPE_NORMAL,
        ibus_text_new_from_string("preference"),
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
        ibus_text_new_from_string("label_wubi_table_no"),
        "audio_ime",
        ibus_text_new_from_string("tooltip_wubi_table_no"),
        true,
        true,
        g_wubi86_table ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED,
        nullptr);
    auto prop_wubi_table_86 = ibus_property_new(
        "wubi_table_86",
        PROP_TYPE_RADIO,
        ibus_text_new_from_string("label_wubi_table_86"),
        "audio_ime",
        ibus_text_new_from_string("tooltip_wubi_table_86"),
        true,
        true,
        g_wubi86_table ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED,
        nullptr);
    auto prop_wubi_table_98 = ibus_property_new(
        "wubi_table_98",
        PROP_TYPE_RADIO,
        ibus_text_new_from_string("label_wubi_table_98"),
        "audio_ime",
        ibus_text_new_from_string("tooltip_wubi_table_98"),
        true,
        true,
        g_wubi86_table ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED,
        nullptr);
    ibus_prop_list_append(wubi_prop_sub_list, prop_wubi_table_no);
    ibus_prop_list_append(wubi_prop_sub_list, prop_wubi_table_86);
    ibus_prop_list_append(wubi_prop_sub_list, prop_wubi_table_98);

    auto prop_wubi = ibus_property_new(
        "wubi",
        PROP_TYPE_MENU,
        ibus_text_new_from_string("wubi"),
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
    auto ime = IBusEngineToInputMethod(engine);
    if (std::string(name) == "wubi_table_no") {
        if(state == 1) {
            ime->prop.wubi_table = "";
        }
    } else if (std::string(name) == "wubi_table_86") {
        if(state == 1) {
            ime->prop.wubi_table = ime->wubi86DictPath;
        }
    } else if (std::string(name) == "wubi_table_98") {
        if(state == 1) {
            ime->prop.wubi_table = ime->wubi98DictPath;
        }
    } else if (std::string(name) == "pinyin_table") {
        ime->prop.pinyin = state;
    } else if (std::string(name) == "preference") {
        g_spawn_command_line_async("audio_ime_setup", nullptr);
    }
    LOG_TRACE("Exit");
}
void Engine::engine_reset(IBusEngine *engine, IBusLookupTable *table) {
    ibus_lookup_table_clear(table);
    ibus_engine_hide_preedit_text(engine);
    ibus_engine_hide_auxiliary_text(engine);
    ibus_engine_hide_lookup_table(engine);
}
void Engine::engine_commit_text(IBusEngine *engine, IBusText *text) {
    ibus_engine_commit_text(engine, text);
    engine_reset(engine, m_table);
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
    auto text = ibus_lookup_table_get_candidate(m_table, index);

    if (candidates[index]._isPinyin) {
        std::string code = m_wubi->CodeSearch(text->text);
        if (code.empty()) {
            ibus_engine_hide_auxiliary_text(m_engine);
        } else {
            std::string hint = "五笔[" + code + "]";
            ibus_engine_update_auxiliary_text(m_engine, ibus_text_new_from_string(hint.c_str()), true);
            LOG_INFO("cursor:%d, text:%s, wubi code:%s - %lu", index, text->text, code.c_str(), code.size());
        }
    } else {
        LOG_INFO("cursor:%d, text:%s, is not pinyin", index, text->text);
        ibus_engine_hide_auxiliary_text(m_engine);
    }
    if (!ignoreText) { // which means escape
        ibus_engine_commit_text(m_engine, text);
    }
    ibus_lookup_table_clear(m_table);
    ibus_engine_update_lookup_table_fast(m_engine, m_table, true);
    ibus_engine_hide_lookup_table(m_engine);
    ibus_engine_hide_preedit_text(m_engine);
    m_input.clear();
    candidates.clear();
}
