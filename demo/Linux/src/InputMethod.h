//
// Created by zhangfuwen on 2022/1/22.
//

#ifndef AUDIO_IME_INPUTMETHOD_H
#define AUDIO_IME_INPUTMETHOD_H

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
#include "wubi.h"

class InputMethod : public ::SpeechListener {
    const std::string wubi86DictPath = "/usr/share/ibus-table/data/wubi86.txt";
    const std::string wubi98DictPath = "/usr/share/ibus-table/data/wubi98.txt";
    class Config {};
    class Property {};
    IBusLookupTable *m_table = nullptr;
    IBusEngine *m_engine = nullptr;
    Wubi *m_wubi = nullptr;
    pinyin::Pinyin *m_pinyin = nullptr;
    SpeechRecognizer *m_speechRecognizer = nullptr;
    std::string m_input;
    std::vector<Candidate> candidates = {};

  public:
    explicit InputMethod(gchar *engine_name, int id, IBusBus *bus);
    IBusEngine *getIBusEngine();
    ~InputMethod() override;
    void OnCompleted(std::string text) override;
    void OnFailed() override;
    void OnPartialResult(std::string text) override;
    void registerCallbacks();
    void SetSpeechAkId(std::string akId);
    void SetSpeechAkSecret(std::string akSecret);
    void Enable();
    void Disable();
    void IBusUpdateIndicator(long recordingTime) override;
    // early return ?
    // return value
    std::pair<bool, bool> ProcessSpeech(guint keyval, guint keycode, guint state);
    gboolean ProcessKeyEvent(guint keyval, guint keycode, guint state);
    static gboolean OnProcessKeyEvent(IBusEngine *engine, guint keyval, guint keycode, guint state);
    static void OnEnable([[maybe_unused]] IBusEngine *engine);
    static void OnDisable([[maybe_unused]] IBusEngine *engine);
    static void OnFocusOut(IBusEngine *engine);
    static void OnFocusIn([[maybe_unused]] IBusEngine *engine);
    static void OnCandidateClicked(IBusEngine *engine, guint index, guint button, guint state);
    static std::map<std::string, InputMethod *> s_engineMap;
    static InputMethod * IBusEngineToInputMethod(IBusEngine * engine);
    void FocusIn();
    static void OnPropertyActivate(IBusEngine *engine, gchar *name, guint state, gpointer user_data);
    void engine_reset(IBusEngine *engine, IBusLookupTable *table);
    void engine_commit_text(IBusEngine *engine, IBusText *text);
    std::string IBusMakeIndicatorMsg(long recordingTime);
    void candidateSelected(guint index, bool ignoreText = false);
};

#endif // AUDIO_IME_INPUTMETHOD_H
