// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem_info.h"

#include <memory>

#include <gtest/gtest.h>

#include "shill/cellular/mock_dbus_objectmanager_proxy.h"
#include "shill/cellular/mock_modem_manager_proxy.h"
#include "shill/cellular/modem_manager.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::Test;

namespace shill {

class ModemInfoTest : public Test {
 public:
  ModemInfoTest()
      : metrics_(&dispatcher_),
        manager_(&control_interface_, &dispatcher_, &metrics_),
        modem_info_(&control_interface_, &dispatcher_, &metrics_, &manager_) {}

 protected:
  MockControl control_interface_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  ModemInfo modem_info_;
};

TEST_F(ModemInfoTest, StartStop) {
  EXPECT_EQ(0, modem_info_.modem_managers_.size());
  EXPECT_CALL(control_interface_,
              CreateModemManagerProxy(_, _, "org.chromium.ModemManager", _, _))
      .WillOnce(Return(ByMove(std::make_unique<MockModemManagerProxy>())));
  EXPECT_CALL(
      control_interface_,
      CreateDBusObjectManagerProxy(_, "org.freedesktop.ModemManager1", _, _))
      .WillOnce(Return(ByMove(std::make_unique<MockDBusObjectManagerProxy>())));
  modem_info_.Start();
  EXPECT_EQ(2, modem_info_.modem_managers_.size());
  modem_info_.Stop();
  EXPECT_EQ(0, modem_info_.modem_managers_.size());
}

TEST_F(ModemInfoTest, RegisterModemManager) {
  static const char kService[] = "some.dbus.service";
  EXPECT_CALL(control_interface_,
              CreateModemManagerProxy(_, _, kService, _, _))
      .WillOnce(Return(ByMove(std::make_unique<MockModemManagerProxy>())));
  modem_info_.RegisterModemManager(std::make_unique<ModemManagerClassic>(
      kService, "/dbus/service/path", &modem_info_));
  ASSERT_EQ(1, modem_info_.modem_managers_.size());
  ModemManager* manager = modem_info_.modem_managers_[0].get();
  EXPECT_EQ(kService, manager->service_);
  EXPECT_EQ(&modem_info_, manager->modem_info_);
}

}  // namespace shill
