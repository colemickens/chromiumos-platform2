// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_capability_gsm.h"

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <chromeos/dbus/service_constants.h>
#include <mm/mm-modem.h>

#include "shill/cellular/cellular.h"
#include "shill/cellular/cellular_service.h"
#include "shill/cellular/mock_mobile_operator_info.h"
#include "shill/cellular/mock_modem_gsm_card_proxy.h"
#include "shill/cellular/mock_modem_gsm_network_proxy.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/cellular/mock_modem_proxy.h"
#include "shill/cellular/mock_modem_simple_proxy.h"
#include "shill/error.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_control.h"
#include "shill/mock_log.h"
#include "shill/mock_profile.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"

using base::Bind;
using base::Unretained;
using std::string;
using std::vector;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace shill {

class CellularCapabilityGsmTest : public testing::Test {
 public:
  CellularCapabilityGsmTest()
      : control_interface_(this),
        modem_info_(&control_interface_, &dispatcher_, nullptr, nullptr),
        create_card_proxy_from_factory_(false),
        proxy_(new MockModemProxy()),
        simple_proxy_(new MockModemSimpleProxy()),
        card_proxy_(new MockModemGsmCardProxy()),
        network_proxy_(new MockModemGsmNetworkProxy()),
        capability_(nullptr),
        device_adaptor_(nullptr),
        cellular_(new Cellular(&modem_info_,
                               "",
                               kAddress,
                               0,
                               Cellular::kTypeGsm,
                               "",
                               "")),
        mock_home_provider_info_(nullptr),
        mock_serving_operator_info_(nullptr) {
    modem_info_.metrics()->RegisterDevice(cellular_->interface_index(),
                                          Technology::kCellular);
  }

  ~CellularCapabilityGsmTest() override {
    cellular_->service_ = nullptr;
    capability_ = nullptr;
    device_adaptor_ = nullptr;
  }

  void SetUp() override {
    capability_ =
        static_cast<CellularCapabilityGsm*>(cellular_->capability_.get());
    device_adaptor_ =
        static_cast<DeviceMockAdaptor*>(cellular_->adaptor());
  }

  void InvokeEnable(bool enable, Error* error,
                    const ResultCallback& callback, int timeout) {
    callback.Run(Error());
  }
  void InvokeGetIMEI(Error* error, const GsmIdentifierCallback& callback,
                     int timeout) {
    callback.Run(kIMEI, Error());
  }
  void InvokeGetIMSI(Error* error, const GsmIdentifierCallback& callback,
                     int timeout) {
    callback.Run(kIMSI, Error());
  }
  void InvokeGetIMSIFails(Error* error, const GsmIdentifierCallback& callback,
                          int timeout) {
    callback.Run("", Error(Error::kOperationFailed));
  }
  void InvokeGetMSISDN(Error* error, const GsmIdentifierCallback& callback,
                       int timeout) {
    callback.Run(kMSISDN, Error());
  }
  void InvokeGetMSISDNFail(Error* error, const GsmIdentifierCallback& callback,
                           int timeout) {
    callback.Run("", Error(Error::kOperationFailed));
  }
  void InvokeGetSPN(Error* error, const GsmIdentifierCallback& callback,
                    int timeout) {
    callback.Run(kTestCarrier, Error());
  }
  void InvokeGetSPNFail(Error* error, const GsmIdentifierCallback& callback,
                        int timeout) {
    callback.Run("", Error(Error::kOperationFailed));
  }
  void InvokeGetSignalQuality(Error* error,
                              const SignalQualityCallback& callback,
                              int timeout) {
    callback.Run(kStrength, Error());
  }
  void InvokeGetRegistrationInfo(Error* error,
                                 const RegistrationInfoCallback& callback,
                                 int timeout) {
    callback.Run(MM_MODEM_GSM_NETWORK_REG_STATUS_HOME,
                 kTestNetwork, kTestCarrier, Error());
  }
  void InvokeRegister(const string& network_id,
                      Error* error,
                      const ResultCallback& callback,
                      int timeout) {
    callback.Run(Error());
  }
  void InvokeEnablePIN(const string& pin, bool enable,
                       Error* error, const ResultCallback& callback,
                       int timeout) {
    callback.Run(Error());
  }
  void InvokeSendPIN(const string& pin, Error* error,
                     const ResultCallback& callback, int timeout) {
    callback.Run(Error());
  }
  void InvokeSendPUK(const string& puk, const string& pin, Error* error,
                     const ResultCallback& callback, int timeout) {
    callback.Run(Error());
  }
  void InvokeChangePIN(const string& old_pin, const string& pin, Error* error,
                       const ResultCallback& callback, int timeout) {
    callback.Run(Error());
  }
  void InvokeGetModemStatus(Error* error,
                            const KeyValueStoreCallback& callback,
                            int timeout) {
    KeyValueStore props;
    callback.Run(props, Error());
  }
  void InvokeGetModemInfo(Error* error, const ModemInfoCallback& callback,
                          int timeout) {
    callback.Run("", "", "", Error());
  }

  void InvokeConnectFail(KeyValueStore props, Error* error,
                         const ResultCallback& callback, int timeout) {
    callback.Run(Error(Error::kOperationFailed));
  }

  MOCK_METHOD1(TestCallback, void(const Error& error));

 protected:
  static const char kAddress[];
  static const char kTestMobileProviderDBPath[];
  static const char kTestNetwork[];
  static const char kTestCarrier[];
  static const char kPIN[];
  static const char kPUK[];
  static const char kIMEI[];
  static const char kIMSI[];
  static const char kMSISDN[];
  static const int kStrength;

  class TestControl : public MockControl {
   public:
    explicit TestControl(CellularCapabilityGsmTest* test) : test_(test) {}

    std::unique_ptr<ModemProxyInterface> CreateModemProxy(
        const string& /*path*/,
        const string& /*service*/) override {
      return std::move(test_->proxy_);
    }

    std::unique_ptr<ModemSimpleProxyInterface> CreateModemSimpleProxy(
        const string& /*path*/,
        const string& /*service*/) override {
      return std::move(test_->simple_proxy_);
    }

    std::unique_ptr<ModemGsmCardProxyInterface> CreateModemGsmCardProxy(
        const string& /*path*/,
        const string& /*service*/) override {
      // TODO(benchan): This code conditionally returns a nullptr to avoid
      // CellularCapabilityGsm::InitProperties (and thus
      // CellularCapabilityGsm::GetIMSI) from being called during the
      // construction. Remove this workaround after refactoring the tests.
      if (test_->create_card_proxy_from_factory_) {
        return std::move(test_->card_proxy_);
      }
      return nullptr;
    }

    std::unique_ptr<ModemGsmNetworkProxyInterface> CreateModemGsmNetworkProxy(
        const string& /*path*/,
        const string& /*service*/) override {
      return std::move(test_->network_proxy_);
    }

   private:
    CellularCapabilityGsmTest* test_;
  };

  void SetCardProxy() {
    capability_->card_proxy_ = std::move(card_proxy_);
  }

  void SetNetworkProxy() {
    capability_->network_proxy_ = std::move(network_proxy_);
  }

  void SetAccessTechnology(uint32_t technology) {
    capability_->access_technology_ = technology;
  }

  void SetRegistrationState(uint32_t state) {
    capability_->registration_state_ = state;
  }

  void CreateService() {
    // The following constants are never directly accessed by the tests.
    const char kFriendlyServiceName[] = "default_test_service_name";
    const char kOperatorCode[] = "10010";
    const char kOperatorName[] = "default_test_operator_name";
    const char kOperatorCountry[] = "us";

    // Simulate all the side-effects of Cellular::CreateService
    auto service = new CellularService(&modem_info_, cellular_);
    service->SetFriendlyName(kFriendlyServiceName);

    Stringmap serving_operator;
    serving_operator[kOperatorCodeKey] = kOperatorCode;
    serving_operator[kOperatorNameKey] = kOperatorName;
    serving_operator[kOperatorCountryKey] = kOperatorCountry;

    service->set_serving_operator(serving_operator);
    cellular_->set_home_provider(serving_operator);
    cellular_->service_ = service;
  }

  void SetMockMobileOperatorInfoObjects() {
    CHECK(!mock_home_provider_info_);
    CHECK(!mock_serving_operator_info_);
    mock_home_provider_info_ =
        new MockMobileOperatorInfo(&dispatcher_, "HomeProvider");
    mock_serving_operator_info_ =
        new MockMobileOperatorInfo(&dispatcher_, "ServingOperator");
    cellular_->set_home_provider_info(mock_home_provider_info_);
    cellular_->set_serving_operator_info(mock_serving_operator_info_);
  }

  void SetupCommonProxiesExpectations() {
    EXPECT_CALL(*proxy_, set_state_changed_callback(_));
    EXPECT_CALL(*network_proxy_, set_signal_quality_callback(_));
    EXPECT_CALL(*network_proxy_, set_network_mode_callback(_));
    EXPECT_CALL(*network_proxy_, set_registration_info_callback(_));
  }

  void SetupCommonStartModemExpectations() {
    SetupCommonProxiesExpectations();

    EXPECT_CALL(*proxy_, Enable(_, _, _, CellularCapability::kTimeoutEnable))
        .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeEnable));
    EXPECT_CALL(*card_proxy_,
                GetIMEI(_, _, CellularCapability::kTimeoutDefault))
        .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeGetIMEI));
    EXPECT_CALL(*card_proxy_,
                GetIMSI(_, _, CellularCapability::kTimeoutDefault))
        .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeGetIMSI));
    EXPECT_CALL(*network_proxy_, AccessTechnology());
    EXPECT_CALL(*card_proxy_, EnabledFacilityLocks());
    EXPECT_CALL(*proxy_,
                GetModemInfo(_, _, CellularCapability::kTimeoutDefault))
        .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeGetModemInfo));
    EXPECT_CALL(*network_proxy_,
                GetRegistrationInfo(_, _, CellularCapability::kTimeoutDefault));
    EXPECT_CALL(*network_proxy_,
                GetSignalQuality(_, _, CellularCapability::kTimeoutDefault));
    EXPECT_CALL(*this, TestCallback(IsSuccess()));
  }

  void InitProxies() {
    AllowCreateCardProxyFromFactory();
    capability_->InitProxies();
  }

  void AllowCreateCardProxyFromFactory() {
    create_card_proxy_from_factory_ = true;
  }

  EventDispatcherForTest dispatcher_;
  TestControl control_interface_;
  MockModemInfo modem_info_;
  bool create_card_proxy_from_factory_;
  std::unique_ptr<MockModemProxy> proxy_;
  std::unique_ptr<MockModemSimpleProxy> simple_proxy_;
  std::unique_ptr<MockModemGsmCardProxy> card_proxy_;
  std::unique_ptr<MockModemGsmNetworkProxy> network_proxy_;
  CellularCapabilityGsm* capability_;  // Owned by |cellular_|.
  DeviceMockAdaptor* device_adaptor_;  // Owned by |cellular_|.
  CellularRefPtr cellular_;

  // Set when required and passed to |cellular_|. Owned by |cellular_|.
  MockMobileOperatorInfo* mock_home_provider_info_;
  MockMobileOperatorInfo* mock_serving_operator_info_;
};

