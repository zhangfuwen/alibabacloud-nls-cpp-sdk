//
// Created by zhangfuwen on 2022/1/22.
//

#ifndef AUDIO_IME_FUNENGINE_H
#define AUDIO_IME_FUNENGINE_H

#include "PinyinIME.h"
#include "SpeechRecognizer.h"
#include "log.h"
#include "nlsClient.h"
#include "nlsEvent.h"
#include "nlsToken.h"
#include "speechRecognizerRequest.h"
#include "wubi.h"
#include "RuntimeOptions.h"
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
    explicit LookupTable(IBusEngine * engine);
    ~LookupTable() ;
    void Append(IBusText *text, bool pinyin);
    void Show();
    void Hide();
    void PageUp();
    void PageDown();
    void CursorDown();
    void CursorUp();
    Candidate GetCandidateGlobal(guint globalCursor);
    guint GetGlobalCursor(int index);
    void Update();
    void Clear();
};

class FunEngine : public ::SpeechListener {
private:
    const std::string wubi86DictPath = "/usr/share/ibus-table/data/wubi86.txt";
    const std::string wubi98DictPath = "/usr/share/ibus-table/data/wubi98.txt";

    IBusEngine *m_engine = nullptr;
    Wubi *m_wubi = nullptr;
    pinyin::Pinyin *m_pinyin = nullptr;
    SpeechRecognizer *m_speechRecognizer = nullptr;
    std::string m_input;

    LookupTable *m_lookupTable = nullptr;
    RuntimeOptions * m_options = nullptr;


    // early return ?
    // return value
    std::pair<bool, bool> ProcessSpeech(guint keyval, guint keycode, guint state);
    void engine_commit_text(IBusEngine *engine, IBusText *text);
    std::string IBusMakeIndicatorMsg(long recordingTime);
    void candidateSelected(guint index, bool ignoreText = false);
    void PropertySetup();
    bool LookupTableNavigate(guint keyval);
    void ToggleToEnglishMode();
    void WubiPinyinQuery();

public:
    explicit FunEngine(IBusEngine * engine);
    IBusEngine *getIBusEngine();
    ~FunEngine() override;
    void OnCompleted(std::string text) override;
    void OnFailed() override;
    void OnPartialResult(std::string text) override;
    void Enable();
    void Disable();
    void FocusIn();
    void FocusOut();
    void PageUp() { m_lookupTable->PageUp();}
    void PageDown() { m_lookupTable->PageDown();}
    void CursorUp() { m_lookupTable->CursorUp();}
    void CursorDown() { m_lookupTable->CursorDown();}
    void Reset() { LOG_TRACE(""); }
    void IBusUpdateIndicator(long recordingTime) override;
    void OnPropertyActivate(IBusEngine *engine, const gchar *name, guint state);
    gboolean ProcessKeyEvent(guint keyval, guint keycode, guint state);
    void OnCandidateClicked(IBusEngine *engine, guint index, guint button, guint state);
};

#endif // AUDIO_IME_FUNENGINE_H
