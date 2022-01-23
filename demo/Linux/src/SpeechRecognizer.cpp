//
// Created by zhangfuwen on 2022/1/22.
//

#include "SpeechRecognizer.h"
#include "InputMethod.h"
#include "PinyinIME.h"
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
#include <errno.h>
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
#include <pulse/gccmacro.h>
#include <pulse/simple.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

std::string audio_text;

using SpeechCallbackType = function<void(AlibabaNls::NlsEvent *ev, void *cbParam)>;
static SpeechCallbackType fnOnRecognitionStarted;
static SpeechCallbackType fnOnRecognitionTaskFailed;
static SpeechCallbackType fnOnRecognitionChannelClosed;
static SpeechCallbackType fnOnRecognitionResultChanged;
static SpeechCallbackType fnOnRecognitionCompleted;

/**
 * 根据AccessKey ID和AccessKey Secret重新生成一个token，并获取其有效期时间戳
 */
int SpeechRecognizer::NetGenerateToken(const string &akId, const string &akSecret, string *token, long *expireTime) {
    if (akId.empty() || akSecret.empty()) {
        LOG_ERROR("akId(%d) or akSecret(%d) is empty", akId.size(), akSecret.size());
        return -1;
    }
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
void SpeechRecognizer::OnRecognitionStarted(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    if (cbParam) {
        auto *tmpParam = (ParamCallBack *)cbParam;
        LOG_INFO("OnRecognitionStarted userId:%lu, %s", tmpParam->userId,
                 tmpParam->userInfo); // 仅表示自定义参数示例
        //通知发送线程start()成功, 可以继续发送数据
        pthread_mutex_lock(&(tmpParam->mtxWord));
        pthread_cond_signal(&(tmpParam->cvWord));
        pthread_mutex_unlock(&(tmpParam->mtxWord));
    }

    LOG_INFO("OnRecognitionStarted: status code:%d, task id:%s",
             cbEvent->getStatusCode(), // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
             cbEvent->getTaskId()); // 当前任务的task id，方便定位问题，建议输出
}

/**
 * @brief 设置允许返回中间结果参数, sdk在接收到云端返回到中间结果时,
 *        sdk内部线程上报ResultChanged事件
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为nullptr, 可以根据需求自定义参数
 * @return
 */

void SpeechRecognizer::OnRecognitionResultChanged(AlibabaNls::NlsEvent *cbEvent, [[maybe_unused]] void *cbParam) {
    LOG_INFO("result changed");
    m_speechListerner.OnPartialResult(cbEvent->getResult());
    //    ibus_lookup_table_append_candidate(m_table,
    //    ibus_text_new_from_string(cbEvent->getResult()));
    //    ibus_engine_update_lookup_table_fast(m_engine, m_table, TRUE); // this
    //    line determines if lookup table is displayed
    //    ibus_lookup_table_set_cursor_pos(m_table, 0);
}

/**
 * @brief sdk在接收到云端返回识别结束消息时, sdk内部线程上报Completed事件
 * @note 上报Completed事件之后, SDK内部会关闭识别连接通道.
 *       此时调用sendAudio会返回-1, 请停止发送.
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为nullptr, 可以根据需求自定义参数
 * @return
 */
void SpeechRecognizer::OnRecognitionCompleted(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {

    if (cbParam) {
        auto *tmpParam = (ParamCallBack *)cbParam;
        if (!tmpParam->tParam)
            return;
        LOG_INFO("OnRecognitionCompleted: userId %lu, %s", tmpParam->userId,
                 tmpParam->userInfo); // 仅表示自定义参数示例
    }

    LOG_INFO("OnRecognitionCompleted: status code:%d, task id:%s, result:%s",
             cbEvent->getStatusCode(), // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
             cbEvent->getTaskId(),  // 当前任务的task id，方便定位问题，建议输出
             cbEvent->getResult()); // 获取中间识别结果

    audio_text = cbEvent->getResult();

    LOG_INFO("OnRecognitionCompleted: All response:%s",
             cbEvent->getAllResponse()); // 获取服务端返回的全部信息

    m_waiting = false;
    m_recording = false;
    m_speechListerner.OnCompleted(audio_text);
}

/**
 * @brief 识别过程发生异常时, sdk内部线程上报TaskFailed事件
 * @note 上报TaskFailed事件之后, SDK内部会关闭识别连接通道.
 *       此时调用sendAudio会返回-1, 请停止发送.
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为nullptr, 可以根据需求自定义参数
 * @return
 */

void SpeechRecognizer::OnRecognitionTaskFailed(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    if (cbParam) {
        auto *tmpParam = (ParamCallBack *)cbParam;
        LOG_INFO("taskFailed userId:%lu, %s", tmpParam->userId,
                 tmpParam->userInfo); // 仅表示自定义参数示例
    }

    LOG_INFO("OnRecognitionTaskFailed: status code:%d, task id:%s, error message:%s",
             cbEvent->getStatusCode(), // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
             cbEvent->getTaskId(), // 当前任务的task id，方便定位问题，建议输出
             cbEvent->getErrorMessage());

    LOG_INFO("OnRecognitionTaskFailed: All response:%s",
             cbEvent->getAllResponse()); // 获取服务端返回的全部信息
    m_waiting = false;
    m_speechListerner.OnFailed();
}

/**
 * @brief 识别结束或发生异常时，会关闭连接通道,
 *        sdk内部线程上报ChannelCloseed事件
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为nullptr, 可以根据需求自定义参数
 * @return
 */

void SpeechRecognizer::OnRecognitionChannelClosed(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    LOG_INFO("OnRecognitionChannelClosed: All response:%s",
             cbEvent->getAllResponse()); // 获取服务端返回的全部信息
    if (cbParam) {
        auto *tmpParam = (ParamCallBack *)cbParam;
        LOG_INFO("OnRecognitionChannelClosed CbParam:%lu, %s", tmpParam->userId,
                 tmpParam->userInfo); // 仅表示自定义参数示例

        //通知发送线程, 最终识别结果已经返回, 可以调用stop()
        pthread_mutex_lock(&(tmpParam->mtxWord));
        pthread_cond_signal(&(tmpParam->cvWord));
        pthread_mutex_unlock(&(tmpParam->mtxWord));
    }
}

int SpeechRecognizer::RecognitionRecordAndRequest(ParamStruct *tst) {

    // 0: 从自定义线程参数中获取token, 配置文件等参数.
    if (tst == nullptr) {
        LOG_ERROR("arg is not valid.");
        return -1;
    }

    //初始化自定义回调参数, 以下两变量仅作为示例表示参数传递,
    //在demo中不起任何作用
    //回调参数在堆中分配之后, SDK在销毁requesr对象时会一并销毁, 外界无需在释放
    auto cbParam = new (nothrow) ParamCallBack(tst);
    cbParam->userId = pthread_self();
    strcpy(cbParam->userInfo, "User.");

    /*
     * 1: 创建一句话识别SpeechRecognizerRequest对象
     */
    AlibabaNls::SpeechRecognizerRequest *request = AlibabaNls::NlsClient::getInstance()->createRecognizerRequest();
    if (request == nullptr) {
        LOG_ERROR("createRecognizerRequest failed.");
        return -3;
    }
    //        std::function<void(AlibabaNls::NlsEvent *ev, void *cbParam)> fn
    fnOnRecognitionStarted = bind(&SpeechRecognizer::OnRecognitionStarted, this, placeholders::_1, placeholders::_2);
    fnOnRecognitionTaskFailed =
        bind(&SpeechRecognizer::OnRecognitionTaskFailed, this, placeholders::_1, placeholders::_2);
    fnOnRecognitionChannelClosed =
        bind(&SpeechRecognizer::OnRecognitionChannelClosed, this, placeholders::_1, placeholders::_2);
    fnOnRecognitionResultChanged =
        bind(&SpeechRecognizer::OnRecognitionResultChanged, this, placeholders::_1, placeholders::_2);
    fnOnRecognitionCompleted =
        bind(&SpeechRecognizer::OnRecognitionCompleted, this, placeholders::_1, placeholders::_2);

    // 设置start()成功回调函数
    request->setOnRecognitionStarted([](AlibabaNls::NlsEvent *ev, void *par) { fnOnRecognitionStarted(ev, par); },
                                     cbParam);
    // 设置异常识别回调函数
    request->setOnTaskFailed([](AlibabaNls::NlsEvent *ev, void *par) { fnOnRecognitionTaskFailed(ev, par); }, cbParam);
    // 设置识别通道关闭回调函数
    request->setOnChannelClosed([](AlibabaNls::NlsEvent *ev, void *par) { fnOnRecognitionChannelClosed(ev, par); },
                                cbParam);
    // 设置中间结果回调函数
    request->setOnRecognitionResultChanged(
        [](AlibabaNls::NlsEvent *ev, void *par) { fnOnRecognitionResultChanged(ev, par); }, cbParam);
    // 设置识别结束回调函数
    request->setOnRecognitionCompleted([](AlibabaNls::NlsEvent *ev, void *par) { fnOnRecognitionCompleted(ev, par); },
                                       cbParam);

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
        pthread_cond_timedwait(&(cbParam->cvWord), &(cbParam->mtxWord), &outtime);
        pthread_mutex_unlock(&(cbParam->mtxWord));
    }

    static const pa_sample_spec ss = {.format = PA_SAMPLE_S16LE, .rate = 16000, .channels = 1};
    int error;

    LOG_DEBUG("pa_simple_new");
    pa_simple *s =
        pa_simple_new(nullptr, "audio_ime", PA_STREAM_RECORD, nullptr, "record", &ss, nullptr, nullptr, &error);
    /* Create the m_recording stream */
    if (!s) {
        LOG_INFO("pa_simple_new() failed: %s", pa_strerror(error));
        return -5;
    }

    struct timeval x {};
    gettimeofday(&x, nullptr);

    LOG_DEBUG("m_recording %d", m_recording);
    while (m_recording) {
        struct timeval y {};
        gettimeofday(&y, nullptr);
        auto newRecordingTime = y.tv_sec - x.tv_sec;
        if (newRecordingTime - recordingTime >= 1) {
            m_speechListerner.IBusUpdateIndicator(recordingTime);
        }
        recordingTime = newRecordingTime;
        if (recordingTime > 59) {
            break;
        }
        uint8_t buf[BUFSIZE];
        /* Record some data ... */
        LOG_DEBUG("reading %d", m_recording);
        if (pa_simple_read(s, buf, sizeof(buf), &error) < 0) {
            LOG_INFO("pa_simple_read() failed: %s", pa_strerror(error));
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
        LOG_DEBUG("send audio %d", m_recording);
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
        pthread_cond_timedwait(&(cbParam->cvWord), &(cbParam->mtxWord), &outtime);
        pthread_mutex_unlock(&(cbParam->mtxWord));
        LOG_INFO("wait closed callback done.");
    } else {
        LOG_INFO("stop ret is %d", ret);
    }
    AlibabaNls::NlsClient::getInstance()->releaseRecognizerRequest(request);
    return 0;
}

int SpeechRecognizer::RecognitionPrepareAndStartRecording() {
    int ret = AlibabaNls::NlsClient::getInstance()->setLogConfig("log-recognizer", AlibabaNls::LogDebug, 400,
                                                                 50); //"log-recognizer"
    if (-1 == ret) {
        LOG_ERROR("set log failed.");
        return -1;
    }

    // 启动工作线程, 在创建请求和启动前必须调用此函数
    // 入参为负时, 启动当前系统中可用的核数
    AlibabaNls::NlsClient::getInstance()->startWorkThread();

    ParamStruct tst{};
    memset(tst.appkey, 0, DEFAULT_STRING_LEN);
    string appkey = "Y0ueIZ5N4OkyfpUW";
    string token = "1a9838b31cd5425b80f3f7677697c252";
    time_t curTime = time(nullptr);
    if (g_expireTime == 0 || g_expireTime - curTime < 10) {
        LOG_DEBUG("generating new token %lu %lu", g_expireTime, curTime);
        if (-1 == NetGenerateToken(g_akId, g_akSecret, &g_token, &g_expireTime)) {
            LOG_ERROR("failed to gen token");
            return -1;
        }
    }
    memset(tst.token, 0, DEFAULT_STRING_LEN);
    memcpy(tst.token, g_token.c_str(), g_token.length());
    memcpy(tst.appkey, appkey.data(), appkey.size());
    tst.appkey[appkey.size()] = '\0';

    memset(tst.url, 0, DEFAULT_STRING_LEN);

    RecognitionRecordAndRequest(&tst);
    AlibabaNls::NlsClient::getInstance()->releaseInstance();
    return 0;
}