const char CellularCapabilityGsmTest::kAddress[] = "1122334455";
const char CellularCapabilityGsmTest::kTestMobileProviderDBPath[] =
    "provider_db_unittest.bfd";
const char CellularCapabilityGsmTest::kTestCarrier[] = "The Cellular Carrier";
const char CellularCapabilityGsmTest::kTestNetwork[] = "310555";
const char CellularCapabilityGsmTest::kPIN[] = "9876";
const char CellularCapabilityGsmTest::kPUK[] = "8765";
const char CellularCapabilityGsmTest::kIMEI[] = "987654321098765";
const char CellularCapabilityGsmTest::kIMSI[] = "310150123456789";
const char CellularCapabilityGsmTest::kMSISDN[] = "12345678901";
const int CellularCapabilityGsmTest::kStrength = 80;

TEST_F(CellularCapabilityGsmTest, PropertyStore) {
  EXPECT_TRUE(cellular_->store().Contains(kSIMLockStatusProperty));
}

TEST_F(CellularCapabilityGsmTest, GetIMEI) {
  EXPECT_CALL(*card_proxy_, GetIMEI(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this,
                       &CellularCapabilityGsmTest::InvokeGetIMEI));
  EXPECT_CALL(*this, TestCallback(IsSuccess()));
  SetCardProxy();
  ASSERT_TRUE(cellular_->imei().empty());
  capability_->GetIMEI(Bind(&CellularCapabilityGsmTest::TestCallback,
                            Unretained(this)));
  EXPECT_EQ(kIMEI, cellular_->imei());
}

