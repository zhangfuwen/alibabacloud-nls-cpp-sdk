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

#include "iNlsRequestListener.h"
#include "nlog.h"

namespace AlibabaNls {


INlsRequestListener::INlsRequestListener() {}
INlsRequestListener::~INlsRequestListener() {}

void INlsRequestListener::handlerFrame(std::string errorInfo,
                                       int errorCode,
                                       NlsEvent::EventType type,
                                       std::string taskId) {
    LOG_DEBUG("Event Type: %d.", type);
    NlsEvent* nlsevent = new NlsEvent(
        errorInfo.c_str(), errorCode, type, taskId);
    handlerFrame(*nlsevent);
    delete nlsevent;
    nlsevent = NULL;

    if (NlsEvent::TaskFailed == type) {
      LOG_ERROR(errorInfo.c_str());
    } else {
      LOG_DEBUG(errorInfo.c_str());
    }
}

}
