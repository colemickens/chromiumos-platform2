//
// Copyright (C) 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "trunks/trunks_dbus_proxy.h"

#include <memory>
#include <string>

#include <base/bind.h>
#include <base/threading/thread.h>
#include <brillo/bind_lambda.h>
#include <dbus/object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trunks/error_codes.h"
#include "trunks/interface.pb.h"
#include "trunks/mock_dbus_bus.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace {

class FakeObjectProxy : public dbus::ObjectProxy {
 public:
  FakeObjectProxy() : dbus::ObjectProxy(nullptr, "", dbus::ObjectPath(), 0) {}

  void CallMethodWithErrorCallback(
      dbus::MethodCall* method_call,
      int timeout_ms,
      dbus::ObjectProxy::ResponseCallback callback,
      dbus::ObjectProxy::ErrorCallback error_callback) override {
    dbus::ScopedDBusError error;
    std::unique_ptr<dbus::Response> response =
        CallMethodAndBlockWithErrorDetails(method_call, timeout_ms, &error);
    if (response) {
      callback.Run(response.get());
    } else {
      method_call->SetSerial(1);
      std::unique_ptr<dbus::ErrorResponse> error_response =
          dbus::ErrorResponse::FromMethodCall(method_call,
                                              "org.MyError",
                                              "Error message");
      error_callback.Run(error_response.get());
    }
  }

  std::unique_ptr<dbus::Response> CallMethodAndBlockWithErrorDetails(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      dbus::ScopedDBusError* error) override {
    dbus::MessageReader reader(method_call);
    trunks::SendCommandRequest command_proto;
    brillo::dbus_utils::PopValueFromReader(&reader, &command_proto);
    last_command_ = command_proto.command();
    if (next_response_.empty()) {
      return std::unique_ptr<dbus::Response>();
    }
    std::unique_ptr<dbus::Response> dbus_response =
        dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(dbus_response.get());
    trunks::SendCommandResponse response_proto;
    response_proto.set_response(next_response_);
    brillo::dbus_utils::AppendValueToWriter(&writer, response_proto);
    return dbus_response;
  }

  std::string next_response_;
  std::string last_command_;
};

}  // namespace

namespace trunks {

class TrunksDBusProxyTest : public testing::Test {
 public:
  TrunksDBusProxyTest() {
    proxy_.set_init_timeout(base::TimeDelta());
    proxy_.set_init_attempt_delay(base::TimeDelta());
  }

  void SetUp() override {
    ON_CALL(*bus_, Connect())
        .WillByDefault(Return(true));
    ON_CALL(*bus_, GetObjectProxy(_, _))
        .WillByDefault(Return(object_proxy_.get()));
    ON_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
        .WillByDefault(Return("test-service-owner"));
  }

  void set_next_response(const std::string& response) {
    object_proxy_->next_response_ = response;
  }
  std::string last_command() const {
    std::string last_command = object_proxy_->last_command_;
    object_proxy_->last_command_.clear();
    return last_command;
  }

