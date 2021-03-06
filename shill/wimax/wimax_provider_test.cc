// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wimax/wimax_provider.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/dhcp/mock_dhcp_config.h"
#include "shill/dhcp/mock_dhcp_provider.h"
#include "shill/eap_credentials.h"
#include "shill/fake_store.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/nice_mock_control.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"
#include "shill/wimax/mock_wimax.h"
#include "shill/wimax/mock_wimax_device_proxy.h"
#include "shill/wimax/mock_wimax_manager_proxy.h"
#include "shill/wimax/mock_wimax_network_proxy.h"
#include "shill/wimax/mock_wimax_service.h"
#include "shill/wimax/wimax_service.h"

using std::string;
using std::vector;
using testing::ByMove;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnNull;
using testing::SaveArg;
using testing::StartsWith;
using testing::_;

namespace shill {

namespace {

string GetTestLinkName(int index) {
  return base::StringPrintf("wm%d", index);
}

string GetTestPath(int index) {
  return wimax_manager::kDeviceObjectPathPrefix + GetTestLinkName(index);
}

string GetTestNetworkName(uint32_t identifier) {
  return base::StringPrintf("WiMAX %08x", identifier);
}

string GetTestNetworkPath(uint32_t identifier) {
  return base::StringPrintf("%s%08x",
                            wimax_manager::kNetworkObjectPathPrefix,
                            identifier);
}

class MockWiMaxNetworkProxyFactory {
 public:
  explicit MockWiMaxNetworkProxyFactory(uint32_t identifier)
      : identifier_(identifier) {}

  std::unique_ptr<MockWiMaxNetworkProxy> CreateProxy() {
    auto proxy = std::make_unique<MockWiMaxNetworkProxy>();
    ON_CALL(*proxy, Name(_))
        .WillByDefault(Return(GetTestNetworkName(identifier_)));
    ON_CALL(*proxy, Identifier(_)).WillByDefault(Return(identifier_));
    return proxy;
  }

 private:
  const uint32_t identifier_;

  DISALLOW_COPY_AND_ASSIGN(MockWiMaxNetworkProxyFactory);
};

}  // namespace

class WiMaxProviderTest : public testing::Test {
 public:
  WiMaxProviderTest()
      : metrics_(&dispatcher_),
        manager_(&control_, &dispatcher_, &metrics_),
        device_info_(&control_, &dispatcher_, &metrics_, &manager_),
        provider_(&control_, &dispatcher_, &metrics_, &manager_) {}

  virtual ~WiMaxProviderTest() {}

 protected:
  string GetServiceFriendlyName(const ServiceRefPtr& service) {
    return service->friendly_name();
  }

  NiceMockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  MockDeviceInfo device_info_;
  WiMaxProvider provider_;
};

TEST_F(WiMaxProviderTest, StartStop) {
  base::Closure service_appeared_callback;

  auto wimax_manager_proxy = std::make_unique<MockWiMaxManagerProxy>();
  EXPECT_CALL(*wimax_manager_proxy, set_devices_changed_callback(_)).Times(1);
  EXPECT_CALL(*wimax_manager_proxy, Devices(_))
      .WillOnce(Return(RpcIdentifiers()));
  EXPECT_CALL(control_, CreateWiMaxManagerProxy(_, _))
      .WillOnce(DoAll(SaveArg<0>(&service_appeared_callback),
                      Return(ByMove(std::move(wimax_manager_proxy)))));

  EXPECT_FALSE(provider_.wimax_manager_proxy_.get());
  provider_.Start();
  EXPECT_TRUE(provider_.wimax_manager_proxy_.get());

  service_appeared_callback.Run();

  provider_.pending_devices_[GetTestLinkName(2)] = GetTestPath(2);
  provider_.Stop();
  EXPECT_FALSE(provider_.wimax_manager_proxy_.get());
  EXPECT_TRUE(provider_.pending_devices_.empty());
}

TEST_F(WiMaxProviderTest, ConnectDisconnectWiMaxManager) {
  MockWiMaxManagerProxy* wimax_manager_proxy = new MockWiMaxManagerProxy();
  provider_.wimax_manager_proxy_.reset(wimax_manager_proxy);

  EXPECT_CALL(manager_, device_info()).WillRepeatedly(Return(&device_info_));
  EXPECT_CALL(manager_, wimax_provider()).WillRepeatedly(Return(&provider_));

  // Set up a live device and a pending device.
  const int device_index = 1;
  const int pending_device_index = 2;
  const std::string device_link = GetTestLinkName(device_index);
  const std::string pending_device_link = GetTestLinkName(pending_device_index);

  EXPECT_CALL(device_info_, GetIndex(device_link))
      .WillRepeatedly(Return(device_index));
  EXPECT_CALL(device_info_, GetIndex(pending_device_link))
      .WillRepeatedly(Return(-1));
  EXPECT_CALL(device_info_, GetMACAddress(device_index, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(device_info_, RegisterDevice(_)).Times(2);

  RpcIdentifiers devices = {GetTestPath(device_index),
                            GetTestPath(pending_device_index)};
  EXPECT_CALL(*wimax_manager_proxy, Devices(_)).WillRepeatedly(Return(devices));

  // After connecting to WiMAX manager, WiMaxProvider should see both the live
  // and pending device.
  provider_.ConnectToWiMaxManager();
  EXPECT_EQ(1, provider_.devices_.count(device_link));
  EXPECT_EQ(1, provider_.pending_devices_.count(pending_device_link));

  // Enable the live device.
  auto device_proxy = std::make_unique<MockWiMaxDeviceProxy>();
  EXPECT_CALL(*device_proxy, Enable(_, _, _))
      .WillOnce(SetErrorTypeInArgument<0>(Error::kSuccess));
  EXPECT_CALL(*device_proxy, Connect(_, _, _, _, _))
      .WillOnce(SetErrorTypeInArgument<2>(Error::kSuccess));
  EXPECT_CALL(control_, CreateWiMaxDeviceProxy(_))
      .WillOnce(Return(ByMove(std::move(device_proxy))));

  WiMaxRefPtr device = provider_.devices_[device_link];
  ASSERT_NE(nullptr, device);
  Error error;
  device->Start(&error, EnabledStateChangedCallback());
  EXPECT_TRUE(error.IsSuccess());

  // Set up two services.
  const uint32_t network0_id = 0x11223344;
  const uint32_t network1_id = 0xaabbccdd;
  MockWiMaxNetworkProxyFactory network0_proxy_factory(network0_id);
  MockWiMaxNetworkProxyFactory network1_proxy_factory(network1_id);
  ON_CALL(control_, CreateWiMaxNetworkProxy(GetTestNetworkPath(network0_id)))
      .WillByDefault(InvokeWithoutArgs(
          &network0_proxy_factory, &MockWiMaxNetworkProxyFactory::CreateProxy));
  ON_CALL(control_, CreateWiMaxNetworkProxy(GetTestNetworkPath(network1_id)))
      .WillByDefault(InvokeWithoutArgs(
          &network1_proxy_factory, &MockWiMaxNetworkProxyFactory::CreateProxy));
  RpcIdentifiers live_networks = {GetTestNetworkPath(network0_id),
                                  GetTestNetworkPath(network1_id)};
  device->OnNetworksChanged(live_networks);
  ASSERT_EQ(live_networks.size(), provider_.services_.size());

  // Connects to one service.
  scoped_refptr<MockDHCPConfig> dhcp_config(
      new MockDHCPConfig(&control_, device_link));
  EXPECT_CALL(*dhcp_config, RequestIP()).WillOnce(Return(true));
  MockDHCPProvider dhcp_provider;
  EXPECT_CALL(dhcp_provider, CreateIPv4Config(device_link, _, false, _))
      .WillOnce(Return(dhcp_config));
  device->set_dhcp_provider(&dhcp_provider);

  WiMaxServiceRefPtr service = provider_.services_.begin()->second;
  service->SetConnectable(true);
  service->Connect(&error, "in test");
  EXPECT_TRUE(error.IsSuccess());
  device->OnStatusChanged(wimax_manager::kDeviceStatusConnected);

  // Make sure this test doesn't hold an extra reference of the device object
  // or the service object, which prevents either object from being destructed.
  device = nullptr;
  service = nullptr;

  // After disconnecting from WiMAX manager, WiMaxProvider shouldn't reference
  // to any device or service objects.
  provider_.DisconnectFromWiMaxManager();
  EXPECT_TRUE(provider_.devices_.empty());
  EXPECT_TRUE(provider_.pending_devices_.empty());
  EXPECT_TRUE(provider_.services_.empty());

  // After reconnecting to WiMAX manager, WiMaxProvider should see both the
  // live and pending device.
  provider_.ConnectToWiMaxManager();
  EXPECT_EQ(1, provider_.devices_.count(device_link));
  EXPECT_EQ(1, provider_.pending_devices_.count(pending_device_link));
  EXPECT_TRUE(provider_.services_.empty());
}

TEST_F(WiMaxProviderTest, OnDevicesChanged) {
  EXPECT_CALL(manager_, device_info()).WillRepeatedly(Return(&device_info_));

  provider_.pending_devices_[GetTestLinkName(1)] = GetTestPath(1);
  RpcIdentifiers live_devices;
  live_devices.push_back(GetTestPath(2));
  live_devices.push_back(GetTestPath(3));
  EXPECT_CALL(device_info_, GetIndex(GetTestLinkName(2))).WillOnce(Return(-1));
  EXPECT_CALL(device_info_, GetIndex(GetTestLinkName(3))).WillOnce(Return(-1));
  provider_.OnDevicesChanged(live_devices);
  ASSERT_EQ(2, provider_.pending_devices_.size());
  EXPECT_EQ(GetTestPath(2), provider_.pending_devices_[GetTestLinkName(2)]);
  EXPECT_EQ(GetTestPath(3), provider_.pending_devices_[GetTestLinkName(3)]);
}

TEST_F(WiMaxProviderTest, OnDeviceInfoAvailable) {
  EXPECT_CALL(manager_, device_info()).WillRepeatedly(Return(&device_info_));

  provider_.pending_devices_[GetTestLinkName(1)] = GetTestPath(1);
  EXPECT_CALL(device_info_, GetIndex(GetTestLinkName(1))).WillOnce(Return(1));
  EXPECT_CALL(device_info_, GetMACAddress(1, _)).WillOnce(Return(true));
  EXPECT_CALL(device_info_, RegisterDevice(_));
  provider_.OnDeviceInfoAvailable(GetTestLinkName(1));
  EXPECT_TRUE(provider_.pending_devices_.empty());
  ASSERT_EQ(1, provider_.devices_.size());
  ASSERT_TRUE(base::ContainsKey(provider_.devices_, GetTestLinkName(1)));
  EXPECT_EQ(GetTestLinkName(1),
            provider_.devices_[GetTestLinkName(1)]->link_name());
}

TEST_F(WiMaxProviderTest, CreateDevice) {
  EXPECT_CALL(manager_, device_info()).WillRepeatedly(Return(&device_info_));

  EXPECT_CALL(device_info_, GetIndex(GetTestLinkName(1))).WillOnce(Return(-1));
  provider_.CreateDevice(GetTestLinkName(1), GetTestPath(1));
  EXPECT_TRUE(provider_.devices_.empty());
  ASSERT_EQ(1, provider_.pending_devices_.size());
  EXPECT_EQ(GetTestPath(1), provider_.pending_devices_[GetTestLinkName(1)]);

  EXPECT_CALL(device_info_, GetIndex(GetTestLinkName(1))).WillOnce(Return(1));
  EXPECT_CALL(device_info_, GetMACAddress(1, _)).WillOnce(Return(true));
  EXPECT_CALL(device_info_, RegisterDevice(_));
  provider_.CreateDevice(GetTestLinkName(1), GetTestPath(1));
  EXPECT_TRUE(provider_.pending_devices_.empty());
  ASSERT_EQ(1, provider_.devices_.size());
  ASSERT_TRUE(base::ContainsKey(provider_.devices_, GetTestLinkName(1)));
  EXPECT_EQ(GetTestLinkName(1),
            provider_.devices_[GetTestLinkName(1)]->link_name());

  WiMax* device = provider_.devices_[GetTestLinkName(1)].get();
  provider_.CreateDevice(GetTestLinkName(1), GetTestPath(1));
  EXPECT_EQ(device, provider_.devices_[GetTestLinkName(1)].get());
}

TEST_F(WiMaxProviderTest, DestroyDeadDevices) {
  for (int i = 0; i < 4; i++) {
    scoped_refptr<MockWiMax> device(
        new MockWiMax(&control_, nullptr, &metrics_, &manager_,
                      GetTestLinkName(i), "", i, GetTestPath(i)));
    EXPECT_CALL(*device, OnDeviceVanished()).Times((i == 0 || i == 3) ? 0 : 1);
    provider_.devices_[GetTestLinkName(i)] = device;
  }
  for (int i = 4; i < 8; i++) {
    provider_.pending_devices_[GetTestLinkName(i)] = GetTestPath(i);
  }
  RpcIdentifiers live_devices;
  live_devices.push_back(GetTestPath(0));
  live_devices.push_back(GetTestPath(3));
  live_devices.push_back(GetTestPath(4));
  live_devices.push_back(GetTestPath(7));
  live_devices.push_back(GetTestPath(123));
  EXPECT_CALL(manager_, device_info())
      .Times(2)
      .WillRepeatedly(Return(&device_info_));
  EXPECT_CALL(device_info_, DeregisterDevice(_)).Times(2);
  provider_.DestroyDeadDevices(live_devices);
  ASSERT_EQ(2, provider_.devices_.size());
  EXPECT_TRUE(base::ContainsKey(provider_.devices_, GetTestLinkName(0)));
  EXPECT_TRUE(base::ContainsKey(provider_.devices_, GetTestLinkName(3)));
  EXPECT_EQ(2, provider_.pending_devices_.size());
  EXPECT_TRUE(
      base::ContainsKey(provider_.pending_devices_, GetTestLinkName(4)));
  EXPECT_TRUE(
      base::ContainsKey(provider_.pending_devices_, GetTestLinkName(7)));
}

TEST_F(WiMaxProviderTest, GetLinkName) {
  EXPECT_EQ("", provider_.GetLinkName("/random/path"));
  EXPECT_EQ(GetTestLinkName(1), provider_.GetLinkName(GetTestPath(1)));
}

TEST_F(WiMaxProviderTest, RetrieveNetworkInfo) {
  static const char kName[] = "Default Network";
  const uint32_t kIdentifier = 0xabcdef;
  static const char kNetworkId[] = "00abcdef";
  string network_path = GetTestNetworkPath(kIdentifier);

  auto network_proxy = std::make_unique<MockWiMaxNetworkProxy>();
  EXPECT_CALL(*network_proxy, Name(_)).WillOnce(Return(kName));
  EXPECT_CALL(*network_proxy, Identifier(_)).WillOnce(Return(kIdentifier));
  EXPECT_CALL(control_, CreateWiMaxNetworkProxy(network_path))
      .WillOnce(Return(ByMove(std::move(network_proxy))));

  provider_.RetrieveNetworkInfo(network_path);
  EXPECT_EQ(1, provider_.networks_.size());
  EXPECT_TRUE(base::ContainsKey(provider_.networks_, network_path));
  EXPECT_EQ(kName, provider_.networks_[network_path].name);
  EXPECT_EQ(kNetworkId, provider_.networks_[network_path].id);
  provider_.RetrieveNetworkInfo(network_path);
  EXPECT_EQ(1, provider_.networks_.size());
}

TEST_F(WiMaxProviderTest, FindService) {
  EXPECT_FALSE(provider_.FindService("some_storage_id"));
  scoped_refptr<MockWiMaxService> service(
      new MockWiMaxService(&control_, nullptr, &metrics_, &manager_));
  static const char kName[] = "WiMAX Network";
  static const char kNetworkId[] = "76543210";
  service->set_friendly_name(kName);
  service->set_network_id(kNetworkId);
  service->InitStorageIdentifier();
  provider_.services_[service->GetStorageIdentifier()] = service;
  EXPECT_EQ(service.get(),
            provider_.FindService(
                WiMaxService::CreateStorageIdentifier(kNetworkId,
                                                      kName)).get());
  EXPECT_FALSE(provider_.FindService("some_storage_id"));
}

TEST_F(WiMaxProviderTest, StartLiveServices) {
  const uint32_t kIdentifier = 0x1234567;
  static const char kNetworkId[] = "01234567";
  static const char kName[] = "Some WiMAX Provider";
  vector<scoped_refptr<MockWiMaxService>> services(4);
  for (size_t i = 0; i < services.size(); i++) {
    services[i] =
        new MockWiMaxService(&control_, nullptr, &metrics_, &manager_);
    if (i == 0) {
      services[0]->set_network_id("deadbeef");
    } else {
      services[i]->set_network_id(kNetworkId);
    }
    // Make services[3] the default service.
    if (i == 3) {
      services[i]->set_friendly_name(kName);
    } else {
      services[i]->set_friendly_name(
          base::StringPrintf("Configured %d", static_cast<int>(i)));
    }
    services[i]->InitStorageIdentifier();
    provider_.services_[services[i]->GetStorageIdentifier()] = services[i];
  }
  WiMaxProvider::NetworkInfo info;
  info.id = kNetworkId;
  info.name = kName;
  provider_.networks_[GetTestNetworkPath(kIdentifier)] = info;
  EXPECT_CALL(*services[0], IsStarted()).Times(0);
  EXPECT_CALL(*services[1], IsStarted()).WillOnce(Return(true));
  EXPECT_CALL(*services[1], MockableStart(_)).Times(0);
  EXPECT_CALL(*services[2], IsStarted()).WillOnce(Return(false));
  EXPECT_CALL(*services[2], MockableStart(_)).WillOnce(Return(true));
  EXPECT_CALL(*services[3], IsStarted()).WillOnce(Return(false));
  EXPECT_CALL(*services[3], MockableStart(_)).WillOnce(Return(false));
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  provider_.StartLiveServices();
  EXPECT_FALSE(services[0]->is_default());
  EXPECT_FALSE(services[1]->is_default());
  EXPECT_FALSE(services[2]->is_default());
  EXPECT_TRUE(services[3]->is_default());
}

TEST_F(WiMaxProviderTest, DestroyAllServices) {
  vector<scoped_refptr<MockWiMaxService>> services(2);
  for (size_t i = 0; i < services.size(); i++) {
    services[i] =
        new MockWiMaxService(&control_, nullptr, &metrics_, &manager_);
    provider_.services_[services[i]->GetStorageIdentifier()] = services[i];
    EXPECT_CALL(*services[i], Stop());
  }
  EXPECT_CALL(manager_, DeregisterService(_)).Times(services.size());
  provider_.DestroyAllServices();
  EXPECT_TRUE(provider_.services_.empty());
}

TEST_F(WiMaxProviderTest, StopDeadServices) {
  vector<scoped_refptr<MockWiMaxService>> services(4);
  for (size_t i = 0; i < services.size(); i++) {
    services[i] =
        new MockWiMaxService(&control_, nullptr, &metrics_, &manager_);
    if (i == 0) {
      EXPECT_CALL(*services[i], IsStarted()).WillOnce(Return(false));
      EXPECT_CALL(*services[i], GetNetworkObjectPath()).Times(0);
      EXPECT_CALL(*services[i], Stop()).Times(0);
    } else {
      EXPECT_CALL(*services[i], IsStarted()).WillOnce(Return(true));
      EXPECT_CALL(*services[i], GetNetworkObjectPath())
          .WillOnce(Return(GetTestNetworkPath(100 + i)));
    }
    provider_.services_[services[i]->GetStorageIdentifier()] = services[i];
  }
  services[3]->set_is_default(true);
  EXPECT_CALL(*services[1], Stop()).Times(0);
  EXPECT_CALL(*services[2], Stop());
  EXPECT_CALL(*services[3], Stop());
  EXPECT_CALL(manager_, DeregisterService(_));
  provider_.networks_[GetTestNetworkPath(777)].id = "01234567";
  provider_.networks_[GetTestNetworkPath(101)].id = "12345678";
  provider_.StopDeadServices();
  EXPECT_EQ(3, provider_.services_.size());
  EXPECT_FALSE(base::ContainsKey(provider_.services_,
                                 services[3]->GetStorageIdentifier()));
}

TEST_F(WiMaxProviderTest, OnNetworksChanged) {
  static const char kName[] = "Default Network";
  const uint32_t kIdentifier = 0xabcdef;
  static const char kNetworkId[] = "00abcdef";

  // Started service to be stopped.
  scoped_refptr<MockWiMaxService> service0(
      new MockWiMaxService(&control_, nullptr, &metrics_, &manager_));
  EXPECT_CALL(*service0, IsStarted()).WillOnce(Return(true));
  EXPECT_CALL(*service0, GetNetworkObjectPath())
      .WillOnce(Return(GetTestNetworkPath(100)));
  EXPECT_CALL(*service0, MockableStart(_)).Times(0);
  EXPECT_CALL(*service0, Stop()).Times(1);
  service0->set_network_id("1234");
  service0->InitStorageIdentifier();

  // Stopped service to be started.
  scoped_refptr<MockWiMaxService> service1(
      new MockWiMaxService(&control_, nullptr, &metrics_, &manager_));
  EXPECT_CALL(*service1, IsStarted()).Times(2).WillRepeatedly(Return(false));
  EXPECT_CALL(*service1, MockableStart(_)).WillOnce(Return(true));
  EXPECT_CALL(*service1, Stop()).Times(0);
  service1->set_network_id(kNetworkId);
  service1->set_friendly_name(kName);
  service1->InitStorageIdentifier();

  auto network_proxy = std::make_unique<MockWiMaxNetworkProxy>();
  EXPECT_CALL(*network_proxy, Name(_)).WillOnce(Return(kName));
  EXPECT_CALL(*network_proxy, Identifier(_)).WillOnce(Return(kIdentifier));
  EXPECT_CALL(control_, CreateWiMaxNetworkProxy(GetTestNetworkPath(101)))
      .Times(2)
      .WillOnce(Return(ByMove(std::move(network_proxy))))
      .WillOnce(ReturnNull());

  provider_.services_[service0->GetStorageIdentifier()] = service0;
  provider_.services_[service1->GetStorageIdentifier()] = service1;

  for (int i = 0; i < 3; i++) {
    scoped_refptr<MockWiMax> device(
        new MockWiMax(&control_, nullptr, &metrics_, &manager_,
                      GetTestLinkName(i), "", i, GetTestPath(i)));
    provider_.devices_[GetTestLinkName(i)] = device;
    if (i > 0) {
      device->networks_.insert(GetTestNetworkPath(101));
    }
  }
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, DeregisterService(_)).Times(0);

  provider_.networks_["/org/chromium/foo"].id = "foo";
  provider_.OnNetworksChanged();
  EXPECT_EQ(1, provider_.networks_.size());
  EXPECT_TRUE(base::ContainsKey(provider_.networks_, GetTestNetworkPath(101)));
}

TEST_F(WiMaxProviderTest, GetUniqueService) {
  EXPECT_TRUE(provider_.services_.empty());

  static const char kName0[] = "Test WiMAX Network";
  static const char kName1[] = "Unknown Network";
  static const char kNetworkId[] = "12340000";

  // Service already exists.
  scoped_refptr<MockWiMaxService> service0(
      new MockWiMaxService(&control_, nullptr, &metrics_, &manager_));
  service0->set_network_id(kNetworkId);
  service0->set_friendly_name(kName0);
  service0->InitStorageIdentifier();
  provider_.services_[service0->GetStorageIdentifier()] = service0;
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  WiMaxServiceRefPtr service = provider_.GetUniqueService(kNetworkId, kName0);
  ASSERT_TRUE(service.get());
  EXPECT_EQ(service0.get(), service.get());
  EXPECT_EQ(1, provider_.services_.size());

  // Create a new service.
  EXPECT_CALL(manager_, RegisterService(_));
  service = provider_.GetUniqueService(kNetworkId, kName1);
  ASSERT_TRUE(service.get());
  EXPECT_NE(service0.get(), service.get());
  EXPECT_EQ(2, provider_.services_.size());
  EXPECT_EQ(WiMaxService::CreateStorageIdentifier(kNetworkId, kName1),
            service->GetStorageIdentifier());
  EXPECT_FALSE(service->is_default());

  // Service already exists -- it was just created.
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  WiMaxServiceRefPtr service1 = provider_.GetUniqueService(kNetworkId, kName1);
  ASSERT_TRUE(service1.get());
  EXPECT_EQ(service.get(), service1.get());
  EXPECT_EQ(2, provider_.services_.size());
  EXPECT_FALSE(service->is_default());
}

TEST_F(WiMaxProviderTest, CreateServicesFromProfile) {
  FakeStore store;
  store.SetString("no_type", "Name", "No Type Entry");
  store.SetString("no_wimax", "Type", "vpn");
  store.SetString("wimax_network_01234567", "Name", "network");
  store.SetString("wimax_network_01234567", "Type", "wimax");
  store.SetString("wimax_network_01234567", "NetworkId", "01234567");
  store.SetString("no_network_id", "Type", "wimax");
  store.SetString("no_name", "Type", "wimax");
  store.SetString("no_name", "NetworkId", "76543210");

  scoped_refptr<MockProfile> profile(
      new MockProfile(&control_, &metrics_, &manager_));
  EXPECT_CALL(*profile, GetConstStorage())
      .Times(2)
      .WillRepeatedly(Return(&store));
  EXPECT_CALL(manager_, RegisterService(_));
  EXPECT_CALL(*profile, ConfigureService(_)).WillOnce(Return(true));
  provider_.CreateServicesFromProfile(profile);
  ASSERT_EQ(1, provider_.services_.size());

  WiMaxServiceRefPtr service = provider_.services_.begin()->second;
  EXPECT_EQ("wimax_network_01234567", service->GetStorageIdentifier());
  provider_.CreateServicesFromProfile(profile);
  ASSERT_EQ(1, provider_.services_.size());
  EXPECT_EQ(service.get(), provider_.services_.begin()->second);
}

TEST_F(WiMaxProviderTest, CreateTemporaryServiceFromProfile) {
  FakeStore store;
  store.SetString("no_type", "Name", "No Type Entry");
  store.SetString("no_wimax", "Type", "vpn");
  store.SetString("wimax_network_01234567", "Name", "network");
  store.SetString("wimax_network_01234567", "Type", "wimax");
  store.SetString("wimax_network_01234567", "NetworkId", "01234567");
  store.SetString("no_network_id", "Type", "wimax");
  store.SetString("no_name", "Type", "wimax");
  store.SetString("no_name", "NetworkId", "76543210");
  scoped_refptr<MockProfile> profile(
      new MockProfile(&control_, &metrics_, &manager_));
  EXPECT_CALL(*profile, GetConstStorage())
      .WillRepeatedly(Return(&store));
  Error error;

  // Network type not specified.
  EXPECT_EQ(nullptr,
            provider_.CreateTemporaryServiceFromProfile(profile,
                                                        "no_type",
                                                        &error).get());
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(),
              StartsWith("Unspecified or invalid network type"));

  // Not a WiMAX network.
  error.Reset();
  EXPECT_EQ(nullptr,
            provider_.CreateTemporaryServiceFromProfile(profile,
                                                        "no_wimax",
                                                        &error).get());
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(),
              StartsWith("Unspecified or invalid network type"));