TEST_F(CellularCapabilityGsmTest, GetIMSI) {
  SetMockMobileOperatorInfoObjects();
  EXPECT_CALL(*card_proxy_, GetIMSI(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this,
                       &CellularCapabilityGsmTest::InvokeGetIMSI));
  EXPECT_CALL(*this, TestCallback(IsSuccess()));
  SetCardProxy();
  ResultCallback callback = Bind(&CellularCapabilityGsmTest::TestCallback,
                                 Unretained(this));
  EXPECT_TRUE(cellular_->imsi().empty());
  EXPECT_FALSE(cellular_->sim_present());
  EXPECT_CALL(*mock_home_provider_info_, UpdateIMSI(kIMSI));
  capability_->GetIMSI(callback);
  EXPECT_EQ(kIMSI, cellular_->imsi());
  EXPECT_TRUE(cellular_->sim_present());
}

// In this test, the call to the proxy's GetIMSI() will always indicate failure,
// which will cause the retry logic to call the proxy again a number of times.
// Eventually, the retries expire.
TEST_F(CellularCapabilityGsmTest, GetIMSIFails) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOG_INFO,
                       ::testing::EndsWith("cellular_capability_gsm.cc"),
                       ::testing::StartsWith("GetIMSI failed - ")));
  EXPECT_CALL(*card_proxy_, GetIMSI(_, _, CellularCapability::kTimeoutDefault))
      .Times(CellularCapabilityGsm::kGetIMSIRetryLimit + 2)
      .WillRepeatedly(Invoke(this,
                             &CellularCapabilityGsmTest::InvokeGetIMSIFails));
  EXPECT_CALL(*this, TestCallback(IsFailure())).Times(2);
  SetCardProxy();
  ResultCallback callback = Bind(&CellularCapabilityGsmTest::TestCallback,
                                 Unretained(this));
  EXPECT_TRUE(cellular_->imsi().empty());
  EXPECT_FALSE(cellular_->sim_present());

  capability_->sim_lock_status_.lock_type = "sim-pin";
  capability_->GetIMSI(callback);
  EXPECT_TRUE(cellular_->imsi().empty());
  EXPECT_TRUE(cellular_->sim_present());

  capability_->sim_lock_status_.lock_type.clear();
  cellular_->set_sim_present(false);
  capability_->get_imsi_retries_ = 0;
  EXPECT_EQ(CellularCapabilityGsm::kGetIMSIRetryDelayMilliseconds,
            capability_->get_imsi_retry_delay_milliseconds_);

  // Set the delay to zero to speed up the test.
  capability_->get_imsi_retry_delay_milliseconds_ = 0;
  capability_->GetIMSI(callback);
  for (int i = 0; i < CellularCapabilityGsm::kGetIMSIRetryLimit; ++i) {
    dispatcher_.DispatchPendingEvents();
  }
  EXPECT_EQ(CellularCapabilityGsm::kGetIMSIRetryLimit + 1,
            capability_->get_imsi_retries_);
  EXPECT_TRUE(cellular_->imsi().empty());
  EXPECT_FALSE(cellular_->sim_present());
}

