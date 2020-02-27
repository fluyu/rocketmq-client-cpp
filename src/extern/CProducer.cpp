/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "c/CProducer.h"

#include <cstring>
#include <functional>
#include <typeindex>

#include "ClientRPCHook.h"
#include "DefaultMQProducer.h"
#include "Logging.h"
#include "MQClientErrorContainer.h"
#include "UtilAll.h"

using namespace rocketmq;

class SelectMessageQueueInner : public MessageQueueSelector {
 public:
  MQMessageQueue select(const std::vector<MQMessageQueue>& mqs, const MQMessage& msg, void* arg) {
    int index = 0;
    std::string shardingKey = UtilAll::to_string((char*)arg);

    index = std::hash<std::string>{}(shardingKey) % mqs.size();
    return mqs[index % mqs.size()];
  }
};

class SelectMessageQueue : public MessageQueueSelector {
 public:
  SelectMessageQueue(QueueSelectorCallback callback) { m_pCallback = callback; }

  MQMessageQueue select(const std::vector<MQMessageQueue>& mqs, const MQMessage& msg, void* arg) {
    CMessage* message = (CMessage*)&msg;
    // Get the index of sending MQMessageQueue through callback function.
    int index = m_pCallback(mqs.size(), message, arg);
    return mqs[index];
  }

 private:
  QueueSelectorCallback m_pCallback;
};

class COnSendCallback : public AutoDeleteSendCallback {
 public:
  COnSendCallback(COnSendSuccessCallback cSendSuccessCallback,
                  COnSendExceptionCallback cSendExceptionCallback,
                  void* message,
                  void* userData) {
    m_cSendSuccessCallback = cSendSuccessCallback;
    m_cSendExceptionCallback = cSendExceptionCallback;
    m_message = message;
    m_userData = userData;
  }

  virtual ~COnSendCallback() = default;

  void onSuccess(SendResult& sendResult) override {
    CSendResult result;
    result.sendStatus = CSendStatus((int)sendResult.getSendStatus());
    result.offset = sendResult.getQueueOffset();
    strncpy(result.msgId, sendResult.getMsgId().c_str(), MAX_MESSAGE_ID_LENGTH - 1);
    result.msgId[MAX_MESSAGE_ID_LENGTH - 1] = 0;
    m_cSendSuccessCallback(result, (CMessage*)m_message, m_userData);
  }

  void onException(MQException& e) noexcept override {
    CMQException exception;
    exception.error = e.GetError();
    exception.line = e.GetLine();
    strncpy(exception.msg, e.what(), MAX_EXEPTION_MSG_LENGTH - 1);
    strncpy(exception.file, e.GetFile(), MAX_EXEPTION_FILE_LENGTH - 1);
    m_cSendExceptionCallback(exception, (CMessage*)m_message, m_userData);
  }

 private:
  COnSendSuccessCallback m_cSendSuccessCallback;
  COnSendExceptionCallback m_cSendExceptionCallback;
  void* m_message;
  void* m_userData;
};

class CSendCallback : public AutoDeleteSendCallback {
 public:
  CSendCallback(CSendSuccessCallback cSendSuccessCallback, CSendExceptionCallback cSendExceptionCallback) {
    m_cSendSuccessCallback = cSendSuccessCallback;
    m_cSendExceptionCallback = cSendExceptionCallback;
  }

  virtual ~CSendCallback() = default;

  void onSuccess(SendResult& sendResult) override {
    CSendResult result;
    result.sendStatus = CSendStatus((int)sendResult.getSendStatus());
    result.offset = sendResult.getQueueOffset();
    strncpy(result.msgId, sendResult.getMsgId().c_str(), MAX_MESSAGE_ID_LENGTH - 1);
    result.msgId[MAX_MESSAGE_ID_LENGTH - 1] = 0;
    m_cSendSuccessCallback(result);
  }

  void onException(MQException& e) noexcept override {
    CMQException exception;
    exception.error = e.GetError();
    exception.line = e.GetLine();
    strncpy(exception.msg, e.what(), MAX_EXEPTION_MSG_LENGTH - 1);
    strncpy(exception.file, e.GetFile(), MAX_EXEPTION_FILE_LENGTH - 1);
    m_cSendExceptionCallback(exception);
  }

