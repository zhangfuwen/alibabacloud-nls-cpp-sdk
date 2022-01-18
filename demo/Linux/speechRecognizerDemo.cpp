/*
 * Copyright 2021 Alibaba Group Holding Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <glib-2.0/glib.h>
#include <glib-object.h>
#include <ibus.h>
#include <iostream>
#include <map>
#include <pinyinime.h>
#include <pthread.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <string>
#include <thread>
#include <vector>

#include "log.h"
#include "nlsClient.h"
#include "nlsEvent.h"
#include "nlsToken.h"
#include "profile_scan.h"
#include "speechRecognizerRequest.h"

const int ALPHABET_SIZE = 26;
#define BUFSIZE 1024
#define FRAME_100MS 3200
#define SAMPLE_RATE 16000
#define DEFAULT_STRING_LEN 128

#define CONF_SECTION "engine/audio_ime"
#define CONF_NAME "xxxx"


/**
 * 全局维护一个服务鉴权token和其对应的有效期时间戳，
 * 每次调用服务之前，首先判断token是否已经过期，
 * 如果已经过期，则根据AccessKey ID和AccessKey Secret重新生成一个token，
 * 并更新这个全局的token和其有效期时间戳。
 *
 * 注意：不要每次调用服务之前都重新生成新token，
 * 只需在token即将过期时重新生成即可。所有的服务并发可共用一个token。
 */
// 自定义线程参数

struct ParamStruct1 {
    char token[DEFAULT_STRING_LEN];
    char appkey[DEFAULT_STRING_LEN];
    char url[DEFAULT_STRING_LEN];
};

// 自定义事件回调参数
struct ParamCallBack1 {
  public:
    explicit ParamCallBack1(ParamStruct1 *param) {
        tParam = param;
        pthread_mutex_init(&mtxWord, nullptr);
        pthread_cond_init(&cvWord, nullptr);
    };

    ~ParamCallBack1() {
        tParam = nullptr;
        pthread_mutex_destroy(&mtxWord);
        pthread_cond_destroy(&cvWord);
    };

    unsigned long userId{}; // 这里用线程号
    char userInfo[8]{};

    pthread_mutex_t mtxWord{};
    pthread_cond_t cvWord{};

    ParamStruct1 *tParam;
};

void engine_commit_text(IBusEngine *engine, IBusText *text);
void IBusUpdateIndicator();


static gint id = 0;
static IBusEngine *g_engine = nullptr;
static IBusLookupTable *g_table = nullptr;
static IBusBus *g_bus;
static IBusConfig *g_config = nullptr;

volatile static bool recording = false; // currently recording user voice
volatile static bool waiting =
    false; // waiting for converted text from internet
volatile static long recordingTime = 0; // can only record 60 seconds;

std::string audio_text;
std::string wbpy_input;
guint mixed_input_state = 0;

namespace wubi {
// trie node
struct TrieNode {
    struct TrieNode *children[ALPHABET_SIZE] = {};

    // isEndOfWord is true if the node represents
    // end of a word
    bool isEndOfWord = false;
    std::string word;
    std::map<uint64_t, std::string> values = {};
};

// Returns new trie node (initialized to nullptrs)
struct TrieNode *NewNode() {
    auto pNode = new TrieNode;

    pNode->isEndOfWord = false;

    for (auto &i : pNode->children)
        i = nullptr;

    return pNode;
}

TrieNode *g_root = nullptr;
// If not present, inserts key into trie
// If the key is prefix of trie node, just
// marks leaf node
void TrieInsert(struct TrieNode *root, const std::string &key,
                const std::string &value, uint64_t freq) {
    struct TrieNode *pCrawl = root;

    for (char i : key) {
        int index = i - 'a';
        if (!pCrawl->children[index])
            pCrawl->children[index] = NewNode();

        pCrawl = pCrawl->children[index];
    }

    // mark last node as leaf
    pCrawl->isEndOfWord = true;
    pCrawl->word = key;
    pCrawl->values.insert({freq, value});
}

// Returns true if key presents in trie, else
// false
TrieNode *TrieSearch(struct TrieNode *root, const std::string &key) {
    if (root == nullptr) {
        return nullptr;
    }
    struct TrieNode *pCrawl = root;

    for (char i : key) {
        int index = i - 'a';
        if (!pCrawl->children[index])
            return nullptr;

        pCrawl = pCrawl->children[index];
    }

    return pCrawl;
}

void TrieTraversal(std::map<uint64_t, std::string> &m, struct TrieNode *root) {
    if (root == nullptr) {
        return;
    }
    for (auto &index : root->children) {
        if (index && index->isEndOfWord) {
            for (const auto &pair : index->values) {
                auto freq = pair.first;
                auto value = pair.second;
                m.insert({freq, value});
            }
        }
        TrieTraversal(m, index);
    }
}

void TrieImportWubiTable() {
    g_root = NewNode();

    std::string s1;
    s1.reserve(256);
    bool has_began = false;

    for (std::ifstream f2("/usr/share/ibus-table/data/wubi86.txt");
         getline(f2, s1);) {
        if (s1 == "BEGIN_TABLE") {
            has_began = true;
            continue;
        }
        if (!has_began) {
            continue;
        }
        if (s1 == "END_TABLE") {
            continue;
        }
        auto first_space = s1.find_first_of(" \t");
        std::string key = s1.substr(0, first_space);
        s1 = s1.substr(first_space + 1);
        auto second_space = s1.find_first_of("\t");
        std::string value = s1.substr(0, second_space);
        std::string freq_str = s1.substr(second_space + 1);
        uint64_t freq = std::stoll(freq_str);
        TrieInsert(g_root, key, value, freq);
    }
}

}; // namespace wubi