TEST_F(CellularCapabilityGsmTest, GetMSISDN) {
  EXPECT_CALL(*card_proxy_, GetMSISDN(_, _,
                                      CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this,
                       &CellularCapabilityGsmTest::InvokeGetMSISDN));
  EXPECT_CALL(*this, TestCallback(IsSuccess()));
  SetCardProxy();
  ASSERT_TRUE(cellular_->mdn().empty());
  capability_->GetMSISDN(Bind(&CellularCapabilityGsmTest::TestCallback,
                            Unretained(this)));
  EXPECT_EQ(kMSISDN, cellular_->mdn());
}

TEST_F(CellularCapabilityGsmTest, GetSPN) {
  EXPECT_CALL(*card_proxy_, GetSPN(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this,
                       &CellularCapabilityGsmTest::InvokeGetSPN));
  EXPECT_CALL(*this, TestCallback(IsSuccess()));
  SetCardProxy();
  ASSERT_TRUE(capability_->spn_.empty());
  capability_->GetSPN(Bind(&CellularCapabilityGsmTest::TestCallback,
                            Unretained(this)));
  EXPECT_EQ(kTestCarrier, capability_->spn_);
}

TEST_F(CellularCapabilityGsmTest, GetSignalQuality) {
  EXPECT_CALL(*network_proxy_,
              GetSignalQuality(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this,
                       &CellularCapabilityGsmTest::InvokeGetSignalQuality));
  SetNetworkProxy();
  CreateService();
  EXPECT_EQ(0, cellular_->service()->strength());
  capability_->GetSignalQuality();
  EXPECT_EQ(kStrength, cellular_->service()->strength());
}