  // WiMAX network with required properties.
  error.Reset();
  EXPECT_TRUE(
      provider_.CreateTemporaryServiceFromProfile(profile,
                                                  "wimax_network_01234567",
                                                  &error).get());
  EXPECT_TRUE(error.IsSuccess());

  // Network ID not specified.
  error.Reset();
  EXPECT_EQ(nullptr,
            provider_.CreateTemporaryServiceFromProfile(profile,
                                                        "no_network_id",
                                                        &error).get());
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(),
              StartsWith("Network ID not specified"));

  // Network name not specified.
  error.Reset();
  EXPECT_EQ(nullptr,
            provider_.CreateTemporaryServiceFromProfile(profile,
                                                        "no_name",
                                                        &error).get());
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(),
              StartsWith("Network name not specified"));
}

TEST_F(WiMaxProviderTest, GetService) {
  KeyValueStore args;
  Error e;

  args.SetString(kTypeProperty, kTypeWimax);

  // No network id property.
  ServiceRefPtr service = provider_.GetService(args, &e);
  EXPECT_EQ(Error::kInvalidArguments, e.type());
  EXPECT_FALSE(service.get());

  // No name property.
  static const char kNetworkId[] = "1234abcd";
  args.SetString(WiMaxService::kNetworkIdProperty, kNetworkId);
  e.Reset();
  service = provider_.GetService(args, &e);
  EXPECT_EQ(Error::kInvalidArguments, e.type());
  EXPECT_FALSE(service.get());

  // Service created and configured.
  static const char kName[] = "Test WiMAX Network";
  args.SetString(kNameProperty, kName);
  static const char kIdentity[] = "joe";
  args.SetString(kEapIdentityProperty, kIdentity);

  e.Reset();
  service = provider_.FindSimilarService(args, &e);
  EXPECT_EQ(ServiceRefPtr(), service.get());
  EXPECT_EQ(Error::kNotFound, e.type());

  e.Reset();
  EXPECT_CALL(manager_, RegisterService(_));
  service = provider_.GetService(args, &e);
  EXPECT_TRUE(e.IsSuccess());
  ASSERT_TRUE(service.get());
  testing::Mock::VerifyAndClearExpectations(&manager_);

  // GetService should create a service with only identifying parameters set.
  EXPECT_EQ(kName, GetServiceFriendlyName(service));
  EXPECT_EQ("", service->eap()->identity());

  e.Reset();
  ServiceRefPtr similar_service = provider_.FindSimilarService(args, &e);
  EXPECT_EQ(service.get(), similar_service);
  EXPECT_TRUE(e.IsSuccess());

  // After configuring the service, other parameters should be set.
  service->Configure(args, &e);
  EXPECT_TRUE(e.IsSuccess());
  EXPECT_EQ(kIdentity, service->eap()->identity());

  e.Reset();
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  ServiceRefPtr temporary_service = provider_.CreateTemporaryService(args, &e);
  EXPECT_NE(ServiceRefPtr(), temporary_service);
  EXPECT_NE(service.get(), temporary_service);
  EXPECT_TRUE(e.IsSuccess());
}