namespace speech {
static int frame_size = FRAME_100MS;
static int encoder_type = ENCODER_NONE;
std::string g_akId = "todo";
std::string g_akSecret = "todo";
std::string g_token;
long g_expireTime = 0;

/**
 * 根据AccessKey ID和AccessKey Secret重新生成一个token，并获取其有效期时间戳
 */
int NetGenerateToken(const std::string &akId, const std::string &akSecret,
                     std::string *token, long *expireTime) {
    AlibabaNlsCommon::NlsToken nlsTokenRequest;
    nlsTokenRequest.setAccessKeyId(akId);
    nlsTokenRequest.setKeySecret(akSecret);
    //  nlsTokenRequest.setDomain("nls-meta-vpc-pre.aliyuncs.com");

    if (-1 == nlsTokenRequest.applyNlsToken()) {
        LOG_INFO("Failed:%s", nlsTokenRequest.getErrorMsg());
        return -1;
    }

    *token = nlsTokenRequest.getToken();
    *expireTime = nlsTokenRequest.getExpireTime();

    return 0;
}

/**
 * @brief 调用start(), 成功与云端建立连接, sdk内部线程上报started事件
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为nullptr, 可以根据需求自定义参数
 * @return
 */
void OnRecognitionStarted(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    if (cbParam) {
        auto *tmpParam = (ParamCallBack1 *)cbParam;
        LOG_INFO("OnRecognitionStarted userId:%lu, %s", tmpParam->userId,
                 tmpParam->userInfo); // 仅表示自定义参数示例
        //通知发送线程start()成功, 可以继续发送数据
        pthread_mutex_lock(&(tmpParam->mtxWord));
        pthread_cond_signal(&(tmpParam->cvWord));
        pthread_mutex_unlock(&(tmpParam->mtxWord));
    }

    LOG_INFO(
        "OnRecognitionStarted: status code:%d, task id:%s",
        cbEvent
            ->getStatusCode(), // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
        cbEvent->getTaskId()); // 当前任务的task id，方便定位问题，建议输出
}

/**
 * @brief 设置允许返回中间结果参数, sdk在接收到云端返回到中间结果时,
 *        sdk内部线程上报ResultChanged事件
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为nullptr, 可以根据需求自定义参数
 * @return
 */

void OnRecognitionResultChanged(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    LOG_INFO("result changed");
    ibus_engine_update_preedit_text(
        g_engine, ibus_text_new_from_string(cbEvent->getResult()), 0, TRUE);
    //    ibus_lookup_table_append_candidate(g_table,
    //    ibus_text_new_from_string(cbEvent->getResult()));
    //    ibus_engine_update_lookup_table_fast(g_engine, g_table, TRUE); // this
    //    line determines if lookup table is displayed
    //    ibus_lookup_table_set_cursor_pos(g_table, 0);
}

/**
 * @brief sdk在接收到云端返回识别结束消息时, sdk内部线程上报Completed事件
 * @note 上报Completed事件之后, SDK内部会关闭识别连接通道.
 *       此时调用sendAudio会返回-1, 请停止发送.
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为nullptr, 可以根据需求自定义参数
 * @return
 */
void OnRecognitionCompleted(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {

    if (cbParam) {
        auto *tmpParam = (ParamCallBack1 *)cbParam;
        if (!tmpParam->tParam)
            return;
        LOG_INFO("OnRecognitionCompleted: userId %lu, %s", tmpParam->userId,
                 tmpParam->userInfo); // 仅表示自定义参数示例
    }

    LOG_INFO(
        "OnRecognitionCompleted: status code:%d, task id:%s, result:%s",
        cbEvent
            ->getStatusCode(), // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
        cbEvent->getTaskId(), // 当前任务的task id，方便定位问题，建议输出
        cbEvent->getResult()); // 获取中间识别结果

    audio_text = cbEvent->getResult();

    LOG_INFO("OnRecognitionCompleted: All response:%s",
             cbEvent->getAllResponse()); // 获取服务端返回的全部信息

    waiting = false;
    recording = false;
    engine_commit_text(g_engine, ibus_text_new_from_string(audio_text.c_str()));
    audio_text = "";
    ibus_lookup_table_clear(g_table);
    ibus_engine_update_preedit_text(g_engine, ibus_text_new_from_string(""), 0,
                                    false);
}

/**
 * @brief 识别过程发生异常时, sdk内部线程上报TaskFailed事件
 * @note 上报TaskFailed事件之后, SDK内部会关闭识别连接通道.
 *       此时调用sendAudio会返回-1, 请停止发送.
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为nullptr, 可以根据需求自定义参数
 * @return
 */

void OnRecognitionTaskFailed(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    if (cbParam) {
        auto *tmpParam = (ParamCallBack1 *)cbParam;
        LOG_INFO("taskFailed userId:%lu, %s", tmpParam->userId,
                 tmpParam->userInfo); // 仅表示自定义参数示例
    }

    LOG_INFO(
        "OnRecognitionTaskFailed: status code:%d, task id:%s, error message:%s",
        cbEvent
            ->getStatusCode(), // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
        cbEvent->getTaskId(), // 当前任务的task id，方便定位问题，建议输出
        cbEvent->getErrorMessage());

    LOG_INFO("OnRecognitionTaskFailed: All response:%s",
             cbEvent->getAllResponse()); // 获取服务端返回的全部信息
    waiting = false;
    audio_text = "";
    engine_commit_text(g_engine, ibus_text_new_from_string(audio_text.c_str()));
    ibus_lookup_table_clear(g_table);
    ibus_engine_update_preedit_text(g_engine, ibus_text_new_from_string(""), 0,
                                    false);
}

/**
 * @brief 识别结束或发生异常时，会关闭连接通道,
 *        sdk内部线程上报ChannelCloseed事件
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为nullptr, 可以根据需求自定义参数
 * @return
 */

void OnRecognitionChannelClosed(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    LOG_INFO("OnRecognitionChannelClosed: All response:%s",
             cbEvent->getAllResponse()); // 获取服务端返回的全部信息
    if (cbParam) {
        auto *tmpParam = (ParamCallBack1 *)cbParam;
        LOG_INFO("OnRecognitionChannelClosed CbParam:%lu, %s", tmpParam->userId,
                 tmpParam->userInfo); // 仅表示自定义参数示例

        //通知发送线程, 最终识别结果已经返回, 可以调用stop()
        pthread_mutex_lock(&(tmpParam->mtxWord));
        pthread_cond_signal(&(tmpParam->cvWord));
        pthread_mutex_unlock(&(tmpParam->mtxWord));
    }
}

int RecognitionRecordAndRequest(ParamStruct1 *tst) {

    // 0: 从自定义线程参数中获取token, 配置文件等参数.
    if (tst == nullptr) {
        LOG_ERROR("arg is not valid.");
        return -1;
    }

    //初始化自定义回调参数, 以下两变量仅作为示例表示参数传递,
    //在demo中不起任何作用
    //回调参数在堆中分配之后, SDK在销毁requesr对象时会一并销毁, 外界无需在释放
    auto cbParam = new (std::nothrow) ParamCallBack1(tst);
    cbParam->userId = pthread_self();
    strcpy(cbParam->userInfo, "User.");

    /*
     * 1: 创建一句话识别SpeechRecognizerRequest对象
     */
    AlibabaNls::SpeechRecognizerRequest *request =
        AlibabaNls::NlsClient::getInstance()->createRecognizerRequest();
    if (request == nullptr) {
        LOG_ERROR("createRecognizerRequest failed.");
        return -3;
    }

    // 设置start()成功回调函数
    request->setOnRecognitionStarted(OnRecognitionStarted, cbParam);
    // 设置异常识别回调函数
    request->setOnTaskFailed(OnRecognitionTaskFailed, cbParam);
    // 设置识别通道关闭回调函数
    request->setOnChannelClosed(OnRecognitionChannelClosed, cbParam);
    // 设置中间结果回调函数
    request->setOnRecognitionResultChanged(OnRecognitionResultChanged, cbParam);
    // 设置识别结束回调函数
    request->setOnRecognitionCompleted(OnRecognitionCompleted, cbParam);

    // 设置AppKey, 必填参数, 请参照官网申请
    if (strlen(tst->appkey) > 0) {
        request->setAppKey(tst->appkey);
        LOG_INFO("setAppKey:%s", tst->appkey);
    }
    // 设置音频数据编码格式, 可选参数, 目前支持pcm,opus,opu. 默认是pcm
    request->setFormat("pcm");
    // 设置音频数据采样率, 可选参数, 目前支持16000, 8000. 默认是16000
    request->setSampleRate(SAMPLE_RATE);
    // 设置是否返回中间识别结果, 可选参数. 默认false
    request->setIntermediateResult(true);
    // 设置是否在后处理中添加标点, 可选参数. 默认false
    request->setPunctuationPrediction(true);
    // 设置是否在后处理中执行ITN, 可选参数. 默认false
    request->setInverseTextNormalization(true);

    //是否启动语音检测, 可选, 默认是False
    // request->setEnableVoiceDetection(true);
    //允许的最大开始静音, 可选, 单位是毫秒,
    //超出后服务端将会发送RecognitionCompleted事件, 结束本次识别.
    //注意: 需要先设置enable_voice_detection为true
    // request->setMaxStartSilence(800);
    //允许的最大结束静音, 可选, 单位是毫秒,
    //超出后服务端将会发送RecognitionCompleted事件, 结束本次识别.
    //注意: 需要先设置enable_voice_detection为true
    // request->setMaxEndSilence(800);
    // request->setCustomizationId("TestId_123"); //定制模型id, 可选.
    // request->setVocabularyId("TestId_456"); //定制泛热词id, 可选.

    // 设置账号校验token, 必填参数
    if (strlen(tst->token) > 0) {
        request->setToken(tst->token);
        LOG_INFO("setToken:%s", tst->token);
    }
    if (strlen(tst->url) > 0) {
        LOG_INFO("setUrl:%s", tst->url);
        request->setUrl(tst->url);
    }

    LOG_INFO("begin sendAudio. ");

    /*
     * 2: start()为异步操作。成功返回started事件。失败返回TaskFailed事件。
     */
    LOG_INFO("start ->");
    struct timespec outtime {};
    struct timeval now {};
    int ret = request->start();
    if (ret < 0) {
        LOG_ERROR("start failed(%d)", ret);
        AlibabaNls::NlsClient::getInstance()->releaseRecognizerRequest(request);
        return -4;
    } else {
        //等待started事件返回, 在发送
        LOG_INFO("wait started callback.");
        gettimeofday(&now, nullptr);
        outtime.tv_sec = now.tv_sec + 10;
        outtime.tv_nsec = now.tv_usec * 1000;
        pthread_mutex_lock(&(cbParam->mtxWord));
        pthread_cond_timedwait(&(cbParam->cvWord), &(cbParam->mtxWord),
                               &outtime);
        pthread_mutex_unlock(&(cbParam->mtxWord));
    }

    static const pa_sample_spec ss = {
        .format = PA_SAMPLE_S16LE, .rate = 16000, .channels = 1};
    pa_simple *s = nullptr;
    int error;

    /* Create the recording stream */
    if (!(s = pa_simple_new(nullptr, "audio_ime", PA_STREAM_RECORD, nullptr,
                            "record", &ss, nullptr, nullptr, &error))) {
        LOG_INFO("pa_simple_new() failed: %s\n", pa_strerror(error));
        return -5;
    }

    struct timeval x {};
    gettimeofday(&x, nullptr);

    while (recording) {
        struct timeval y {};
        gettimeofday(&y, nullptr);
        auto newRecordingTime = y.tv_sec - x.tv_sec;
        if (newRecordingTime - recordingTime >= 1) {
            IBusUpdateIndicator();
        }
        recordingTime = newRecordingTime;
        if (recordingTime > 59) {
            break;
        }
        uint8_t buf[BUFSIZE];

        /* Record some data ... */
        if (pa_simple_read(s, buf, sizeof(buf), &error) < 0) {
            LOG_INFO("pa_simple_read() failed: %s\n", pa_strerror(error));
            break;
        }
        uint8_t data[frame_size];
        memset(data, 0, frame_size);

        /*
         * 3: 发送音频数据: sendAudio为异步操作, 返回负值表示发送失败,
         * 需要停止发送; 返回0 为成功. notice : 返回值非成功发送字节数.
         *    若希望用省流量的opus格式上传音频数据, 则第三参数传入ENCODER_OPU
         *    ENCODER_OPU/ENCODER_OPUS模式时,nlen必须为640
         */
        ret = request->sendAudio(buf, sizeof(buf), (ENCODER_TYPE)encoder_type);
        if (ret < 0) {
            // 发送失败, 退出循环数据发送
            LOG_ERROR("send data fail(%d)", ret);
            break;
        }
    } // while
    recordingTime = 0;

    /*
     * 6: 通知云端数据发送结束.
     * stop()为异步操作.失败返回TaskFailed事件
     */
    // stop()后会收到所有回调，若想立即停止则调用cancel()
    ret = request->stop();
    LOG_INFO("stop done");

    /*
     * 6: 通知SDK释放request.
     */
    if (ret == 0) {
        LOG_INFO("wait closed callback.");
        gettimeofday(&now, nullptr);
        outtime.tv_sec = now.tv_sec + 10;
        outtime.tv_nsec = now.tv_usec * 1000;
        // 等待closed事件后再进行释放, 否则会出现崩溃
        pthread_mutex_lock(&(cbParam->mtxWord));
        pthread_cond_timedwait(&(cbParam->cvWord), &(cbParam->mtxWord),
                               &outtime);
        pthread_mutex_unlock(&(cbParam->mtxWord));
    } else {
        LOG_INFO("stop ret is %d", ret);
    }
    AlibabaNls::NlsClient::getInstance()->releaseRecognizerRequest(request);
    return 0;
}

int RecognitionPrepareAndStartRecording() {
    int ret = AlibabaNls::NlsClient::getInstance()->setLogConfig(
        "log-recognizer", AlibabaNls::LogLevel::LogDebug, 400,
        50); //"log-recognizer"
    if (-1 == ret) {
        LOG_ERROR("set log failed.");
        return -1;
    }

    // 启动工作线程, 在创建请求和启动前必须调用此函数
    // 入参为负时, 启动当前系统中可用的核数
    AlibabaNls::NlsClient::getInstance()->startWorkThread();

    ParamStruct1 tst{};
    memset(tst.appkey, 0, DEFAULT_STRING_LEN);
    std::string appkey = "Y0ueIZ5N4OkyfpUW";
    std::string token = "1a9838b31cd5425b80f3f7677697c252";
    std::time_t curTime = std::time(0);
    if (g_expireTime == 0 || g_expireTime < curTime < 10) {
        if (-1 ==
            NetGenerateToken(g_akId, g_akSecret, &g_token, &g_expireTime)) {
            LOG_ERROR("failed to gen token");
            return -1;
        }
        memset(tst.token, 0, DEFAULT_STRING_LEN);
        memcpy(tst.token, g_token.c_str(), g_token.length());
    }
    memcpy(tst.appkey, appkey.data(), appkey.size());
    tst.appkey[appkey.size()] = '\0';

    memset(tst.url, 0, DEFAULT_STRING_LEN);

    RecognitionRecordAndRequest(&tst);
    AlibabaNls::NlsClient::getInstance()->releaseInstance();
    return 0;
}
}; // namespace speech

namespace pinyin {
    guint Search(std::string input) {
        auto numCandidates =
            ime_pinyin::im_search(input.c_str(), input.size());
        return numCandidates;
    }
    std::wstring GetCandidate(int index) {
        ime_pinyin::char16 buffer[40];
        auto ret = ime_pinyin::im_get_candidate(index, buffer, 40);
        if(ret == nullptr) {
            return {};
        }

        return std::wstring((wchar_t*)buffer, 40);
    }
}
static void sigterm_cb(int sig) {
    LOG_ERROR("sig term %d", sig);
    exit(-1);
}

static void IBusOnDisconnectedCb(IBusBus *bus, gpointer user_data) {
    ibus_quit();
}

void engine_reset(IBusEngine *engine, IBusLookupTable *table) {
    ibus_lookup_table_clear(table);
    ibus_engine_hide_preedit_text(engine);
    ibus_engine_hide_auxiliary_text(engine);
    ibus_engine_hide_lookup_table(engine);
}

void engine_commit_text(IBusEngine *engine, IBusText *text) {
    ibus_engine_commit_text(engine, text);
    engine_reset(engine, g_table);
}

std::string IBusMakeIndicatorMsg() {
    std::string msg = "press C-` to toggle record[";
    if (recording) {
        msg += "recording " + std::to_string(recordingTime);
    }
    if (waiting) {
        msg += "waiting";
    }
    msg += "]";
    return msg;
}

void IBusUpdateIndicator() {
    ibus_engine_update_auxiliary_text(
        g_engine, ibus_text_new_from_string(IBusMakeIndicatorMsg().c_str()),
        TRUE);
}

gboolean IBusEngineProcessKeyEventCb(IBusEngine *engine, guint keyval,
                                     guint keycode, guint state) {
    LOG_INFO("engine_process_key_event keycode: %d, keyval:%x", keycode,
             keyval);

    if (state & IBUS_RELEASE_MASK) {
        return FALSE;
    }

    if ((state & IBUS_CONTROL_MASK) && keycode == 41) {
        if (waiting) {
            return TRUE;
        }
        if (!recording) {
            recording = true;
            std::thread t1([]() { speech::RecognitionPrepareAndStartRecording(); });
            t1.detach();
        } else {
            recording = false;
            waiting = true;
            // engine_commit_text(engine,
            // ibus_text_new_from_string(audio_text.c_str()));
        }
        ibus_engine_hide_lookup_table(engine);
        ibus_engine_show_preedit_text(engine);
        ibus_engine_show_auxiliary_text(engine);
        IBusUpdateIndicator();
        return true;
    }
    if (state & IBUS_CONTROL_MASK) {
        return false;
    }

    // other key inputs
    if (recording || waiting) {
        // don't respond to other key inputs when recording or waiting
        return true;
    }

    if (state & IBUS_LOCK_MASK) {
        if (keycode == 58) {
            LOG_INFO("caps lock pressed");
            // caps lock
            wbpy_input = "";
            ibus_lookup_table_clear(g_table);
            ibus_engine_update_auxiliary_text(
                g_engine, ibus_text_new_from_string(""), true);
            ibus_engine_hide_lookup_table(engine);
            ibus_engine_hide_preedit_text(engine);
            ibus_engine_hide_auxiliary_text(engine);
            return true;
        }
        if (keyval == IBUS_KEY_equal || keyval == IBUS_KEY_Right) {
            LOG_DEBUG("equal pressed");
            ibus_lookup_table_page_down(g_table);
            guint cursor = ibus_lookup_table_get_cursor_in_page(g_table);
            LOG_INFO("cursor pos %d", cursor);
            cursor = ibus_lookup_table_get_cursor_pos(g_table);
            LOG_INFO("cursor pos(global) %d", cursor);
            ibus_engine_update_lookup_table_fast(g_engine, g_table, true);
            //            ibus_engine_forward_key_event(g_engine, keyval,
            //            keycode, state);
            return true;
        }
        if (keyval == IBUS_KEY_minus || keyval == IBUS_KEY_Left) {
            LOG_DEBUG("minus pressed");
            ibus_lookup_table_page_up(g_table);
            guint cursor = ibus_lookup_table_get_cursor_in_page(g_table);
            LOG_INFO("cursor pos %d", cursor);
            cursor = ibus_lookup_table_get_cursor_pos(g_table);
            LOG_INFO("cursor pos(global) %d", cursor);
            ibus_lookup_table_set_cursor_pos(g_table, 3);
            ibus_engine_update_lookup_table_fast(g_engine, g_table, true);
            return true;
        }
        if (keyval == IBUS_KEY_Down) {
            LOG_DEBUG("down pressed");
            // ibus_lookup_table_cursor_down(g_table);
            bool ret = ibus_lookup_table_cursor_down(g_table);
            if (!ret) {
                LOG_ERROR("failed to put cursor down");
            }
            guint cursor = ibus_lookup_table_get_cursor_in_page(g_table);
            LOG_INFO("cursor pos %d", cursor);
            cursor = ibus_lookup_table_get_cursor_pos(g_table);
            LOG_INFO("cursor pos(global) %d", cursor);
            ibus_engine_update_lookup_table_fast(g_engine, g_table, true);
            return true;
        }
        if (keyval == IBUS_KEY_Up) {
            LOG_DEBUG("up pressed");
            bool ret = ibus_lookup_table_cursor_up(g_table);
            if (!ret) {
                LOG_ERROR("failed to put cursor up");
            }
            guint cursor = ibus_lookup_table_get_cursor_in_page(g_table);
            LOG_INFO("cursor pos %d", cursor);
            cursor = ibus_lookup_table_get_cursor_pos(g_table);
            LOG_INFO("cursor pos(global) %d", cursor);
            ibus_engine_update_lookup_table_fast(g_engine, g_table, true);
            return true;
        }
        if (keyval == IBUS_KEY_space || keyval == IBUS_KEY_Return ||
            std::isdigit((char)(keyval)) || keycode == 1) {
            if (wbpy_input.empty()) {
                return false;
            }
            LOG_DEBUG("space pressed");
            guint cursor = ibus_lookup_table_get_cursor_pos(g_table);
            if (std::isdigit((char)keyval)) {
                guint cursor_page =
                    ibus_lookup_table_get_cursor_in_page(g_table);
                int index = (int)(keyval - IBUS_KEY_0);
                cursor = cursor + (index - cursor_page) - 1;
                LOG_DEBUG("cursor_page:%d, index:%d, cursor:%d", cursor_page,
                          index, cursor);
                ibus_lookup_table_set_cursor_pos(g_table, cursor);
            }
            auto text = ibus_lookup_table_get_candidate(g_table, cursor);
            if (keycode != 1) { // which means escape
                ibus_engine_commit_text(engine, text);
            }
            ibus_engine_update_auxiliary_text(
                g_engine, ibus_text_new_from_string(""), true);
            ibus_lookup_table_clear(g_table);
            ibus_engine_update_lookup_table_fast(g_engine, g_table, true);
            ibus_engine_hide_lookup_table(g_engine);
            ibus_engine_hide_auxiliary_text(g_engine);
            ibus_engine_hide_preedit_text(g_engine);
            wbpy_input = "";
            return true;
        }

        LOG_DEBUG("keyval %x, wbpy_input.size:%lu", keyval, wbpy_input.size());
        if (keyval == IBUS_KEY_BackSpace && !wbpy_input.empty()) {
            wbpy_input = wbpy_input.substr(0, wbpy_input.size() - 1);
        } else {
            if (!std::isalpha((char)keyval)) {
                // only process letters and numbers
                return false;
            }
            wbpy_input += (char)std::tolower((int)keyval);
        }
        // chinese mode
        ibus_engine_update_auxiliary_text(
            g_engine, ibus_text_new_from_string(wbpy_input.c_str()), true);

        // get pinyin candidates
        auto numCandidates = pinyin::Search(wbpy_input);
        LOG_INFO("num candidates %lu for %s\n", numCandidates,
                 wbpy_input.c_str());

        // get wubi candidates
        LOG_DEBUG("");
        wubi::TrieNode *x = wubi::TrieSearch(wubi::g_root, wbpy_input);
        std::map<uint64_t, std::string> m;
        wubi::TrieTraversal(m, x);

        ibus_lookup_table_clear(g_table);
        if (x != nullptr && x->isEndOfWord) {
            auto it = x->values.rbegin();
            std::string candidate = it->second;
            // best exact match first
            auto text = ibus_text_new_from_string(candidate.c_str());
            ibus_lookup_table_append_candidate(g_table, text);
            it++;
            // put the rest in the queue
            while (it != x->values.rend()) {
                m.insert(*it);
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
                ibus_lookup_table_append_candidate(g_table, text);
                it++;
            }
            if (j < numCandidates) {
                std::wstring buffer = pinyin::GetCandidate(j);
                glong items_read;
                glong items_written;
                GError *error;
                gunichar *utf32_str =
                    g_utf16_to_ucs4(
                    reinterpret_cast<const gunichar2 *>(buffer.data()), buffer.size(), &items_read,
                                    &items_written, &error);
                ibus_lookup_table_append_candidate(
                    g_table, ibus_text_new_from_ucs4(utf32_str));
                j++;
            }
        }

        ibus_engine_update_lookup_table_fast(g_engine, g_table, TRUE);
        ibus_engine_show_lookup_table(engine);

        return true;

    } else {
        // english mode
        ibus_engine_hide_lookup_table(engine);
        ibus_engine_hide_preedit_text(engine);
        ibus_engine_hide_auxiliary_text(engine);
        return false;
    }
}

void IBusEngineEnableCb([[maybe_unused]] IBusEngine *engine) {
    LOG_INFO("[IM:iBus]: IM enabled\n");
    // Setup Lookup table
    g_table = ibus_lookup_table_new(10, 0, TRUE, TRUE);
    LOG_INFO("table %p", g_table);
    g_object_ref_sink(g_table);

    ibus_lookup_table_set_round(g_table, true);
    ibus_lookup_table_set_page_size(g_table, 5);
    ibus_lookup_table_set_orientation(g_table, IBUS_ORIENTATION_VERTICAL);
    // ibus_engine_show_lookup_table(engine);
    //     ibus_engine_show_auxiliary_text(engine);
    bool ret = ime_pinyin::im_open_decoder(
        "/usr/share/ibus-table/data/dict_pinyin.dat",
        "/home/zhangfuwen/pinyin.dat");
    if (!ret) {
        LOG_ERROR("failed to open decoder\n");
    }
}

void IBusEngineDisableCb([[maybe_unused]] IBusEngine *engine) {
    LOG_INFO("[IM:iBus]: IM disabled\n");
    ime_pinyin::im_close_decoder();
}

void IBusEngineFocusOutCb(IBusEngine *engine) {
    LOG_INFO("[IM:iBus]: IM Focus out\n");
}

void IBusEngineFocusInCb([[maybe_unused]] IBusEngine *engine) {
    LOG_INFO("[IM:iBus]: IM Focus in\n");
    auto prop_list = ibus_prop_list_new();
    LOG_DEBUG("");
    auto prop1 = ibus_property_new(
        "mixed_input", IBusPropType::PROP_TYPE_TOGGLE,
        ibus_text_new_from_string("五笔拼音混输"), "audio_ime",
        ibus_text_new_from_string("五笔拼音混输"), true, true,
        IBusPropState::PROP_STATE_CHECKED, nullptr);
    auto prop2 = ibus_property_new(
        "preference", IBusPropType::PROP_TYPE_NORMAL,
        ibus_text_new_from_string("preference"), "audio_ime",
        ibus_text_new_from_string("preference_tool_tip"), true, true,
        IBusPropState::PROP_STATE_CHECKED, nullptr);
    g_object_ref_sink(prop_list);
    LOG_DEBUG("");
    ibus_prop_list_append(prop_list, prop1);
    ibus_prop_list_append(prop_list, prop2);
    LOG_DEBUG("");
    ibus_engine_register_properties(g_engine, prop_list);
    LOG_DEBUG("");
}

void IBusEnginePropertyActivateCb(IBusEngine *engine, gchar *name, guint state,
                                  gpointer user_data) {
    LOG_INFO("property changed, name:%s, state:%d", name, state);
    if (std::string(name) == "mixed_input") {
        mixed_input_state = state;
    } else if (std::string(name) == "preference") {
        auto engine_desc = ibus_bus_get_global_engine(g_bus);
        gchar setup[1024];
        const gchar *setup_path = ibus_engine_desc_get_setup(engine_desc);
        g_spawn_command_line_async("audio_ime_setup", nullptr);
        LOG_DEBUG("setup path--:%s", setup_path);
        g_object_unref(G_OBJECT(engine_desc));
    }
}

void IBusEngineCandidateClickedCb(IBusEngine *engine, guint index, guint button,
                                  guint state) {
    LOG_INFO("[IM:iBus]: candidate clicked\n");
    IBusText *text = ibus_lookup_table_get_candidate(g_table, index);
    ibus_engine_commit_text(engine, text);
    ibus_lookup_table_clear(g_table);
    ibus_engine_update_auxiliary_text(g_engine, ibus_text_new_from_string(""),
                                      true);
    wbpy_input = "";
}

void IBusConfigValueChangedCb(IBusConfig *config, gchar *section, gchar *name,
                              GVariant *value, gpointer user_data) {
    LOG_INFO("name:%s, section:%s", name, section);
}

IBusEngine *IBusEngineCreatedCb(IBusFactory *factory, gchar *engine_name,
                                gpointer user_data) {
    id += 1;
    gchar *path = g_strdup_printf("/org/freedesktop/IBus/Engine/%i", id);
    g_engine =
        ibus_engine_new(engine_name, path, ibus_bus_get_connection(g_bus));

    // Setup Lookup table
    LOG_INFO("[IM:iBus]: Creating IM Engine\n");
    LOG_INFO("[IM:iBus]: Creating IM Engine with name:%s and id:%d\n",
             engine_name, id);

    g_signal_connect(g_engine, "process-key-event",
                     G_CALLBACK(IBusEngineProcessKeyEventCb), nullptr);
    g_signal_connect(g_engine, "enable", G_CALLBACK(IBusEngineEnableCb),
                     nullptr);
    g_signal_connect(g_engine, "disable", G_CALLBACK(IBusEngineDisableCb),
                     nullptr);
    g_signal_connect(g_engine, "focus-out", G_CALLBACK(IBusEngineFocusOutCb),
                     nullptr);
    g_signal_connect(g_engine, "focus-in", G_CALLBACK(IBusEngineFocusInCb),
                     nullptr);
    g_signal_connect(g_engine, "candidate-clicked",
                     G_CALLBACK(IBusEngineCandidateClickedCb), nullptr);
    g_signal_connect(g_engine, "property-activate",
                     G_CALLBACK(IBusEnginePropertyActivateCb), nullptr);

    wubi::TrieImportWubiTable();
    LOG_DEBUG("");

    return g_engine;
}

int main([[maybe_unused]] gint argc, gchar **argv) {
    signal(SIGTERM, sigterm_cb);
    signal(SIGINT, sigterm_cb);

    ibus_init();
    g_bus = ibus_bus_new();
    g_object_ref_sink(g_bus);

    LOG_DEBUG("bus %p", g_bus);

    if (!ibus_bus_is_connected(g_bus)) {
        LOG_WARN("not connected to ibus");
        exit(0);
    }

    LOG_DEBUG("ibus bus connected");

    g_signal_connect(g_bus, "disconnected", G_CALLBACK(IBusOnDisconnectedCb),
                     nullptr);

    IBusFactory *factory = ibus_factory_new(ibus_bus_get_connection(g_bus));
    LOG_DEBUG("factory %p", factory);
    g_object_ref_sink(factory);

    auto conn = ibus_bus_get_connection(g_bus);
    LOG_DEBUG("");
    g_config = ibus_config_new(conn, nullptr, nullptr);
    LOG_DEBUG("");
    ibus_config_watch(g_config, CONF_SECTION, CONF_NAME);
    g_signal_connect(g_config, "value-changed",
                     G_CALLBACK(IBusConfigValueChangedCb), nullptr);

    g_signal_connect(factory, "create-engine", G_CALLBACK(IBusEngineCreatedCb),
                     nullptr);

    ibus_factory_add_engine(factory, "AudIme", IBUS_TYPE_ENGINE);

    IBusComponent *component;

    if (g_bus) {
        if (!ibus_bus_request_name(g_bus, "org.freedesktop.IBus.AudIme", 0)) {
            LOG_ERROR("error requesting bus name");
            exit(1);
        } else {
            LOG_INFO("ibus_bus_request_name success");
        }
    } else {
        component = ibus_component_new(
            "org.freedesktop.IBus.AudIme", "LOT input method", "1.1", "MIT",
            "zhangfuwen", "xxx", "/usr/bin/audio_ime --ibus", "audio_ime");
        LOG_DEBUG("component %p", component);
        ibus_component_add_engine(
            component,
            ibus_engine_desc_new("AudIme", "audo input method",
                                 "audo input method", "zh_CN", "MIT",
                                 "zhangfuwen", "audio_ime", "default"));
        ibus_bus_register_component(g_bus, component);

        ibus_bus_set_global_engine_async(g_bus, "AudIme", -1, nullptr, nullptr,
                                         nullptr);
    }

    LOG_INFO("entering ibus main");
    ibus_main();
    LOG_INFO("exiting ibus main");

    g_object_unref(factory);
    g_object_unref(g_bus);
}
