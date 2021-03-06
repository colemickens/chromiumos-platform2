// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/http_request.h"

#include <netinet/in.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <brillo/mime_utils.h>
#include <brillo/streams/mock_stream.h>
#include <curl/curl.h>
#include <gtest/gtest.h>

#include "shill/http_url.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_dns_client.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/net/ip_address.h"
#include "shill/net/mock_sockets.h"

using base::Bind;
using base::Callback;
using base::IntToString;
using base::StringPrintf;
using base::Unretained;
using std::string;
using std::vector;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnNew;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::Test;
using ::testing::Unused;
using ::testing::WithArg;

namespace shill {

namespace {
const char kTextSiteName[] = "www.chromium.org";
const char kTextURL[] = "http://www.chromium.org/path/to/resource";
const char kNumericURL[] = "http://10.1.1.1";
const char kPath[] = "/path/to/resource";
const char kInterfaceName[] = "int0";
const char kDNSServer0[] = "8.8.8.8";
const char kDNSServer1[] = "8.8.4.4";
const char* const kDNSServers[] = {kDNSServer0, kDNSServer1};
const char kServerAddress[] = "10.1.1.1";
}  // namespace

MATCHER_P(IsIPAddress, address, "") {
  IPAddress ip_address(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(ip_address.SetAddressFromString(address));
  return ip_address.Equals(arg);
}

MATCHER_P(ByteStringMatches, byte_string, "") {
  return byte_string.Equals(arg);
}

MATCHER_P(CallbackEq, callback, "") {
  return arg.Equals(callback);
}

class HttpRequestTest : public Test {
 public:
  HttpRequestTest()
      : interface_name_(kInterfaceName),
        dns_servers_(kDNSServers, kDNSServers + 2),
        dns_client_(new StrictMock<MockDnsClient>()),
        transport_(std::make_shared<brillo::http::MockTransport>()),
        brillo_connection_(
            std::make_shared<brillo::http::MockConnection>(transport_)),
        device_info_(
            new NiceMock<MockDeviceInfo>(&control_, nullptr, nullptr, nullptr)),
        connection_(new StrictMock<MockConnection>(device_info_.get())) {}

 protected:
  class CallbackTarget {
   public:
    CallbackTarget()
        : request_success_callback_(Bind(
              &CallbackTarget::RequestSuccessCallTarget, Unretained(this))),
          request_error_callback_(Bind(&CallbackTarget::RequestErrorCallTarget,
                                       Unretained(this))) {}

    MOCK_METHOD1(RequestSuccessCallTarget,
                 void(std::shared_ptr<brillo::http::Response>));
    MOCK_METHOD1(RequestErrorCallTarget, void(HttpRequest::Result));

    const Callback<void(std::shared_ptr<brillo::http::Response>)>&
    request_success_callback() const {
      return request_success_callback_;
    }

    const Callback<void(HttpRequest::Result)>& request_error_callback() const {
      return request_error_callback_;
    }

   private:
    Callback<void(std::shared_ptr<brillo::http::Response>)>
        request_success_callback_;
    Callback<void(HttpRequest::Result)> request_error_callback_;
  };

  void SetUp() override {
    EXPECT_CALL(*connection_, IsIPv6()).WillRepeatedly(Return(false));
    EXPECT_CALL(*connection_, interface_name())
        .WillRepeatedly(ReturnRef(interface_name_));
    EXPECT_CALL(*connection_, dns_servers())
        .WillRepeatedly(ReturnRef(dns_servers_));

    request_.reset(new HttpRequest(connection_, &dispatcher_));
    // Passes ownership.
    request_->dns_client_.reset(dns_client_);
    request_->transport_ = transport_;
  }
  void TearDown() override {
    if (request_->is_running_) {
      ExpectStop();

      // Subtle: Make sure the finalization of the request happens while our
      // expectations are still active.
      request_.reset();
    }
    testing::Mock::VerifyAndClearExpectations(brillo_connection_.get());
    brillo_connection_.reset();
    testing::Mock::VerifyAndClearExpectations(transport_.get());
    transport_.reset();
  }
  HttpRequest* request() { return request_.get(); }

  // Expectations
  void ExpectReset() {
    EXPECT_EQ(connection_.get(), request_->connection_.get());
    EXPECT_TRUE(request_->request_error_callback_.is_null());
    EXPECT_TRUE(request_->request_success_callback_.is_null());
    EXPECT_FALSE(request_->dns_client_callback_.is_null());
    EXPECT_EQ(dns_client_, request_->dns_client_.get());
    EXPECT_TRUE(request_->server_hostname_.empty());
    EXPECT_FALSE(request_->is_running_);
  }
  void ExpectStop() {
    EXPECT_CALL(*dns_client_, Stop())
        .Times(AtLeast(1));
    EXPECT_CALL(*connection_, ReleaseRouting());
  }
  void ExpectDNSRequest(const string& host, bool return_value) {
    EXPECT_CALL(*dns_client_, Start(StrEq(host), _))
        .WillOnce(Return(return_value));
  }
  void ExpectRouteRequest() { EXPECT_CALL(*connection_, RequestRouting()); }
  void ExpectRouteRelease() { EXPECT_CALL(*connection_, ReleaseRouting()); }
  void ExpectRequestErrorCallback(HttpRequest::Result result) {
    EXPECT_CALL(target_, RequestErrorCallTarget(result));
  }
  void InvokeResultVerify(std::shared_ptr<brillo::http::Response> response) {
    EXPECT_CALL(*brillo_connection_, GetResponseStatusCode())
        .WillOnce(Return(brillo::http::status_code::Partial));
    EXPECT_EQ(brillo::http::status_code::Partial, response->GetStatusCode());

    EXPECT_CALL(*brillo_connection_, GetResponseStatusText())
        .WillOnce(Return("Partial completion"));
    EXPECT_EQ("Partial completion", response->GetStatusText());

    EXPECT_CALL(*brillo_connection_,
                GetResponseHeader(brillo::http::response_header::kContentType))
        .WillOnce(Return(brillo::mime::text::kHtml));
    EXPECT_EQ(brillo::mime::text::kHtml, response->GetContentType());

    EXPECT_EQ(expected_response_, response->ExtractDataAsString());
  }
  void ExpectRequestSuccessCallback(const string& resp_data) {
    expected_response_ = resp_data;
    EXPECT_CALL(target_, RequestSuccessCallTarget(_))
        .WillOnce(Invoke(this, &HttpRequestTest::InvokeResultVerify));
  }
  void GetDNSResultFailure(const string& error_msg) {
    Error error(Error::kOperationFailed, error_msg);
    IPAddress address(IPAddress::kFamilyUnknown);
    request_->GetDNSResult(error, address);
  }
  void GetDNSResultSuccess(const IPAddress& address) {
    Error error;
    request_->GetDNSResult(error, address);
  }
  HttpRequest::Result StartRequest(const string& url) {
    HttpUrl http_url;
    EXPECT_TRUE(http_url.ParseFromString(url));
    return request_->Start(http_url, target_.request_success_callback(),
                           target_.request_error_callback());
  }
  void ExpectCreateConnection(const string& url) {
    HttpUrl http_url;
    EXPECT_TRUE(http_url.ParseFromString(url));
    string url_string = StringPrintf("%s:%d%s", http_url.host().c_str(),
                                     http_url.port(), http_url.path().c_str());
    EXPECT_CALL(*transport_,
                CreateConnection(url_string, brillo::http::request_type::kGet,
                                 _, "", "", _))
        .WillOnce(Return(brillo_connection_));
  }
  void FinishRequestAsyncSuccess(
      const brillo::http::SuccessCallback& success_callback) {
    auto read_data = [this](void* buffer, Unused, size_t* read,
                            Unused) -> bool {
      memcpy(buffer, resp_data_.data(), resp_data_.size());
      *read = resp_data_.size();
      return true;
    };

    auto mock_stream = std::make_unique<brillo::MockStream>();
    EXPECT_CALL(*mock_stream, ReadBlocking(_, _, _, _))
        .WillOnce(Invoke(read_data))
        .WillOnce(DoAll(SetArgPointee<2>(0), Return(true)));

    EXPECT_CALL(*brillo_connection_, MockExtractDataStream(_))
        .WillOnce(Return(mock_stream.release()));
    auto resp = std::make_unique<brillo::http::Response>(brillo_connection_);
    // request_id_ has not yet been set. Pass -1 to match default value.
    success_callback.Run(-1, std::move(resp));
  }
  void FinishRequestAsyncFail(
      const brillo::http::ErrorCallback& error_callback) {
    brillo::ErrorPtr error;
    brillo::Error::AddTo(&error, FROM_HERE, "curl_easy_error",
                         IntToString(CURLE_COULDNT_CONNECT), "");
    // request_id_ has not yet been set. Pass -1 to match default value.
    error_callback.Run(-1, error.get());
  }
  void ExpectFinishRequestAsyncSuccess(const string& resp_data) {
    resp_data_ = resp_data;
    EXPECT_CALL(*brillo_connection_, FinishRequestAsync(_, _))
        .WillOnce(DoAll(WithArg<0>(Invoke(
                            this, &HttpRequestTest::FinishRequestAsyncSuccess)),
                        Return(0)));
  }
  void ExpectFinishRequestAsyncFail() {
    EXPECT_CALL(*brillo_connection_, FinishRequestAsync(_, _))
        .WillOnce(DoAll(
            WithArg<1>(Invoke(this, &HttpRequestTest::FinishRequestAsyncFail)),
            Return(0)));
  }

 private:
  const string interface_name_;
  vector<string> dns_servers_;
  // Owned by the HttpRequest, but tracked here for EXPECT().
  StrictMock<MockDnsClient>* dns_client_;
  std::shared_ptr<brillo::http::MockTransport> transport_;
  std::shared_ptr<brillo::http::MockConnection> brillo_connection_;
  StrictMock<MockEventDispatcher> dispatcher_;
  MockControl control_;
  std::unique_ptr<MockDeviceInfo> device_info_;
  scoped_refptr<MockConnection> connection_;
  std::unique_ptr<HttpRequest> request_;
  StrictMock<CallbackTarget> target_;
  string expected_response_;
  string resp_data_;
};

TEST_F(HttpRequestTest, Constructor) {
  ExpectReset();
}

TEST_F(HttpRequestTest, NumericRequestSuccess) {
  ExpectRouteRequest();

  const string resp{"Sample response."};
  ExpectRequestSuccessCallback(resp);

  ExpectCreateConnection(kNumericURL);
  ExpectFinishRequestAsyncSuccess(resp);

  ExpectStop();
  EXPECT_EQ(HttpRequest::kResultInProgress, StartRequest(kNumericURL));
  ExpectReset();
}

TEST_F(HttpRequestTest, RequestFail) {
  ExpectRouteRequest();

  ExpectRequestErrorCallback(HttpRequest::kResultConnectionFailure);

  ExpectCreateConnection(kNumericURL);
  ExpectFinishRequestAsyncFail();

  ExpectStop();
  EXPECT_EQ(HttpRequest::kResultInProgress, StartRequest(kNumericURL));
  ExpectReset();
}

TEST_F(HttpRequestTest, TextRequestSuccess) {
  ExpectRouteRequest();
  ExpectDNSRequest(kTextSiteName, true);

  const string resp{"Sample response."};
  ExpectRequestSuccessCallback(resp);

  ExpectCreateConnection(StringPrintf("%s%s", kNumericURL, kPath));
  ExpectFinishRequestAsyncSuccess(resp);

  ExpectStop();
  EXPECT_EQ(HttpRequest::kResultInProgress, StartRequest(kTextURL));
  IPAddress addr(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(addr.SetAddressFromString(kServerAddress));
  GetDNSResultSuccess(addr);
  ExpectReset();
}

TEST_F(HttpRequestTest, FailDNSStart) {
  ExpectRouteRequest();
  ExpectDNSRequest(kTextSiteName, false);
  ExpectStop();
  EXPECT_EQ(HttpRequest::kResultDNSFailure, StartRequest(kTextURL));
  ExpectReset();
}

TEST_F(HttpRequestTest, FailDNSFailure) {
  ExpectRouteRequest();
  ExpectDNSRequest(kTextSiteName, true);
  EXPECT_EQ(HttpRequest::kResultInProgress, StartRequest(kTextURL));
  ExpectRequestErrorCallback(HttpRequest::kResultDNSFailure);
  ExpectStop();
  GetDNSResultFailure(DnsClient::kErrorNoData);
  ExpectReset();
}

TEST_F(HttpRequestTest, FailDNSTimeout) {
  ExpectRouteRequest();
  ExpectDNSRequest(kTextSiteName, true);
  EXPECT_EQ(HttpRequest::kResultInProgress, StartRequest(kTextURL));
  ExpectRequestErrorCallback(HttpRequest::kResultDNSTimeout);
  ExpectStop();
  const string error(DnsClient::kErrorTimedOut);
  GetDNSResultFailure(error);
  ExpectReset();
}

}  // namespace shill