 private:
  CSendSuccessCallback m_cSendSuccessCallback;
  CSendExceptionCallback m_cSendExceptionCallback;
};

CProducer* CreateProducer(const char* groupId) {
  if (groupId == NULL) {
    return NULL;
  }
  auto* defaultMQProducer = new DefaultMQProducer(groupId);
  return (CProducer*)defaultMQProducer;
}

CProducer* CreateOrderlyProducer(const char* groupId) {
  return CreateProducer(groupId);
}

int DestroyProducer(CProducer* producer) {
  if (producer == nullptr) {
    return NULL_POINTER;
  }
  delete reinterpret_cast<DefaultMQProducer*>(producer);
  return OK;
}

int StartProducer(CProducer* producer) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  try {
    reinterpret_cast<DefaultMQProducer*>(producer)->start();
  } catch (std::exception& e) {
    MQClientErrorContainer::setErr(std::string(e.what()));
    return PRODUCER_START_FAILED;
  }
  return OK;
}

int ShutdownProducer(CProducer* producer) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  reinterpret_cast<DefaultMQProducer*>(producer)->shutdown();
  return OK;
}

int SetProducerNameServerAddress(CProducer* producer, const char* namesrv) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  reinterpret_cast<DefaultMQProducer*>(producer)->setNamesrvAddr(namesrv);
  return OK;
}

// Deprecated
int SetProducerNameServerDomain(CProducer* producer, const char* domain) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  // reinterpret_cast<DefaultMQProducer*>(producer)->setNamesrvDomain(domain);
  return OK;
}

int SendMessageSync(CProducer* producer, CMessage* msg, CSendResult* result) {
  // CSendResult sendResult;
  if (producer == NULL || msg == NULL || result == NULL) {
    return NULL_POINTER;
  }
  try {
    DefaultMQProducer* defaultMQProducer = (DefaultMQProducer*)producer;
    MQMessage* message = (MQMessage*)msg;
    SendResult sendResult = defaultMQProducer->send(message);
    switch (sendResult.getSendStatus()) {
      case SEND_OK:
        result->sendStatus = E_SEND_OK;
        break;
      case SEND_FLUSH_DISK_TIMEOUT:
        result->sendStatus = E_SEND_FLUSH_DISK_TIMEOUT;
        break;
      case SEND_FLUSH_SLAVE_TIMEOUT:
        result->sendStatus = E_SEND_FLUSH_SLAVE_TIMEOUT;
        break;
      case SEND_SLAVE_NOT_AVAILABLE:
        result->sendStatus = E_SEND_SLAVE_NOT_AVAILABLE;
        break;
      default:
        result->sendStatus = E_SEND_OK;
        break;
    }
    result->offset = sendResult.getQueueOffset();
    strncpy(result->msgId, sendResult.getMsgId().c_str(), MAX_MESSAGE_ID_LENGTH - 1);
    result->msgId[MAX_MESSAGE_ID_LENGTH - 1] = 0;
  } catch (std::exception& e) {
    MQClientErrorContainer::setErr(std::string(e.what()));
    return PRODUCER_SEND_SYNC_FAILED;
  }
  return OK;
}

int SendBatchMessage(CProducer* producer, CBatchMessage* batcMsg, CSendResult* result) {
  // CSendResult sendResult;
  if (producer == NULL || batcMsg == NULL || result == NULL) {
    return NULL_POINTER;
  }
  try {
    DefaultMQProducer* defaultMQProducer = (DefaultMQProducer*)producer;
    std::vector<MQMessage*>* message = (std::vector<MQMessage*>*)batcMsg;
    SendResult sendResult = defaultMQProducer->send(*message);
    switch (sendResult.getSendStatus()) {
      case SEND_OK:
        result->sendStatus = E_SEND_OK;
        break;
      case SEND_FLUSH_DISK_TIMEOUT:
        result->sendStatus = E_SEND_FLUSH_DISK_TIMEOUT;
        break;
      case SEND_FLUSH_SLAVE_TIMEOUT:
        result->sendStatus = E_SEND_FLUSH_SLAVE_TIMEOUT;
        break;
      case SEND_SLAVE_NOT_AVAILABLE:
        result->sendStatus = E_SEND_SLAVE_NOT_AVAILABLE;
        break;
      default:
        result->sendStatus = E_SEND_OK;
        break;
    }
    result->offset = sendResult.getQueueOffset();
    strncpy(result->msgId, sendResult.getMsgId().c_str(), MAX_MESSAGE_ID_LENGTH - 1);
    result->msgId[MAX_MESSAGE_ID_LENGTH - 1] = 0;
  } catch (std::exception& e) {
    return PRODUCER_SEND_SYNC_FAILED;
  }
  return OK;
}

