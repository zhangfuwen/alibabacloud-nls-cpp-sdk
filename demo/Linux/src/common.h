//
// Created by zhangfuwen on 2022/1/25.
//

#ifndef AUDIO_IME_COMMON_H
#define AUDIO_IME_COMMON_H
#include <ibus.h>

struct Candidate {
    IBusText _text;
    bool _isPinyin;
    explicit Candidate(IBusText text, bool isPinyin = false) : _text(text), _isPinyin(isPinyin) {}
};

#endif // AUDIO_IME_COMMON_H
