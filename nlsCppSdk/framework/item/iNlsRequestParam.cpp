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

#ifdef _MSC_VER
#include <Rpc.h>
#else
#include "uuid/uuid.h"
#endif
#include "nlsGlobal.h"
#include "nlog.h"
#include "Config.h"
#include "connectNode.h"
#include "nlsRequestParamInfo.h"
#include "iNlsRequestParam.h"

namespace AlibabaNls {

#if defined(__ANDROID__)
  const char g_sdk_name[] = "nls-cpp-sdk-android";
#elif defined(_MSC_VER)
  const char g_sdk_name[] = "nls-cpp-sdk-windows";
#elif defined(__APPLE__)
  const char g_sdk_name[] = "nls-cpp-sdk-ios";
#elif defined(__linux__)
  const char g_sdk_name[] = "nls-cpp-sdk-linux";
#else
  const char g_sdk_name[] = "nls-cpp-sdk-unknown";
#endif

const char g_sdk_language[] = "C++";
const char g_sdk_version[] = NLS_SDK_VERSION_STR;

#define STOP_RECV_TIMEOUT 12

INlsRequestParam::INlsRequestParam(NlsType mode) : _mode(mode),
                                                   _payload(Json::objectValue) {
  _url = "wss://nls-gateway.cn-shanghai.aliyuncs.com/ws/v1";
  _token = "";

  _context[D_SDK_CLIENT] = getSdkInfo();

#if defined(_WIN32)
  _outputFormat = D_DEFAULT_VALUE_ENCODE_GBK;
#else
  _outputFormat = D_DEFAULT_VALUE_ENCODE_UTF8;
#endif

  _payload[D_FORMAT] = D_DEFAULT_VALUE_AUDIO_ENCODE;
  _payload[D_SAMPLE_RATE] = D_DEFAULT_VALUE_SAMPLE_RATE;

  _requestType = SpeechNormal;
  _timeout = STOP_RECV_TIMEOUT;

  _enableWakeWord = false;
}

INlsRequestParam::~INlsRequestParam() {}

std::string INlsRequestParam::getRandomUuid() {
  char uuidBuff[48] = {0};

#ifdef _MSC_VER
  char* data = NULL;
  UUID uuidhandle;
  RPC_STATUS ret_val = UuidCreate(&uuidhandle);
  if (ret_val != RPC_S_OK) {
    LOG_ERROR("UuidCreate failed");
    return uuidBuff;
  }

  UuidToString(&uuidhandle, (RPC_CSTR*)&data);
  if (data == NULL) {
    LOG_ERROR("UuidToString data is nullptr");
    return uuidBuff;
  }

  int len = strnlen(data, 36);
  int i = 0, j = 0;
  for (i = 0; i < len; i++) {
    if (data[i] != '-') {
      uuidBuff[j++] = data[i];
    }
  }

  RpcStringFree((RPC_CSTR*)&data);
#else
  char tmp[48] = {0};
  uuid_t uuid;
  uuid_generate(uuid);
  uuid_unparse(uuid, tmp);

  int i = 0, j = 0;
  while (tmp[i]) {
    if (tmp[i] != '-') {
      uuidBuff[j++] = tmp[i];
    }
    i++;
  }
#endif
  return uuidBuff;
}

Json::Value INlsRequestParam::getSdkInfo() {
  Json::Value sdkInfo;

  sdkInfo[D_SDK_NAME] = g_sdk_name;
  sdkInfo[D_SDK_VERSION] = g_sdk_version;
  sdkInfo[D_SDK_LANGUAGE] = g_sdk_language;

  return sdkInfo;
}

const char* INlsRequestParam::getStartCommand() {
  Json::Value root;
  Json::FastWriter writer;

  _task_id = getRandomUuid();
  _header[D_TASK_ID] = _task_id;
  LOG_DEBUG("TaskId:%s", _task_id.c_str());
  _header[D_MESSAGE_ID] = getRandomUuid();

  root[D_HEADER] = _header;
  root[D_PAYLOAD] = _payload;
  root[D_CONTEXT] = _context;

  _startCommand = writer.write(root);

  LOG_INFO("Start:%s", _startCommand.c_str());

  return _startCommand.c_str();
}

const char* INlsRequestParam::getControlCommand(const char* message) {
  Json::Value root;
  Json::Value inputRoot;
  Json::FastWriter writer;
  Json::Reader reader;
  std::string logInfo;

  if (!reader.parse(message, inputRoot)) {
    logInfo = "parse json fail: %s";
    logInfo += message;
    LOG_ERROR(logInfo.c_str());
    return NULL;
  }

  if (!inputRoot.isObject()) {
    LOG_ERROR("value isnot a json object.");
    return NULL;
  }

  _header[D_TASK_ID] = _task_id;
  LOG_DEBUG("TaskId:%s", _task_id.c_str());
  _header[D_MESSAGE_ID] = getRandomUuid();

  root[D_HEADER] = _header;
  if (!inputRoot[D_PAYLOAD].isNull()) {
    root[D_PAYLOAD] = inputRoot[D_PAYLOAD];
  }
  if (!inputRoot[D_CONTEXT].isNull()) {
    root[D_CONTEXT] = inputRoot[D_CONTEXT];
  }

  _controlCommand = writer.write(root);
  LOG_INFO("Control:%s", _controlCommand.c_str());

  return _controlCommand.c_str();
}

const char* INlsRequestParam::getStopCommand() {
  Json::Value root;
  Json::FastWriter writer;

  _header[D_TASK_ID] = _task_id;
  _header[D_MESSAGE_ID] = getRandomUuid();

//  Json::FastWriter writer2;
//  LOG_DEBUG("STOP_DEBUG:%s", writer2.write(_header).c_str());

  root[D_HEADER] = _header;
  root[D_CONTEXT] = _context;

  _stopCommand = writer.write(root);
  LOG_INFO("STOP:%s", _stopCommand.c_str());
  return _stopCommand.c_str();
}

const char* INlsRequestParam::getExecuteDialog() {return "";}

const char* INlsRequestParam::getStopWakeWordCommand() {return "";}

int INlsRequestParam::setPayloadParam(const char* value) {
  Json::Value root;
  Json::Reader reader;
  Json::Value::iterator iter;
  Json::Value::Members members;
  std::string tmpValue = value;
  std::string logInfo;

  if (!reader.parse(tmpValue, root)) {
    logInfo = "parse json fail: %s";
    logInfo += value;
    LOG_ERROR(logInfo.c_str());
    return -1;
  }

  if (!root.isObject()) {
    LOG_ERROR("value is n't a json object.");
    return -1;
  }

  std::string jsonKey;
  std::string jsonValue;
  members = root.getMemberNames();
  Json::Value::Members::iterator it = members.begin();
  for (; it != members.end(); ++it) {
    jsonKey = *it;

    logInfo = "json key:";
    logInfo += jsonKey;
    LOG_DEBUG(logInfo.c_str());

    _payload[jsonKey.c_str()] = root[jsonKey.c_str()];
  }

  return 0;
}

int INlsRequestParam::setContextParam(const char* value) {
  Json::Value root;
  Json::Reader reader;
  Json::Value::iterator iter;
  Json::Value::Members members;
  std::string tmpValue = value;
  std::string logInfo;

  if (!reader.parse(tmpValue, root)) {
    logInfo = "parse json fail: %s";
    logInfo += value;
    LOG_ERROR(logInfo.c_str());
    return -1;
  }

  if (!root.isObject()) {
    LOG_ERROR("value is n't a json object.");
    return -1;
  }

  std::string jsonKey;
  std::string jsonValue;
  members = root.getMemberNames();
  Json::Value::Members::iterator it = members.begin();
  for (; it != members.end(); ++it) {
    jsonKey = *it;

    logInfo = "json key:";
    logInfo += jsonKey;
    LOG_DEBUG(logInfo.c_str());

    _context[jsonKey.c_str()] = root[jsonKey.c_str()];
  }

  return 0;
}

void INlsRequestParam::setAppKey(const char* appKey) {
  _header[D_APP_KEY] = appKey;
};

void INlsRequestParam::setFormat(const char* format) {
  _format = format;
  _payload[D_FORMAT] = format;
};

void INlsRequestParam::setIntermediateResult(bool value) {
  _payload[D_SR_INTERMEDIATE_RESULT] = value;
};

void INlsRequestParam::setPunctuationPrediction(bool value) {
  _payload[D_SR_PUNCTUATION_PREDICTION] = value;
};

void INlsRequestParam::setTextNormalization(bool value) {
  _payload[D_SR_TEXT_NORMALIZATION] = value;
};

int INlsRequestParam::setCustomizationId(const char * value) {
  if (!value) {
    return -1;
  }

  _payload[D_SR_CUSTOMIZATION_ID] = value;

  return 0;
}

int INlsRequestParam::setVocabularyId(const char * value) {
  if (!value) {
    return -1;
  }

  _payload[D_SR_VOCABULARY_ID] = value;

  return 0;
}

void INlsRequestParam::setSentenceDetection(bool value) {
  _payload[D_SR_SENTENCE_DETECTION] = value;
};

void INlsRequestParam::setSampleRate(int sampleRate) {
  _sampleRate = sampleRate;
  _payload[D_SAMPLE_RATE] = sampleRate;
};

int INlsRequestParam::setEnableWakeWordVerification(bool value) {
  _payload[D_DA_WAKE_WORD_VERIFICATION] = value;

  _enableWakeWord = value;

  return 0;
}

int INlsRequestParam::setSessionId(const char* sessionId) {
  _payload[D_DA_SESSION_ID] = sessionId;
  return 0;
}

int INlsRequestParam::AppendHttpHeader(const char* key, const char* value) {
  _httpHeader[key] = value;
  return 0;
}

std::string INlsRequestParam::GetHttpHeader() {
  Json::Value::iterator iter;
  Json::Value::Members members;

  _httpHeaderString.clear();

  if (_httpHeader.empty()) {
    return _httpHeaderString;
  }

  std::string jsonKey;
  std::string jsonValue;
  members = _httpHeader.getMemberNames();
  Json::Value::Members::iterator it = members.begin();
  for (; it != members.end(); ++it) {
    jsonKey = *it;

    _httpHeaderString += jsonKey;
    _httpHeaderString += ": ";
    _httpHeaderString += _httpHeader[jsonKey].asString();
    _httpHeaderString += "\r\n";
  }

  LOG_DEBUG("HttpHeader:%s", _httpHeaderString.c_str());
  return _httpHeaderString;
}

}  // namespace AlibabaNls