int SendMessageAsync(CProducer* producer,
                     CMessage* msg,
                     CSendSuccessCallback cSendSuccessCallback,
                     CSendExceptionCallback cSendExceptionCallback) {
  if (producer == NULL || msg == NULL || cSendSuccessCallback == NULL || cSendExceptionCallback == NULL) {
    return NULL_POINTER;
  }
  DefaultMQProducer* defaultMQProducer = (DefaultMQProducer*)producer;
  MQMessage* message = (MQMessage*)msg;
  CSendCallback* cSendCallback = new CSendCallback(cSendSuccessCallback, cSendExceptionCallback);
  defaultMQProducer->send(message, cSendCallback);
  return OK;
}

int SendAsync(CProducer* producer,
              CMessage* msg,
              COnSendSuccessCallback onSuccess,
              COnSendExceptionCallback onException,
              void* usrData) {
  if (producer == NULL || msg == NULL || onSuccess == NULL || onException == NULL) {
    return NULL_POINTER;
  }
  DefaultMQProducer* defaultMQProducer = (DefaultMQProducer*)producer;
  MQMessage* message = (MQMessage*)msg;
  COnSendCallback* cSendCallback = new COnSendCallback(onSuccess, onException, (void*)msg, usrData);
  defaultMQProducer->send(message, cSendCallback);
  return OK;
}

int SendMessageOneway(CProducer* producer, CMessage* msg) {
  if (producer == NULL || msg == NULL) {
    return NULL_POINTER;
  }
  DefaultMQProducer* defaultMQProducer = (DefaultMQProducer*)producer;
  MQMessage* message = (MQMessage*)msg;
  try {
    defaultMQProducer->sendOneway(message);
  } catch (std::exception& e) {
    return PRODUCER_SEND_ONEWAY_FAILED;
  }
  return OK;
}

int SendMessageOnewayOrderly(CProducer* producer, CMessage* msg, QueueSelectorCallback selector, void* arg) {
  if (producer == NULL || msg == NULL) {
    return NULL_POINTER;
  }
  DefaultMQProducer* defaultMQProducer = (DefaultMQProducer*)producer;
  MQMessage* message = (MQMessage*)msg;
  try {
    SelectMessageQueue selectMessageQueue(selector);
    defaultMQProducer->sendOneway(message, &selectMessageQueue, arg);
  } catch (std::exception& e) {
    MQClientErrorContainer::setErr(std::string(e.what()));
    return PRODUCER_SEND_ONEWAY_FAILED;
  }
  return OK;
}

int SendMessageOrderlyAsync(CProducer* producer,
                            CMessage* msg,
                            QueueSelectorCallback callback,
                            void* arg,
                            CSendSuccessCallback cSendSuccessCallback,
                            CSendExceptionCallback cSendExceptionCallback) {
  if (producer == NULL || msg == NULL || callback == NULL || cSendSuccessCallback == NULL ||
      cSendExceptionCallback == NULL) {
    return NULL_POINTER;
  }
  DefaultMQProducer* defaultMQProducer = (DefaultMQProducer*)producer;
  MQMessage* message = (MQMessage*)msg;
  CSendCallback* cSendCallback = new CSendCallback(cSendSuccessCallback, cSendExceptionCallback);
  // Constructing SelectMessageQueue objects through function pointer callback
  SelectMessageQueue selectMessageQueue(callback);
  defaultMQProducer->send(message, &selectMessageQueue, arg, cSendCallback);
  return OK;
}