TEST_F(WiMaxProviderTest, SelectCarrier) {
  scoped_refptr<MockWiMaxService> service(
      new MockWiMaxService(&control_, nullptr, &metrics_, &manager_));
  EXPECT_FALSE(provider_.SelectCarrier(service));
  scoped_refptr<MockWiMax> device(
      new MockWiMax(&control_, nullptr, &metrics_, &manager_,
                    GetTestLinkName(1), "", 1, GetTestPath(1)));
  provider_.devices_[GetTestLinkName(1)] = device;
  WiMaxRefPtr carrier = provider_.SelectCarrier(service);
  EXPECT_EQ(device.get(), carrier.get());
}

TEST_F(WiMaxProviderTest, OnServiceUnloaded) {
  scoped_refptr<MockWiMaxService> service(
      new MockWiMaxService(&control_, nullptr, &metrics_, &manager_));
  EXPECT_FALSE(service->is_default());
  scoped_refptr<MockWiMaxService> service_default(
      new MockWiMaxService(&control_, nullptr, &metrics_, &manager_));
  service_default->set_is_default(true);
  provider_.services_[service->GetStorageIdentifier()] = service;
  provider_.services_[service_default->GetStorageIdentifier()] =
      service_default;
  EXPECT_CALL(manager_, DeregisterService(_)).Times(0);
  EXPECT_FALSE(provider_.OnServiceUnloaded(service_default));
  EXPECT_EQ(2, provider_.services_.size());
  EXPECT_TRUE(provider_.OnServiceUnloaded(service));
  EXPECT_EQ(1, provider_.services_.size());
  EXPECT_EQ(service_default.get(), provider_.services_.begin()->second.get());
}

}  // namespace shill
