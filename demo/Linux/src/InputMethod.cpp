//
// Created by zhangfuwen on 2022/1/22.
//

#include "InputMethod.h"
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

InputMethod::InputMethod(gchar *engine_name, int id, IBusBus *bus) {
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

std::map<std::string, InputMethod *> InputMethod::s_engineMap = {};
InputMethod * InputMethod::IBusEngineToInputMethod(IBusEngine * engine) {
    return s_engineMap[ibus_engine_get_name(engine)];
}


IBusEngine *InputMethod::getIBusEngine() { return m_engine; }
InputMethod::~InputMethod() {
    delete m_pinyin;
    delete m_wubi;
}
void InputMethod::OnCompleted(std::string text) {
    engine_commit_text(m_engine, ibus_text_new_from_string(text.c_str()));
    ibus_lookup_table_clear(m_table);
    ibus_engine_update_preedit_text(m_engine, ibus_text_new_from_string(""), 0, false);
}
void InputMethod::OnFailed() {
    engine_commit_text(m_engine, ibus_text_new_from_string(""));
    ibus_lookup_table_clear(m_table);
    ibus_engine_update_preedit_text(m_engine, ibus_text_new_from_string(""), 0, false);
}
void InputMethod::OnPartialResult(std::string text) {
    ibus_engine_update_preedit_text(m_engine, ibus_text_new_from_string(text.c_str()), 0, TRUE);
}
void InputMethod::registerCallbacks() {
    g_signal_connect(m_engine, "process-key-event", G_CALLBACK(OnProcessKeyEvent), nullptr);
    g_signal_connect(m_engine, "enable", G_CALLBACK(OnEnable), nullptr);
    g_signal_connect(m_engine, "disable", G_CALLBACK(OnDisable), nullptr);
    g_signal_connect(m_engine, "focus-out", G_CALLBACK(OnFocusOut), nullptr);
    g_signal_connect(m_engine, "focus-in", G_CALLBACK(OnFocusIn), nullptr);
    g_signal_connect(m_engine, "candidate-clicked", G_CALLBACK(OnCandidateClicked), nullptr);
    g_signal_connect(m_engine, "property-activate", G_CALLBACK(OnPropertyActivate), nullptr);
}
void InputMethod::SetSpeechAkId(std::string akId) { m_speechRecognizer->setAkId(std::move(akId)); }
void InputMethod::SetSpeechAkSecret(std::string akSecret) { m_speechRecognizer->setAkSecret(std::move(akSecret)); }
void InputMethod::Enable() {
    m_table = ibus_lookup_table_new(10, 0, TRUE, TRUE);
    LOG_INFO("table %p", m_table);
    g_object_ref_sink(m_table);

    ibus_lookup_table_set_round(m_table, true);
    ibus_lookup_table_set_page_size(m_table, 5);
    ibus_lookup_table_set_orientation(m_table, IBUS_ORIENTATION_VERTICAL);
}
void InputMethod::Disable() {}
void InputMethod::IBusUpdateIndicator(long recordingTime) {
    ibus_engine_update_auxiliary_text(m_engine, ibus_text_new_from_string(IBusMakeIndicatorMsg(recordingTime).c_str()),
                                      TRUE);
}
// early return ?
// return value
std::pair<bool, bool> InputMethod::ProcessSpeech(guint keyval, guint keycode, guint state) {
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
gboolean InputMethod::ProcessKeyEvent(guint keyval, guint keycode, guint state) {
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
                gunichar *utf32_str = g_utf16_to_ucs4(reinterpret_cast<const gunichar2 *>(buffer.data()), buffer.size(),
                                                      &items_read, &items_written, &error);
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
gboolean InputMethod::OnProcessKeyEvent(IBusEngine *engine, guint keyval, guint keycode, guint state) {
    IBusEngineToInputMethod(engine)->ProcessKeyEvent(keyval, keycode, state);
}

// static
void InputMethod::OnEnable([[maybe_unused]] IBusEngine *engine) { IBusEngineToInputMethod(engine)->Enable(); }

// static
void InputMethod::OnDisable([[maybe_unused]] IBusEngine *engine) { IBusEngineToInputMethod(engine)->Disable(); }

// static
void InputMethod::OnFocusOut(IBusEngine *engine) {}

// static
void InputMethod::OnFocusIn([[maybe_unused]] IBusEngine *engine) { IBusEngineToInputMethod(engine)->FocusIn(); }

// static
void InputMethod::OnCandidateClicked(IBusEngine *engine, guint index, guint button, guint state) {
    IBusEngineToInputMethod(engine)->candidateSelected(index);
}

void InputMethod::FocusIn() {
    LOG_TRACE("Entry");
    auto prop_list = ibus_prop_list_new();
    LOG_DEBUG("");
    auto prop1 = ibus_property_new("wubi86_table", PROP_TYPE_TOGGLE, ibus_text_new_from_string("use五笔86table"),
                                   "audio_ime", ibus_text_new_from_string("use五笔86table"), true, true,
                                   g_wubi86_table ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED, nullptr);
    auto prop2 = ibus_property_new("wubi98_table", PROP_TYPE_TOGGLE, ibus_text_new_from_string("use wubi 98 table"),
                                   "audio_ime", ibus_text_new_from_string("use wubii 98 table"), true, true,
                                   g_wubi98_table ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED, nullptr);
    auto prop3 = ibus_property_new("pinyin_table", PROP_TYPE_TOGGLE, ibus_text_new_from_string("use拼音table"),
                                   "audio_ime", ibus_text_new_from_string("use拼音table"), true, true,
                                   g_pinyin_table ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED, nullptr);
    auto propx =
        ibus_property_new("preference", PROP_TYPE_NORMAL, ibus_text_new_from_string("preference"), "audio_ime",
                          ibus_text_new_from_string("preference_tool_tip"), true, true, PROP_STATE_CHECKED, nullptr);
    g_object_ref_sink(prop_list);
    LOG_DEBUG("");
    ibus_prop_list_append(prop_list, prop1);
    ibus_prop_list_append(prop_list, prop2);
    ibus_prop_list_append(prop_list, prop3);
    ibus_prop_list_append(prop_list, propx);
    LOG_DEBUG("");
    ibus_engine_register_properties(m_engine, prop_list);
    LOG_DEBUG("");
    LOG_TRACE("Exit");
}
void InputMethod::OnPropertyActivate(IBusEngine *engine, gchar *name, guint state, gpointer user_data) {
    LOG_TRACE("Entry");
    LOG_INFO("property changed, name:%s, state:%d", name, state);
    if (std::string(name) == "wubi98_table") {
        g_wubi98_table = (bool)state;
        //            if(wubi::g_root == nullptr) {
        //                wubi::TrieImportWubiTable();
        //            }
    } else if (std::string(name) == "wubi86_table") {
        g_wubi86_table = (bool)state;
        //            if(wubi::g_root == nullptr) {
        //                wubi::TrieImportWubiTable();
        //            }
    } else if (std::string(name) == "pinyin_table") {
        g_pinyin_table = (bool)state;
    } else if (std::string(name) == "preference") {
//        auto engine_desc = ibus_bus_get_global_engine(g_bus);
//        gchar setup[1024];
//        const gchar *setup_path = ibus_engine_desc_get_setup(engine_desc);
        g_spawn_command_line_async("audio_ime_setup", nullptr);
//        LOG_DEBUG("setup path--:%s", setup_path);
//        g_object_unref(G_OBJECT(engine_desc));
    }
    LOG_TRACE("Exit");
}
void InputMethod::engine_reset(IBusEngine *engine, IBusLookupTable *table) {
    ibus_lookup_table_clear(table);
    ibus_engine_hide_preedit_text(engine);
    ibus_engine_hide_auxiliary_text(engine);
    ibus_engine_hide_lookup_table(engine);
}
void InputMethod::engine_commit_text(IBusEngine *engine, IBusText *text) {
    ibus_engine_commit_text(engine, text);
    engine_reset(engine, m_table);
}
std::string InputMethod::IBusMakeIndicatorMsg(long recordingTime) {
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
void InputMethod::candidateSelected(guint index, bool ignoreText) {
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