TEST_F(CellularCapabilityGsmTest, RegisterOnNetwork) {
  EXPECT_CALL(*network_proxy_, Register(kTestNetwork, _, _,
                                        CellularCapability::kTimeoutRegister))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeRegister));
  EXPECT_CALL(*this, TestCallback(IsSuccess()));
  SetNetworkProxy();
  Error error;
  capability_->RegisterOnNetwork(kTestNetwork, &error,
                                 Bind(&CellularCapabilityGsmTest::TestCallback,
                                      Unretained(this)));
  EXPECT_EQ(kTestNetwork, cellular_->selected_network());
}

TEST_F(CellularCapabilityGsmTest, IsRegistered) {
  EXPECT_FALSE(capability_->IsRegistered());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE);
  EXPECT_FALSE(capability_->IsRegistered());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_HOME);
  EXPECT_TRUE(capability_->IsRegistered());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING);
  EXPECT_FALSE(capability_->IsRegistered());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED);
  EXPECT_FALSE(capability_->IsRegistered());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN);
  EXPECT_FALSE(capability_->IsRegistered());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING);
  EXPECT_TRUE(capability_->IsRegistered());
}

TEST_F(CellularCapabilityGsmTest, GetRegistrationState) {
  ASSERT_FALSE(capability_->IsRegistered());
  EXPECT_CALL(*network_proxy_,
              GetRegistrationInfo(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this,
                       &CellularCapabilityGsmTest::InvokeGetRegistrationInfo));
  SetNetworkProxy();
  capability_->GetRegistrationState();
  EXPECT_TRUE(capability_->IsRegistered());
  EXPECT_EQ(MM_MODEM_GSM_NETWORK_REG_STATUS_HOME,
            capability_->registration_state_);
}

