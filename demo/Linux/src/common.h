//
// Created by zhangfuwen on 2022/1/25.
//

#ifndef AUDIO_IME_COMMON_H
#define AUDIO_IME_COMMON_H
#include <ibus.h>

struct CandidateAttr {
    bool _isPinyin;
    explicit CandidateAttr(bool isPinyin = false) :_isPinyin(isPinyin) {}
};

#endif // AUDIO_IME_COMMON_H
