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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ctime>
#include <map>
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <sys/time.h>
#include <signal.h>
#include "nlsClient.h"
#include "nlsEvent.h"
#include "nlsToken.h"
#include "speechRecognizerRequest.h"
#include "profile_scan.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/gccmacro.h>

#define BUFSIZE 1024

#define SELF_TESTING_TRIGGER
#define FRAME_100MS 3200
#define SAMPLE_RATE 16000
#define DEFAULT_STRING_LEN 128


#define DEBUG(fmt, ...) printf("%s:%d %s" fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define DBG DEBUG

#include <glib-2.0/glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <ibus.h>
#include <stdio.h>
#include <thread>
#include <fcntl.h>

FILE* myfile;

#define LOG_INFO(fmt, ...)                                                           \
    do                                                                               \
    {                                                                                \
        printf("%s:%d %s > " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
        fprintf(myfile, "%s:%d %s > " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
        fflush(myfile); \
        g_info("%s:%d %s > " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__);      \
    } while (0)

#define LOG_DEBUG LOG_INFO
#define LOG_WARN LOG_INFO
#define LOG_ERROR LOG_INFO
void engine_commit_text(IBusEngine * engine, IBusText * text);

static gint id = 0;
static IBusEngine *g_engine = nullptr;
static IBusLookupTable *g_table = nullptr;
IBusBus *bus;
int startRecording();
volatile bool recording = false;
volatile int recordingTime = 0; // can only record 60 seconds;
volatile bool waiting = false; //waiting for converted text from internet
void updateIndicator();

std::string audio_text = "";
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
    ParamCallBack1(ParamStruct1 *param) {
        tParam = param;
        pthread_mutex_init(&mtxWord, NULL);
        pthread_cond_init(&cvWord, NULL);
    };

    ~ParamCallBack1() {
        tParam = NULL;
        pthread_mutex_destroy(&mtxWord);
        pthread_cond_destroy(&cvWord);
    };

    unsigned long userId;  // 这里用线程号
    char userInfo[8];

    pthread_mutex_t mtxWord;
    pthread_cond_t cvWord;


    ParamStruct1 *tParam;
};


static int frame_size = FRAME_100MS;
static int encoder_type = ENCODER_NONE;

/**
 * 根据AccessKey ID和AccessKey Secret重新生成一个token，并获取其有效期时间戳
 */
int generateToken(std::string akId, std::string akSecret,
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
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */
void OnRecognitionStarted1(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    if (cbParam) {
        ParamCallBack1 *tmpParam = (ParamCallBack1 *) cbParam;
        LOG_INFO("OnRecognitionStarted userId:%lu, %s", tmpParam->userId, tmpParam->userInfo); // 仅表示自定义参数示例
        //通知发送线程start()成功, 可以继续发送数据
        pthread_mutex_lock(&(tmpParam->mtxWord));
        pthread_cond_signal(&(tmpParam->cvWord));
        pthread_mutex_unlock(&(tmpParam->mtxWord));
    }

    LOG_INFO("OnRecognitionStarted: status code:%d, task id:%s", 
            cbEvent->getStatusCode(),  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
            cbEvent->getTaskId());   // 当前任务的task id，方便定位问题，建议输出
}

/**
 * @brief 设置允许返回中间结果参数, sdk在接收到云端返回到中间结果时,
 *        sdk内部线程上报ResultChanged事件
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */

void OnRecognitionResultChanged1(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    LOG_INFO("result changed");
    ibus_engine_update_preedit_text(g_engine, ibus_text_new_from_string(cbEvent->getResult()), 0, TRUE);
//    ibus_lookup_table_append_candidate(g_table, ibus_text_new_from_string(cbEvent->getResult()));
//    ibus_engine_update_lookup_table_fast(g_engine, g_table, TRUE); // this line determines if lookup table is displayed
//    ibus_lookup_table_set_cursor_pos(g_table, 0);
}

/**
 * @brief sdk在接收到云端返回识别结束消息时, sdk内部线程上报Completed事件
 * @note 上报Completed事件之后, SDK内部会关闭识别连接通道. 
 *       此时调用sendAudio会返回-1, 请停止发送.
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
*/
void OnRecognitionCompleted1(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {

    if (cbParam) {
        ParamCallBack1 *tmpParam = (ParamCallBack1 *) cbParam;
        if (!tmpParam->tParam) return;
        LOG_INFO("OnRecognitionCompleted: userId %lu, %s", tmpParam->userId, tmpParam->userInfo); // 仅表示自定义参数示例
    }

    LOG_INFO("OnRecognitionCompleted: status code:%d, task id:%s, result:%s", 
            cbEvent->getStatusCode(),  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
            cbEvent->getTaskId(),    // 当前任务的task id，方便定位问题，建议输出
            cbEvent->getResult());  // 获取中间识别结果

    audio_text = cbEvent->getResult();

    LOG_INFO("OnRecognitionCompleted: All response:%s", cbEvent->getAllResponse()); // 获取服务端返回的全部信息

    waiting = false;
    recording = false;
    engine_commit_text(g_engine, ibus_text_new_from_string(audio_text.c_str()));
    audio_text="";
    ibus_lookup_table_clear(g_table);
    ibus_engine_update_preedit_text(g_engine, ibus_text_new_from_string(""),0, false);
}

/**
 * @brief 识别过程发生异常时, sdk内部线程上报TaskFailed事件
 * @note 上报TaskFailed事件之后, SDK内部会关闭识别连接通道. 
 *       此时调用sendAudio会返回-1, 请停止发送.
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */

void OnRecognitionTaskFailed1(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    if (cbParam) {
        ParamCallBack1 *tmpParam = (ParamCallBack1 *) cbParam;
        LOG_INFO("taskFailed userId:%lu, %s", tmpParam->userId, tmpParam->userInfo); // 仅表示自定义参数示例
    }

    LOG_INFO("OnRecognitionTaskFailed: status code:%d, task id:%s, error message:%s", 
            cbEvent->getStatusCode(), // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
            cbEvent->getTaskId(),   // 当前任务的task id，方便定位问题，建议输出
            cbEvent->getErrorMessage());

    LOG_INFO("OnRecognitionTaskFailed: All response:%s", cbEvent->getAllResponse()); // 获取服务端返回的全部信息
    waiting = false;
    audio_text="";
    engine_commit_text(g_engine, ibus_text_new_from_string(audio_text.c_str()));
    ibus_lookup_table_clear(g_table);
    ibus_engine_update_preedit_text(g_engine, ibus_text_new_from_string(""),0, false);
}

/**
 * @brief 识别结束或发生异常时，会关闭连接通道,
 *        sdk内部线程上报ChannelCloseed事件
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */

void OnRecognitionChannelClosed1(AlibabaNls::NlsEvent *cbEvent, void *cbParam) {
    LOG_INFO("OnRecognitionChannelClosed: All response:%s", cbEvent->getAllResponse()); // 获取服务端返回的全部信息
    if (cbParam) {
        ParamCallBack1 *tmpParam = (ParamCallBack1 *) cbParam;
        LOG_INFO("OnRecognitionChannelClosed CbParam:%lu, %s", 
                tmpParam->userId,
                tmpParam->userInfo); // 仅表示自定义参数示例


        //通知发送线程, 最终识别结果已经返回, 可以调用stop()
        pthread_mutex_lock(&(tmpParam->mtxWord));
        pthread_cond_signal(&(tmpParam->cvWord));
        pthread_mutex_unlock(&(tmpParam->mtxWord));
    }
}

int recognizeAudio(ParamStruct1 *tst) {

    // 0: 从自定义线程参数中获取token, 配置文件等参数.
    if (tst == NULL) {
        LOG_ERROR("arg is not valid.");
        return -1;
    }

    //初始化自定义回调参数, 以下两变量仅作为示例表示参数传递,
    //在demo中不起任何作用
    //回调参数在堆中分配之后, SDK在销毁requesr对象时会一并销毁, 外界无需在释放
    ParamCallBack1 *cbParam = NULL;
    cbParam = new ParamCallBack1(tst);
    if (!cbParam) {
        LOG_ERROR("failed to allocate memory");
        return -2;
    }
    cbParam->userId = pthread_self();
    strcpy(cbParam->userInfo, "User.");

    /*
     * 1: 创建一句话识别SpeechRecognizerRequest对象
     */
    AlibabaNls::SpeechRecognizerRequest *request =
            AlibabaNls::NlsClient::getInstance()->createRecognizerRequest();
    if (request == NULL) {
        LOG_ERROR("createRecognizerRequest failed.");
        return -3;
    }

    // 设置start()成功回调函数
    request->setOnRecognitionStarted(OnRecognitionStarted1, cbParam);
    // 设置异常识别回调函数
    request->setOnTaskFailed(OnRecognitionTaskFailed1, cbParam);
    // 设置识别通道关闭回调函数
    request->setOnChannelClosed(OnRecognitionChannelClosed1, cbParam);
    // 设置中间结果回调函数
    request->setOnRecognitionResultChanged(OnRecognitionResultChanged1, cbParam);
    // 设置识别结束回调函数
    request->setOnRecognitionCompleted(OnRecognitionCompleted1, cbParam);

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
    //request->setEnableVoiceDetection(true);
    //允许的最大开始静音, 可选, 单位是毫秒,
    //超出后服务端将会发送RecognitionCompleted事件, 结束本次识别.
    //注意: 需要先设置enable_voice_detection为true
    //request->setMaxStartSilence(800);
    //允许的最大结束静音, 可选, 单位是毫秒,
    //超出后服务端将会发送RecognitionCompleted事件, 结束本次识别.
    //注意: 需要先设置enable_voice_detection为true
    //request->setMaxEndSilence(800);
    //request->setCustomizationId("TestId_123"); //定制模型id, 可选.
    //request->setVocabularyId("TestId_456"); //定制泛热词id, 可选.

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
    struct timespec outtime;
    struct timeval now;
    int ret = request->start();
    if (ret < 0) {
        LOG_ERROR("start failed(%d)", ret);
        AlibabaNls::NlsClient::getInstance()->releaseRecognizerRequest(request);
        return -4;
    } else {
        //等待started事件返回, 在发送
        LOG_INFO("wait started callback.");
        gettimeofday(&now, NULL);
        outtime.tv_sec = now.tv_sec + 10;
        outtime.tv_nsec = now.tv_usec * 1000;
        pthread_mutex_lock(&(cbParam->mtxWord));
        pthread_cond_timedwait(&(cbParam->cvWord), &(cbParam->mtxWord), &outtime);
        pthread_mutex_unlock(&(cbParam->mtxWord));
    }

    static const pa_sample_spec ss = {
            .format = PA_SAMPLE_S16LE,
            .rate = 16000,
            .channels = 1
    };
    pa_simple *s = NULL;
    int error;

    /* Create the recording stream */
    if (!(s = pa_simple_new(NULL, "audio_ime", PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error))) {
        LOG_INFO("pa_simple_new() failed: %s\n", pa_strerror(error));
        return -5;
    }

    struct timeval x;
    gettimeofday(&x, NULL);

    while (recording) {
        struct timeval y;
        gettimeofday(&y, NULL);
        int newRecordingTime = y.tv_sec - x.tv_sec;
        if(newRecordingTime - recordingTime >=1){
            updateIndicator();
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
         * 3: 发送音频数据: sendAudio为异步操作, 返回负值表示发送失败, 需要停止发送;
         *    返回0 为成功.
         *    notice : 返回值非成功发送字节数.
         *    若希望用省流量的opus格式上传音频数据, 则第三参数传入ENCODER_OPU
         *    ENCODER_OPU/ENCODER_OPUS模式时,nlen必须为640
         */
        ret = request->sendAudio(buf, sizeof(buf), (ENCODER_TYPE) encoder_type);
        if (ret < 0) {
            // 发送失败, 退出循环数据发送
            LOG_ERROR("send data fail(%d)", ret);
            break;
        }
    }  // while
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
        gettimeofday(&now, NULL);
        outtime.tv_sec = now.tv_sec + 10;
        outtime.tv_nsec = now.tv_usec * 1000;
        // 等待closed事件后再进行释放, 否则会出现崩溃
        pthread_mutex_lock(&(cbParam->mtxWord));
        pthread_cond_timedwait(&(cbParam->cvWord), &(cbParam->mtxWord), &outtime);
        pthread_mutex_unlock(&(cbParam->mtxWord));
    } else {
        LOG_INFO("stop ret is %d", ret);
    }
    AlibabaNls::NlsClient::getInstance()->releaseRecognizerRequest(request);
    return 0;
}


static void sigterm_cb(int sig)
{
    LOG_ERROR("sig term %d", sig);
    exit(-1);
}

static void ibus_disconnected_cb(IBusBus *bus, gpointer user_data)
{
    ibus_quit();
}

void engine_reset(IBusEngine * engine, IBusLookupTable * table) {
    ibus_lookup_table_clear(table);
    ibus_engine_hide_preedit_text(engine);
    ibus_engine_hide_auxiliary_text(engine);
    ibus_engine_hide_lookup_table(engine);
}

void engine_commit_text(IBusEngine * engine, IBusText * text) {
    ibus_engine_commit_text(engine, text);
    engine_reset(engine, g_table);
}


std::string getIndicatingMsg()
{
    std::string msg = "press space to toggle record[";
    if(recording) {
        msg += "recording " + std::to_string(recordingTime);
    }
    if(waiting) {
        msg += "waiting";
    }
    msg += "]";
    return msg;
}

void updateIndicator() {
//    ibus_lookup_table_clear(g_table);
//    ibus_lookup_table_append_candidate(g_table, ibus_text_new_from_string(getIndicatingMsg().c_str()));
//    ibus_engine_update_lookup_table_fast(g_engine, g_table, TRUE); // this line determines if lookup table is displayed
    ibus_engine_update_auxiliary_text(g_engine, ibus_text_new_from_string(getIndicatingMsg().c_str()), TRUE); 
//    ibus_lookup_table_set_cursor_pos(g_table, 0);
}

gboolean engine_process_key_event_cb(IBusEngine *engine,
                                     guint keyval,
                                     guint keycode,
                                     guint state)
{
    LOG_INFO("engine_process_key_event keycode: %d, keyval:%d", keycode, keyval);

    if(state & IBUS_RELEASE_MASK) {
        return FALSE;
    }

    if((state & IBUS_CONTROL_MASK) && keycode == 41) {
        if(waiting) {
            return TRUE;
        }
        if(!recording) {
            recording = true;
            std::thread t1([]() {
                    startRecording();
                    });
            t1.detach();
        } else {
            recording = false;
            waiting = true;
            //engine_commit_text(engine, ibus_text_new_from_string(audio_text.c_str()));
        }
        ibus_engine_show_lookup_table(engine);
        ibus_engine_show_preedit_text(engine);
        ibus_engine_show_auxiliary_text(engine);
        updateIndicator();
        return true;
    }

    // other key inputs
    if(recording || waiting) {
        // don't respond to other key inputs when recording or waiting
        return true;
    }

    if(state & IBUS_LOCK_MASK) {
        // chinese mode
        return false;

    } else {
        // english mode
        ibus_engine_hide_lookup_table(engine);
        ibus_engine_hide_preedit_text(engine);
        ibus_engine_hide_auxiliary_text(engine);
        return false;
    }
}



void engine_enable_cb(IBusEngine *engine)
{
    LOG_INFO("[IM:iBus]: IM enabled\n");
    // Setup Lookup table
    g_table = ibus_lookup_table_new(10, 0, TRUE, TRUE);
    LOG_INFO("table %p", g_table);
    g_object_ref_sink(g_table);

    ibus_lookup_table_set_orientation(g_table, IBUS_ORIENTATION_VERTICAL);
    ibus_engine_show_lookup_table(engine);
    ibus_engine_show_auxiliary_text(engine);
}

void engine_disable_cb(IBusEngine *engine)
{
    LOG_INFO("[IM:iBus]: IM disabled\n");
}

void engine_focus_out_cb(IBusEngine *engine)
{
    LOG_INFO("[IM:iBus]: IM Focus out\n");

    //   if(riti_context_ongoing_input_session(ctx)) {
    //     riti_context_finish_input_session(ctx);
    //     engine_reset();
    //   }
}

void engine_candidate_clicked_cb(IBusEngine *engine, guint index, guint button, guint state)
{
    LOG_INFO("[IM:iBus]: candidate clicked\n");
    ibus_engine_commit_text(engine, ibus_text_new_from_string("sss"));
    //   IBusText *text = ibus_lookup_table_get_candidate(table, index);
    //   riti_context_candidate_committed(ctx, index);
}

IBusEngine *create_engine_cb(IBusFactory *factory,
                             gchar *engine_name,
                             gpointer user_data)
{
    id += 1;
    gchar *path = g_strdup_printf("/org/freedesktop/IBus/Engine/%i", id);
    g_engine = ibus_engine_new(engine_name,
                               path,
                               ibus_bus_get_connection(bus));

    // Setup Lookup table
    LOG_INFO("[IM:iBus]: Creating IM Engine\n");
    LOG_INFO("[IM:iBus]: Creating IM Engine with name:%s and id:%d\n", engine_name, id);

    g_signal_connect(g_engine, "process-key-event", G_CALLBACK(engine_process_key_event_cb), NULL);
    g_signal_connect(g_engine, "enable", G_CALLBACK(engine_enable_cb), NULL);
    g_signal_connect(g_engine, "disable", G_CALLBACK(engine_disable_cb), NULL);
    g_signal_connect(g_engine, "focus-out", G_CALLBACK(engine_focus_out_cb), NULL);
    g_signal_connect(g_engine, "candidate-clicked", G_CALLBACK(engine_candidate_clicked_cb), NULL);

    return g_engine;
}
std::string g_akId="todo";
std::string g_akSecret="todo";
std::string g_token;
long g_expireTime = 0;

char url[DEFAULT_STRING_LEN];
int startRecording() {
    int ret = AlibabaNls::NlsClient::getInstance()->setLogConfig(
            "log-recognizer", AlibabaNls::LogLevel::LogDebug, 400, 50); //"log-recognizer"
    if (-1 == ret) {
        LOG_ERROR("set log failed.");
        return -1;
    }

    // 启动工作线程, 在创建请求和启动前必须调用此函数
    // 入参为负时, 启动当前系统中可用的核数
    AlibabaNls::NlsClient::getInstance()->startWorkThread();

    ParamStruct1 tst;
    memset(tst.appkey, 0, DEFAULT_STRING_LEN);
    std::string appkey = "Y0ueIZ5N4OkyfpUW";
    std::string token = "1a9838b31cd5425b80f3f7677697c252";
    std::time_t curTime = std::time(0);
    if(g_expireTime == 0 || g_expireTime<curTime < 10)
    {
        if (-1 == generateToken(g_akId, g_akSecret, &g_token, &g_expireTime)) {
            LOG_ERROR("failed to gen token");
            return -1;
        }
        memset(tst.token, 0, DEFAULT_STRING_LEN);
        memcpy(tst.token, g_token.c_str(), g_token.length());
    }
    memcpy(tst.appkey, appkey.data(), appkey.size());
    tst.appkey[appkey.size()] = '\0';

    memset(tst.url, 0, DEFAULT_STRING_LEN);

    recognizeAudio(&tst);
    AlibabaNls::NlsClient::getInstance()->releaseInstance();
    return 0;
}


int main(gint argc, gchar **argv)
{
    signal(SIGTERM, sigterm_cb);
    signal(SIGINT, sigterm_cb);
    myfile = fopen ("/home/zhangfuwen/log.txt", "a");

    ibus_init();
    bus = ibus_bus_new();
    g_object_ref_sink(bus);

            LOG_DEBUG("bus %p", bus);

    if (!ibus_bus_is_connected(bus))
    {
                LOG_WARN("not connected to ibus");
        exit(0);
    }

            LOG_DEBUG("ibus bus connected");

    g_signal_connect(bus, "disconnected", G_CALLBACK(ibus_disconnected_cb), NULL);

    IBusFactory *factory = ibus_factory_new(ibus_bus_get_connection(bus));
            LOG_DEBUG("factory %p", factory);
    g_object_ref_sink(factory);

    g_signal_connect(factory, "create-engine", G_CALLBACK(create_engine_cb), NULL);

    ibus_factory_add_engine(factory, "AudIme", IBUS_TYPE_ENGINE);

    IBusComponent *component;

    if (bus)
    {
        if (!ibus_bus_request_name(bus, "org.freedesktop.IBus.AudIme", 0))
        {
                    LOG_ERROR("error requesting bus name");
            exit(1);
        }
        else
        {
            LOG_INFO("ibus_bus_request_name success");
        }
    }
    else
    {
        component = ibus_component_new("org.freedesktop.IBus.AudIme",
                                       "LOT input method",
                                       "1.1",
                                       "MIT",
                                       "zhangfuwen",
                                       "xxx",
                                       "/usr/bin/audio_ime --ibus",
                                       "audio_ime");
                LOG_DEBUG("component %p", component);
        ibus_component_add_engine(component,
                                  ibus_engine_desc_new("AudIme",
                                                       "audo input method",
                                                       "audo input method",
                                                       "zh_CN",
                                                       "MIT",
                                                       "zhangfuwen",
                                                       "audio_ime",
                                                       "default"));
        ibus_bus_register_component(bus, component);

        ibus_bus_set_global_engine_async(bus, "AudIme", -1, nullptr, nullptr, nullptr);
    }

    LOG_INFO("entering ibus main");
    ibus_main();
    LOG_INFO("exiting ibus main");

    g_object_unref(factory);
    g_object_unref(bus);
}