TEST_F(CellularCapabilityGsmTest, RequirePIN) {
  EXPECT_CALL(*card_proxy_, EnablePIN(kPIN, true, _, _,
                                      CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeEnablePIN));
  EXPECT_CALL(*this, TestCallback(IsSuccess()));
  SetCardProxy();
  Error error;
  capability_->RequirePIN(kPIN, true, &error,
                          Bind(&CellularCapabilityGsmTest::TestCallback,
                               Unretained(this)));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(CellularCapabilityGsmTest, EnterPIN) {
  EXPECT_CALL(*card_proxy_, SendPIN(kPIN, _, _,
                                    CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeSendPIN));
  EXPECT_CALL(*this, TestCallback(IsSuccess()));
  SetCardProxy();
  Error error;
  capability_->EnterPIN(kPIN, &error,
                        Bind(&CellularCapabilityGsmTest::TestCallback,
                             Unretained(this)));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(CellularCapabilityGsmTest, UnblockPIN) {
  EXPECT_CALL(*card_proxy_, SendPUK(kPUK, kPIN, _, _,
                                    CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeSendPUK));
  EXPECT_CALL(*this, TestCallback(IsSuccess()));
  SetCardProxy();
  Error error;
  capability_->UnblockPIN(kPUK, kPIN, &error,
                          Bind(&CellularCapabilityGsmTest::TestCallback,
                             Unretained(this)));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(CellularCapabilityGsmTest, ChangePIN) {
  static const char kOldPIN[] = "1111";
  EXPECT_CALL(*card_proxy_, ChangePIN(kOldPIN, kPIN, _, _,
                                    CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeChangePIN));
  EXPECT_CALL(*this, TestCallback(IsSuccess()));
  SetCardProxy();
  Error error;
  capability_->ChangePIN(kOldPIN, kPIN, &error,
                         Bind(&CellularCapabilityGsmTest::TestCallback,
                             Unretained(this)));
  EXPECT_TRUE(error.IsSuccess());
}


TEST_F(CellularCapabilityGsmTest, ParseScanResult) {
  static const char kID[] = "123";
  static const char kLongName[] = "long name";
  static const char kShortName[] = "short name";
  GsmScanResult result;
  result[CellularCapabilityGsm::kNetworkPropertyStatus] = "1";
  result[CellularCapabilityGsm::kNetworkPropertyID] = kID;
  result[CellularCapabilityGsm::kNetworkPropertyLongName] = kLongName;
  result[CellularCapabilityGsm::kNetworkPropertyShortName] = kShortName;
  result[CellularCapabilityGsm::kNetworkPropertyAccessTechnology] = "3";
  result["unknown property"] = "random value";
  Stringmap parsed = capability_->ParseScanResult(result);
  EXPECT_EQ(5, parsed.size());
  EXPECT_EQ("available", parsed[kStatusProperty]);
  EXPECT_EQ(kID, parsed[kNetworkIdProperty]);
  EXPECT_EQ(kLongName, parsed[kLongNameProperty]);
  EXPECT_EQ(kShortName, parsed[kShortNameProperty]);
  EXPECT_EQ(kNetworkTechnologyEdge, parsed[kTechnologyProperty]);
}

TEST_F(CellularCapabilityGsmTest, ParseScanResultProviderLookup) {
  static const char kID[] = "10001";
  const string kLongName = "TestNetworkLongName";
  // Replace the |MobileOperatorInfo| used by |ParseScanResult| by a mock.
  auto* mock_mobile_operator_info = new MockMobileOperatorInfo(
      &dispatcher_,
      "MockParseScanResult");
  capability_->mobile_operator_info_.reset(mock_mobile_operator_info);

  EXPECT_CALL(*mock_mobile_operator_info, UpdateMCCMNC(kID));
  EXPECT_CALL(*mock_mobile_operator_info, IsMobileNetworkOperatorKnown()).
      WillOnce(Return(true));
  EXPECT_CALL(*mock_mobile_operator_info, operator_name()).
      WillRepeatedly(ReturnRef(kLongName));
  GsmScanResult result;
  result[CellularCapabilityGsm::kNetworkPropertyID] = kID;
  Stringmap parsed = capability_->ParseScanResult(result);
  EXPECT_EQ(2, parsed.size());
  EXPECT_EQ(kID, parsed[kNetworkIdProperty]);
  EXPECT_EQ(kLongName, parsed[kLongNameProperty]);
}

TEST_F(CellularCapabilityGsmTest, SetAccessTechnology) {
  capability_->SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_GSM);
  EXPECT_EQ(MM_MODEM_GSM_ACCESS_TECH_GSM, capability_->access_technology_);
  CreateService();
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_HOME);
  capability_->SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_GPRS);
  EXPECT_EQ(MM_MODEM_GSM_ACCESS_TECH_GPRS, capability_->access_technology_);
  EXPECT_EQ(kNetworkTechnologyGprs, cellular_->service()->network_technology());
}