int SendMessageOrderly(CProducer* producer,
                       CMessage* msg,
                       QueueSelectorCallback callback,
                       void* arg,
                       int autoRetryTimes,
                       CSendResult* result) {
  if (producer == NULL || msg == NULL || callback == NULL || arg == NULL || result == NULL) {
    return NULL_POINTER;
  }
  DefaultMQProducer* defaultMQProducer = (DefaultMQProducer*)producer;
  MQMessage* message = (MQMessage*)msg;
  try {
    // Constructing SelectMessageQueue objects through function pointer callback
    SelectMessageQueue selectMessageQueue(callback);
    SendResult sendResult = defaultMQProducer->send(message, &selectMessageQueue, arg);
    // Convert SendStatus to CSendStatus
    result->sendStatus = CSendStatus((int)sendResult.getSendStatus());
    result->offset = sendResult.getQueueOffset();
    strncpy(result->msgId, sendResult.getMsgId().c_str(), MAX_MESSAGE_ID_LENGTH - 1);
    result->msgId[MAX_MESSAGE_ID_LENGTH - 1] = 0;
  } catch (std::exception& e) {
    MQClientErrorContainer::setErr(std::string(e.what()));
    return PRODUCER_SEND_ORDERLY_FAILED;
  }
  return OK;
}

int SendMessageOrderlyByShardingKey(CProducer* producer, CMessage* msg, const char* shardingKey, CSendResult* result) {
  if (producer == NULL || msg == NULL || shardingKey == NULL || result == NULL) {
    return NULL_POINTER;
  }
  DefaultMQProducer* defaultMQProducer = (DefaultMQProducer*)producer;
  MQMessage* message = (MQMessage*)msg;
  try {
    // Constructing SelectMessageQueue objects through function pointer callback
    int retryTimes = 3;
    SelectMessageQueueInner selectMessageQueue;
    SendResult sendResult = defaultMQProducer->send(message, &selectMessageQueue, (void*)shardingKey, retryTimes);
    // Convert SendStatus to CSendStatus
    result->sendStatus = CSendStatus((int)sendResult.getSendStatus());
    result->offset = sendResult.getQueueOffset();
    strncpy(result->msgId, sendResult.getMsgId().c_str(), MAX_MESSAGE_ID_LENGTH - 1);
    result->msgId[MAX_MESSAGE_ID_LENGTH - 1] = 0;
  } catch (std::exception& e) {
    MQClientErrorContainer::setErr(std::string(e.what()));
    return PRODUCER_SEND_ORDERLY_FAILED;
  }
  return OK;
}

int SetProducerGroupName(CProducer* producer, const char* groupName) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  reinterpret_cast<DefaultMQProducer*>(producer)->setGroupName(groupName);
  return OK;
}

int SetProducerInstanceName(CProducer* producer, const char* instanceName) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  reinterpret_cast<DefaultMQProducer*>(producer)->setInstanceName(instanceName);
  return OK;
}

int SetProducerSessionCredentials(CProducer* producer,
                                  const char* accessKey,
                                  const char* secretKey,
                                  const char* onsChannel) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  auto rpcHook = std::make_shared<ClientRPCHook>(SessionCredentials(accessKey, secretKey, onsChannel));
  reinterpret_cast<DefaultMQProducer*>(producer)->setRPCHook(rpcHook);
  return OK;
}

int SetProducerLogPath(CProducer* producer, const char* logPath) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  // Todo, This api should be implemented by core api.
  // reinterpret_cast<DefaultMQProducer*>(producer)->setLogFileSizeAndNum(3, 102400000);
  return OK;
}

int SetProducerLogFileNumAndSize(CProducer* producer, int fileNum, long fileSize) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  ALOG_ADAPTER->setLogFileNumAndSize(fileNum, fileSize);
  return OK;
}

int SetProducerLogLevel(CProducer* producer, CLogLevel level) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  ALOG_ADAPTER->setLogLevel((elogLevel)level);
  return OK;
}

int SetProducerSendMsgTimeout(CProducer* producer, int timeout) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  reinterpret_cast<DefaultMQProducer*>(producer)->setSendMsgTimeout(timeout);
  return OK;
}

int SetProducerCompressMsgBodyOverHowmuch(CProducer* producer, int howmuch) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  reinterpret_cast<DefaultMQProducer*>(producer)->setCompressMsgBodyOverHowmuch(howmuch);
  return OK;
}

int SetProducerCompressLevel(CProducer* producer, int level) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  reinterpret_cast<DefaultMQProducer*>(producer)->setCompressLevel(level);
  return OK;
}

int SetProducerMaxMessageSize(CProducer* producer, int size) {
  if (producer == NULL) {
    return NULL_POINTER;
  }
  reinterpret_cast<DefaultMQProducer*>(producer)->setMaxMessageSize(size);
  return OK;
}
