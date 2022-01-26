//
// Created by zhangfuwen on 2022/1/22.
//

#ifndef AUDIO_IME_ENGINE_H
#define AUDIO_IME_ENGINE_H

#include "PinyinIME.h"
#include "SpeechRecognizer.h"
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

class Engine : public ::SpeechListener {
private:
    const std::string wubi86DictPath = "/usr/share/ibus-table/data/wubi86.txt";
    const std::string wubi98DictPath = "/usr/share/ibus-table/data/wubi98.txt";
    struct Property {
        std::string wubi_table = "";
        bool pinyin = true;
        bool speech = true;
    };

    struct Candidate {
        bool isPinyin;
        IBusText * text;
        std::string code;
    };

    class LookupTable {
        IBusEngine * m_engine = nullptr;
        IBusLookupTable *m_table = nullptr;
        std::vector<CandidateAttr> m_candidateAttrs = {};

    public:
        explicit LookupTable(IBusEngine * engine) {
            m_engine = engine;
            m_table = ibus_lookup_table_new(10, 0, TRUE, TRUE);
            LOG_INFO("table %p", m_table);
            g_object_ref_sink(m_table);

            ibus_lookup_table_set_round(m_table, true);
            ibus_lookup_table_set_page_size(m_table, 5);
            ibus_lookup_table_set_orientation(m_table, IBUS_ORIENTATION_VERTICAL);
        }
        ~LookupTable() {
            Clear();
            Hide();
            g_object_unref(m_table);
        }
        void Append(IBusText *text, bool pinyin) {
            ibus_lookup_table_append_candidate(m_table, text);
            m_candidateAttrs.emplace_back(pinyin);
        }
        void Show() {
            ibus_engine_show_lookup_table(m_engine);
        }
        void Hide() {
            ibus_engine_hide_lookup_table(m_engine);
        }
        void PageUp() {
            ibus_lookup_table_page_up(m_table);
        }
        void PageDown() {
            ibus_lookup_table_page_down(m_table);
        }
        void CursorDown() {
            bool ret = ibus_lookup_table_cursor_down(m_table);
            if (!ret) {
                    LOG_ERROR("failed to put cursor down");
            }
        }
        void CursorUp() {
            bool ret = ibus_lookup_table_cursor_up(m_table);
            if (!ret) {
                    LOG_ERROR("failed to put cursor up");
            }
        }

        Candidate GetCandidateGlobal(guint globalCursor) {
            Candidate cand;
            auto text = ibus_lookup_table_get_candidate(m_table, globalCursor);
            auto attr = m_candidateAttrs[globalCursor];
            cand.isPinyin = attr._isPinyin;
            cand.text = text;
            return cand;
        }
        guint GetGlobalCursor(int index) {
            guint cursor = ibus_lookup_table_get_cursor_pos(m_table);
            if(index >= 0) {
                guint cursor_page = ibus_lookup_table_get_cursor_in_page(m_table);
                cursor = cursor + (index - cursor_page) - 1;
            }
            return cursor;
        }
        void Update() {
            ibus_engine_update_lookup_table_fast(m_engine, m_table, true);
        }
        void Clear() {
            ibus_lookup_table_clear(m_table);
            m_candidateAttrs.clear();
        }
    };

    Property prop{};
    IBusEngine *m_engine = nullptr;
    IBusConfig *m_config = nullptr;
    Wubi *m_wubi = nullptr;
    pinyin::Pinyin *m_pinyin = nullptr;
    SpeechRecognizer *m_speechRecognizer = nullptr;
    std::string m_input;

    LookupTable *m_lookupTable = nullptr;


    std::string ConfGetString(const std::string &name) const;
    void ConfSetString(std::string name, std::string val);

    // early return ?
    // return value
    std::pair<bool, bool> ProcessSpeech(guint keyval, guint keycode, guint state);
    gboolean ProcessKeyEvent(guint keyval, guint keycode, guint state);
    static gboolean OnProcessKeyEvent(IBusEngine *engine, guint keyval, guint keycode, guint state, void *userdata);
    static void OnEnable([[maybe_unused]] IBusEngine *engine, gpointer userdata);
    static void OnDisable([[maybe_unused]] IBusEngine *engine, gpointer userdata);
    static void OnFocusOut(IBusEngine *engine, gpointer userdata);
    static void OnFocusIn([[maybe_unused]] IBusEngine *engine, gpointer userdata);
    static void OnCandidateClicked(IBusEngine *engine, guint index, guint button, guint state, gpointer userdata);
    static void
    IBusConfig_OnValueChanged(IBusConfig *config, gchar *section, gchar *name, GVariant *value, gpointer user_data);
    void FocusIn();
    static void OnPropertyActivate(IBusEngine *engine, gchar *name, guint state, gpointer user_data);
    void engine_reset(IBusEngine *engine, IBusLookupTable *table);
    void engine_commit_text(IBusEngine *engine, IBusText *text);
    std::string IBusMakeIndicatorMsg(long recordingTime);
    void candidateSelected(guint index, bool ignoreText = false);
    void PropertySetup();
    bool LookupTableNavigate(guint keyval);

public:
    explicit Engine(gchar *engine_name, int id, IBusBus *bus);
    IBusEngine *getIBusEngine();
    ~Engine() override;
    void OnCompleted(std::string text) override;
    void OnFailed() override;
    void OnPartialResult(std::string text) override;
    void registerCallbacks();
    void SetSpeechAkId(std::string akId);
    void SetSpeechAkSecret(std::string akSecret);
    void Enable();
    void Disable();
    void IBusUpdateIndicator(long recordingTime) override;
    void IBusConfigSetup(GDBusConnection *conn);
};

#endif // AUDIO_IME_ENGINE_H