TEST_F(CellularCapabilityGsmTest, GetNetworkTechnologyString) {
  EXPECT_EQ("", capability_->GetNetworkTechnologyString());
  SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_GSM);
  EXPECT_EQ(kNetworkTechnologyGsm, capability_->GetNetworkTechnologyString());
  SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_GSM_COMPACT);
  EXPECT_EQ(kNetworkTechnologyGsm, capability_->GetNetworkTechnologyString());
  SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_GPRS);
  EXPECT_EQ(kNetworkTechnologyGprs, capability_->GetNetworkTechnologyString());
  SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_EDGE);
  EXPECT_EQ(kNetworkTechnologyEdge, capability_->GetNetworkTechnologyString());
  SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_UMTS);
  EXPECT_EQ(kNetworkTechnologyUmts, capability_->GetNetworkTechnologyString());
  SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_HSDPA);
  EXPECT_EQ(kNetworkTechnologyHspa, capability_->GetNetworkTechnologyString());
  SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_HSUPA);
  EXPECT_EQ(kNetworkTechnologyHspa, capability_->GetNetworkTechnologyString());
  SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_HSPA);
  EXPECT_EQ(kNetworkTechnologyHspa, capability_->GetNetworkTechnologyString());
  SetAccessTechnology(MM_MODEM_GSM_ACCESS_TECH_HSPA_PLUS);
  EXPECT_EQ(kNetworkTechnologyHspaPlus,
            capability_->GetNetworkTechnologyString());
}

TEST_F(CellularCapabilityGsmTest, GetRoamingStateString) {
  EXPECT_EQ(kRoamingStateUnknown, capability_->GetRoamingStateString());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_HOME);
  EXPECT_EQ(kRoamingStateHome, capability_->GetRoamingStateString());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING);
  EXPECT_EQ(kRoamingStateRoaming, capability_->GetRoamingStateString());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING);
  EXPECT_EQ(kRoamingStateUnknown, capability_->GetRoamingStateString());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED);
  EXPECT_EQ(kRoamingStateUnknown, capability_->GetRoamingStateString());
  SetRegistrationState(MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE);
  EXPECT_EQ(kRoamingStateUnknown, capability_->GetRoamingStateString());
}

TEST_F(CellularCapabilityGsmTest, OnPropertiesChanged) {
  EXPECT_EQ(MM_MODEM_GSM_ACCESS_TECH_UNKNOWN, capability_->access_technology_);
  EXPECT_FALSE(capability_->sim_lock_status_.enabled);
  EXPECT_EQ("", capability_->sim_lock_status_.lock_type);
  EXPECT_EQ(0, capability_->sim_lock_status_.retries_left);
  KeyValueStore props;
  static const char kLockType[] = "sim-pin";
  const int kRetries = 3;
  props.SetUint(CellularCapabilityGsm::kPropertyAccessTechnology,
                MM_MODEM_GSM_ACCESS_TECH_EDGE);
  props.SetUint(CellularCapabilityGsm::kPropertyEnabledFacilityLocks,
                MM_MODEM_GSM_FACILITY_SIM);
  props.SetString(CellularCapabilityGsm::kPropertyUnlockRequired, kLockType);
  props.SetUint(CellularCapabilityGsm::kPropertyUnlockRetries, kRetries);
  // Call with the 'wrong' interface and nothing should change.
  capability_->OnPropertiesChanged(MM_MODEM_GSM_INTERFACE, props,
                                   vector<string>());
  EXPECT_EQ(MM_MODEM_GSM_ACCESS_TECH_UNKNOWN, capability_->access_technology_);
  EXPECT_FALSE(capability_->sim_lock_status_.enabled);
  EXPECT_EQ("", capability_->sim_lock_status_.lock_type);
  EXPECT_EQ(0, capability_->sim_lock_status_.retries_left);

  // Call with the MM_MODEM_GSM_NETWORK_INTERFACE interface and expect a change
  // to the enabled state of the SIM lock.
  KeyValueStore lock_status;
  lock_status.SetBool(kSIMLockEnabledProperty, true);
  lock_status.SetString(kSIMLockTypeProperty, "");
  lock_status.SetInt(kSIMLockRetriesLeftProperty, 0);

  EXPECT_CALL(*device_adaptor_, EmitKeyValueStoreChanged(
      kSIMLockStatusProperty,
      KeyValueStoreEq(lock_status)));

  capability_->OnPropertiesChanged(MM_MODEM_GSM_NETWORK_INTERFACE, props,
                                   vector<string>());
  EXPECT_EQ(MM_MODEM_GSM_ACCESS_TECH_EDGE, capability_->access_technology_);
  capability_->OnPropertiesChanged(MM_MODEM_GSM_CARD_INTERFACE, props,
                                   vector<string>());
  EXPECT_TRUE(capability_->sim_lock_status_.enabled);
  EXPECT_TRUE(capability_->sim_lock_status_.lock_type.empty());
  EXPECT_EQ(0, capability_->sim_lock_status_.retries_left);

  // Some properties are sent on the MM_MODEM_INTERFACE.
  capability_->sim_lock_status_.enabled = false;
  capability_->sim_lock_status_.lock_type = "";
  capability_->sim_lock_status_.retries_left = 0;
  KeyValueStore lock_status2;
  lock_status2.SetBool(kSIMLockEnabledProperty, false);
  lock_status2.SetString(kSIMLockTypeProperty, kLockType);
  lock_status2.SetInt(kSIMLockRetriesLeftProperty, kRetries);
  EXPECT_CALL(*device_adaptor_,
              EmitKeyValueStoreChanged(kSIMLockStatusProperty,
                                       KeyValueStoreEq(lock_status2)));
  capability_->OnPropertiesChanged(MM_MODEM_INTERFACE, props,
                                   vector<string>());
  EXPECT_FALSE(capability_->sim_lock_status_.enabled);
  EXPECT_EQ(kLockType, capability_->sim_lock_status_.lock_type);
  EXPECT_EQ(kRetries, capability_->sim_lock_status_.retries_left);
}

