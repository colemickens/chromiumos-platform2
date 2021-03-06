// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_CONNECTION_H_
#define SHILL_MOCK_CONNECTION_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/connection.h"

namespace shill {

class MockConnection : public Connection {
 public:
  explicit MockConnection(const DeviceInfo* device_info);
  ~MockConnection() override;

  MOCK_METHOD1(UpdateFromIPConfig, void(const IPConfigRefPtr& config));
  MOCK_CONST_METHOD0(GetLowerConnection, ConnectionRefPtr());
  MOCK_METHOD0(IsDefault, bool());
  MOCK_METHOD1(SetMetric, void(uint32_t metric));
  MOCK_METHOD1(SetUseDNS, void(bool enable));
  MOCK_CONST_METHOD0(ipconfig_rpc_identifier, const std::string&());
  MOCK_METHOD0(RequestRouting, void());
  MOCK_METHOD0(ReleaseRouting, void());
  MOCK_CONST_METHOD0(interface_name, const std::string&());
  MOCK_CONST_METHOD0(dns_servers, const std::vector<std::string>&());
  MOCK_METHOD1(RequestHostRoute, bool(const IPAddress& destination));
  MOCK_METHOD2(PinPendingRoutes,
               bool(int interface_index, RoutingTableEntry entry));
  MOCK_CONST_METHOD0(local, const IPAddress&());
  MOCK_CONST_METHOD0(gateway, const IPAddress&());
  MOCK_CONST_METHOD0(technology, Technology::Identifier());
  MOCK_METHOD0(CreateGatewayRoute, bool());
  MOCK_METHOD0(GetCarrierConnection, ConnectionRefPtr());
  MOCK_CONST_METHOD0(tethering, std::string&());
  MOCK_METHOD1(UpdateDNSServers,
               void(const std::vector<std::string>& dns_servers));
  MOCK_METHOD0(IsIPv6, bool());
  MOCK_CONST_METHOD0(GetSubnetName, std::string());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockConnection);
};

}  // namespace shill

#endif  // SHILL_MOCK_CONNECTION_H_
