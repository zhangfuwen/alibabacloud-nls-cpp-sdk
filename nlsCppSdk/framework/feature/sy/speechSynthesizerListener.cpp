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

#include "speechSynthesizerListener.h"
#include "speechSynthesizerRequest.h"
#include "nlog.h"


namespace AlibabaNls {

SpeechSynthesizerListener::SpeechSynthesizerListener(
    SpeechSynthesizerCallback* cb) : _callback(cb) {}

SpeechSynthesizerListener::~SpeechSynthesizerListener() {}

void SpeechSynthesizerListener::handlerFrame(NlsEvent str) {
  NlsEvent::EventType type = str.getMsgType();

  if (NULL == _callback) {
    LOG_ERROR("callback is NULL");
    return;
  }

  switch(type) {
    case NlsEvent::SynthesisStarted:
      if (NULL != _callback->_onSynthesisStarted) {
        _callback->_onSynthesisStarted(
            &str, _callback->_paramap[NlsEvent::SynthesisStarted]);
      }
      break;
    case NlsEvent::SynthesisCompleted:
      if (NULL != _callback->_onSynthesisCompleted) {
        _callback->_onSynthesisCompleted(&str, _callback->_paramap[NlsEvent::SynthesisCompleted]);
      }
      break;
    case NlsEvent::Close:
      if (NULL != _callback->_onChannelClosed) {
        _callback->_onChannelClosed(&str, _callback->_paramap[NlsEvent::Close]);
      }
      break;
    case NlsEvent::Binary:
      if (NULL != _callback->_onBinaryDataReceived) {
        _callback->_onBinaryDataReceived(
            &str, _callback->_paramap[NlsEvent::Binary]);
      }
      break;
    case NlsEvent::MetaInfo:
      if (NULL != _callback->_onMetaInfo) {
        _callback->_onMetaInfo(&str, _callback->_paramap[NlsEvent::MetaInfo]);
      }
      break;
    default:
      if (NULL != _callback->_onTaskFailed) {
        _callback->_onTaskFailed(&str, _callback->_paramap[NlsEvent::TaskFailed]);
      }
      break;
  }

  return;
}

}
