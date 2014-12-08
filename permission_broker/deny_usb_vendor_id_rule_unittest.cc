// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/deny_usb_vendor_id_rule.h"

#include <gtest/gtest.h>

static const uint16_t kLinuxFoundationUsbVendorId = 0x1d6b;

namespace permission_broker {

class DenyUsbVendorIdRuleTest : public testing::Test {
 public:
  DenyUsbVendorIdRuleTest() : rule_(kLinuxFoundationUsbVendorId) {}
  ~DenyUsbVendorIdRuleTest() override = default;

 protected:
  DenyUsbVendorIdRule rule_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DenyUsbVendorIdRuleTest);
};

TEST_F(DenyUsbVendorIdRuleTest, IgnoreNonUsbDevice) {
  ASSERT_EQ(Rule::IGNORE, rule_.Process("/dev/loop0",
                                        Rule::ANY_INTERFACE));
}

TEST_F(DenyUsbVendorIdRuleTest, DISABLED_DenyMatchingUsbDevice) {
  ASSERT_EQ(Rule::DENY, rule_.Process("/dev/bus/usb/001/001",
                                      Rule::ANY_INTERFACE));
}

}  // namespace permission_broker
