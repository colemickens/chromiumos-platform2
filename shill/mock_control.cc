// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_control.h"

#include <memory>

#include "shill/mock_adaptors.h"

namespace shill {

MockControl::MockControl() {}

MockControl::~MockControl() {}

std::unique_ptr<DeviceAdaptorInterface> MockControl::CreateDeviceAdaptor(
    Device* /*device*/) {
  return std::make_unique<DeviceMockAdaptor>();
}

std::unique_ptr<IPConfigAdaptorInterface> MockControl::CreateIPConfigAdaptor(
    IPConfig* /*config*/) {
  return std::make_unique<IPConfigMockAdaptor>();
}

std::unique_ptr<ManagerAdaptorInterface> MockControl::CreateManagerAdaptor(
    Manager* /*manager*/) {
  return std::make_unique<ManagerMockAdaptor>();
}

std::unique_ptr<ProfileAdaptorInterface> MockControl::CreateProfileAdaptor(
    Profile* /*profile*/) {
  return std::make_unique<ProfileMockAdaptor>();
}

std::unique_ptr<RPCTaskAdaptorInterface> MockControl::CreateRPCTaskAdaptor(
    RPCTask* /*task*/) {
  return std::make_unique<RPCTaskMockAdaptor>();
}

std::unique_ptr<ServiceAdaptorInterface> MockControl::CreateServiceAdaptor(
    Service* /*service*/) {
  return std::make_unique<ServiceMockAdaptor>();
}

#ifndef DISABLE_VPN
std::unique_ptr<ThirdPartyVpnAdaptorInterface>
MockControl::CreateThirdPartyVpnAdaptor(ThirdPartyVpnDriver* /*driver*/) {
  return std::make_unique<ThirdPartyVpnMockAdaptor>();
}
#endif

const std::string& MockControl::NullRPCIdentifier() {
  return null_identifier_;
}

}  // namespace shill