 protected:
  scoped_refptr<FakeObjectProxy> object_proxy_ = new FakeObjectProxy();
  scoped_refptr<NiceMock<MockDBusBus>> bus_ = new NiceMock<MockDBusBus>();
  TrunksDBusProxy proxy_{bus_.get()};
};

TEST_F(TrunksDBusProxyTest, InitSuccess) {
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
      .WillOnce(Return("test-service-owner"))
      .WillOnce(Return("test-service-owner"));
  // Before initialization IsServiceReady fails without checking.
  EXPECT_FALSE(proxy_.IsServiceReady(false /* force_check */));
  EXPECT_FALSE(proxy_.IsServiceReady(true /* force_check */));
  EXPECT_TRUE(proxy_.Init());
  EXPECT_TRUE(proxy_.IsServiceReady(false /* force_check */));
  EXPECT_TRUE(proxy_.IsServiceReady(true /* force_check */));
}

TEST_F(TrunksDBusProxyTest, InitFailure) {
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
      .WillRepeatedly(Return(""));
  EXPECT_FALSE(proxy_.Init());
  EXPECT_FALSE(proxy_.IsServiceReady(false /* force_check */));
  EXPECT_FALSE(proxy_.IsServiceReady(true /* force_check */));
}

TEST_F(TrunksDBusProxyTest, InitRetrySuccess) {
  proxy_.set_init_timeout(base::TimeDelta::FromMilliseconds(100));
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
      .WillOnce(Return(""))
      .WillOnce(Return("test-service-owner"))
      .WillOnce(Return("test-service-owner"));
  EXPECT_TRUE(proxy_.Init());
  EXPECT_TRUE(proxy_.IsServiceReady(false /* force_check */));
  EXPECT_TRUE(proxy_.IsServiceReady(true /* force_check */));
}

TEST_F(TrunksDBusProxyTest, SendCommandSuccess) {
  EXPECT_TRUE(proxy_.Init());
  set_next_response("response");
  auto callback = [](const std::string& response) {
    EXPECT_EQ("response", response);
  };
  proxy_.SendCommand("command", base::Bind(callback));
  EXPECT_EQ("command", last_command());
}

TEST_F(TrunksDBusProxyTest, SendCommandAndWaitSuccess) {
  EXPECT_TRUE(proxy_.Init());
  set_next_response("response");
  EXPECT_EQ("response", proxy_.SendCommandAndWait("command"));
  EXPECT_EQ("command", last_command());
}

TEST_F(TrunksDBusProxyTest, SendCommandFailureInit) {
  // If Init() failed, SAPI_RC_NO_CONNECTION should be returned
  // without sending a command.
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
      .WillRepeatedly(Return(""));
  EXPECT_FALSE(proxy_.Init());
  set_next_response("");
  auto callback = [](const std::string& response) {
    EXPECT_EQ(CreateErrorResponse(SAPI_RC_NO_CONNECTION), response);
  };
  proxy_.SendCommand("command", base::Bind(callback));
  EXPECT_EQ("", last_command());
}

TEST_F(TrunksDBusProxyTest, SendCommandAndWaitFailureInit) {
  // If Init() failed, SAPI_RC_NO_CONNECTION should be returned
  // without sending a command.
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
      .WillRepeatedly(Return(""));
  EXPECT_FALSE(proxy_.Init());
  set_next_response("");
  EXPECT_EQ(CreateErrorResponse(SAPI_RC_NO_CONNECTION),
            proxy_.SendCommandAndWait("command"));
  EXPECT_EQ("", last_command());
}

TEST_F(TrunksDBusProxyTest, SendCommandFailureNoConnection) {
  // If Init() succeeded, but service is later lost, it should return
  // SAPI_RC_NO_CONNECTION in case there was no response.
  EXPECT_TRUE(proxy_.Init());
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
      .WillRepeatedly(Return(""));
  set_next_response("");
  auto callback = [](const std::string& response) {
    EXPECT_EQ(CreateErrorResponse(SAPI_RC_NO_CONNECTION), response);
  };
  proxy_.SendCommand("command", base::Bind(callback));
  EXPECT_EQ("command", last_command());
}

TEST_F(TrunksDBusProxyTest, SendCommandAndWaitFailureNoConnection) {
  // If Init() succeeded, but service is later lost, it should return
  // SAPI_RC_NO_CONNECTION in case there was no response.
  EXPECT_TRUE(proxy_.Init());
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
      .WillRepeatedly(Return(""));
  set_next_response("");
  EXPECT_EQ(CreateErrorResponse(SAPI_RC_NO_CONNECTION),
            proxy_.SendCommandAndWait("command"));
  EXPECT_EQ("command", last_command());
}

TEST_F(TrunksDBusProxyTest, SendCommandFailureNoResponse) {
  // If Init() succeeded and the service is ready, it should return
  // an appropriate error code in case there was no response.
  EXPECT_TRUE(proxy_.Init());
  set_next_response("");
  auto callback = [](const std::string& response) {
    EXPECT_EQ(CreateErrorResponse(SAPI_RC_NO_RESPONSE_RECEIVED), response);
  };
  proxy_.SendCommand("command", base::Bind(callback));
  EXPECT_EQ("command", last_command());
}

TEST_F(TrunksDBusProxyTest, SendCommandAndWaitFailureNoResponse) {
  // If Init() succeeded and the service is ready, it should return
  // an appropriate error code in case there was no response.
  EXPECT_TRUE(proxy_.Init());
  set_next_response("");
  EXPECT_EQ(CreateErrorResponse(SAPI_RC_NO_RESPONSE_RECEIVED),
            proxy_.SendCommandAndWait("command"));
  EXPECT_EQ("command", last_command());
}

TEST_F(TrunksDBusProxyTest, SendCommandFailureWrongThread) {
  // Attempting to send from a wrong thread should return TRUNKS_RC_IPC_ERROR
  // without sending the command.
  EXPECT_TRUE(proxy_.Init());
  set_next_response("response");
  base::Thread other_thread("trunks_dbus_proxy_test other thread");
  ASSERT_TRUE(other_thread.Start());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  auto thread_task = [](TrunksDBusProxy* proxy, base::WaitableEvent* event) {
    auto callback = [](const std::string& response) {
      EXPECT_EQ(CreateErrorResponse(TRUNKS_RC_IPC_ERROR), response);
    };
    proxy->SendCommand("command", base::Bind(callback));
    event->Signal();
  };
  other_thread.task_runner()->PostTask(
      FROM_HERE, base::Bind(thread_task, &proxy_, &event));
  EXPECT_TRUE(event.TimedWait(base::TimeDelta::FromMilliseconds(100)));
  EXPECT_EQ("", last_command());
}

TEST_F(TrunksDBusProxyTest, SendCommandAndWaitFailureWrongThread) {
  // Attempting to send from a wrong thread should return TRUNKS_RC_IPC_ERROR
  // without sending the command.
  EXPECT_TRUE(proxy_.Init());
  set_next_response("response");
  base::Thread other_thread("trunks_dbus_proxy_test other thread");
  ASSERT_TRUE(other_thread.Start());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  auto thread_task = [](TrunksDBusProxy* proxy, base::WaitableEvent* event) {
    EXPECT_EQ(CreateErrorResponse(TRUNKS_RC_IPC_ERROR),
              proxy->SendCommandAndWait("command"));
    event->Signal();
  };
  other_thread.task_runner()->PostTask(
      FROM_HERE, base::Bind(thread_task, &proxy_, &event));
  EXPECT_TRUE(event.TimedWait(base::TimeDelta::FromMilliseconds(100)));
  EXPECT_EQ("", last_command());
}

}  // namespace trunks
