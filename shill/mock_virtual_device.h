//
// Copyright (C) 2012 The Android Open Source Project
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

#ifndef SHILL_MOCK_VIRTUAL_DEVICE_H_
#define SHILL_MOCK_VIRTUAL_DEVICE_H_

#include <string>

#include <gmock/gmock.h>

#include "shill/virtual_device.h"

namespace shill {

class MockVirtualDevice : public VirtualDevice {
 public:
  MockVirtualDevice(ControlInterface* control,
                    EventDispatcher* dispatcher,
                    Metrics* metrics,
                    Manager* manager,
                    const std::string& link_name,
                    int interface_index,
                    Technology::Identifier technology);
  ~MockVirtualDevice() override;

  MOCK_METHOD2(Stop,
               void(Error* error, const EnabledStateChangedCallback& callback));
  MOCK_METHOD1(UpdateIPConfig,
               void(const IPConfig::Properties& properties));
  MOCK_METHOD0(DropConnection, void());
  MOCK_METHOD0(ResetConnection, void());
  MOCK_METHOD1(SetServiceState, void(Service::ConnectState state));
  MOCK_METHOD1(SetEnabled, void(bool));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockVirtualDevice);
};

}  // namespace shill

#endif  // SHILL_MOCK_VIRTUAL_DEVICE_H_