TEST_F(CellularCapabilityGsmTest, StartModemSuccess) {
  SetupCommonStartModemExpectations();
  EXPECT_CALL(*card_proxy_,
              GetSPN(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeGetSPN));
  EXPECT_CALL(*card_proxy_,
              GetMSISDN(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeGetMSISDN));
  AllowCreateCardProxyFromFactory();

  Error error;
  capability_->StartModem(
      &error, Bind(&CellularCapabilityGsmTest::TestCallback, Unretained(this)));
  dispatcher_.DispatchPendingEvents();
}

TEST_F(CellularCapabilityGsmTest, StartModemGetSPNFail) {
  SetupCommonStartModemExpectations();
  EXPECT_CALL(*card_proxy_,
              GetSPN(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeGetSPNFail));
  EXPECT_CALL(*card_proxy_,
              GetMSISDN(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeGetMSISDN));
  AllowCreateCardProxyFromFactory();

  Error error;
  capability_->StartModem(
      &error, Bind(&CellularCapabilityGsmTest::TestCallback, Unretained(this)));
  dispatcher_.DispatchPendingEvents();
}

TEST_F(CellularCapabilityGsmTest, StartModemGetMSISDNFail) {
  SetupCommonStartModemExpectations();
  EXPECT_CALL(*card_proxy_,
              GetSPN(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeGetSPN));
  EXPECT_CALL(*card_proxy_,
              GetMSISDN(_, _, CellularCapability::kTimeoutDefault))
      .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeGetMSISDNFail));
  AllowCreateCardProxyFromFactory();

  Error error;
  capability_->StartModem(
      &error, Bind(&CellularCapabilityGsmTest::TestCallback, Unretained(this)));
  dispatcher_.DispatchPendingEvents();
}

TEST_F(CellularCapabilityGsmTest, ConnectFailureNoService) {
  // Make sure we don't crash if the connect failed and there is no
  // CellularService object.  This can happen if the modem is enabled and
  // then quickly disabled.
  SetupCommonProxiesExpectations();
  EXPECT_CALL(*simple_proxy_,
              Connect(_, _, _, CellularCapabilityGsm::kTimeoutConnect))
       .WillOnce(Invoke(this, &CellularCapabilityGsmTest::InvokeConnectFail));
  EXPECT_CALL(*this, TestCallback(IsFailure()));
  InitProxies();
  EXPECT_FALSE(capability_->cellular()->service());
  Error error;
  KeyValueStore props;
  capability_->Connect(props, &error,
                       Bind(&CellularCapabilityGsmTest::TestCallback,
                            Unretained(this)));
}

}  // namespace shill
