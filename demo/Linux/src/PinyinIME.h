//
// Created by zhangfuwen on 2022/1/22.
//

#ifndef AUDIO_IME_PINYINIME_H
#define AUDIO_IME_PINYINIME_H

#include "pinyinime.h"
#include <string>
using namespace std;
extern volatile bool g_pinyin_table;
namespace pinyin {

class PinyinIME {
  public:
    PinyinIME() ;
    unsigned int Search(const basic_string<char> &input);
    basic_string<wchar_t> GetCandidate(int index);
    ~PinyinIME();
};
class Pinyin : public PinyinIME {};

} // namespace pinyin

#endif // AUDIO_IME_PINYINIME_H
