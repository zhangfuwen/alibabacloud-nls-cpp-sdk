//
// Created by zhangfuwen on 2022/1/25.
//

#ifndef AUDIO_IME_COMMON_H
#define AUDIO_IME_COMMON_H
#include <ibus.h>
#include <glib/gi18n.h>
#include <locale.h>

struct CandidateAttr {
    bool _isPinyin;
    explicit CandidateAttr(bool isPinyin = false) :_isPinyin(isPinyin) {}
};

#define GETTEXT_PACKAGE "messages"

#define _(string) gettext(string)

#endif // AUDIO_IME_COMMON_H
