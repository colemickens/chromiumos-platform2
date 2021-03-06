// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/subnet.h"

#include <arpa/inet.h>
#include <stdint.h>

#include <base/bind.h>
#include <gtest/gtest.h>

namespace vm_tools {
namespace concierge {
namespace {

constexpr size_t kContainerBaseAddress = 0x64735cc0;  // 100.115.92.192
constexpr size_t kVmBaseAddress = 0x64735c00;         // 100.115.92.0

constexpr size_t kContainerSubnetPrefix = 28;
constexpr size_t kVmSubnetPrefix = 30;

// kExpectedAvailableCount[i] == AvailableCount() for subnet with prefix i.
constexpr size_t kExpectedAvailableCount[] = {
    0xfffffffe, 0x7ffffffe, 0x3ffffffe, 0x1ffffffe, 0xffffffe, 0x7fffffe,
    0x3fffffe,  0x1fffffe,  0xfffffe,   0x7ffffe,   0x3ffffe,  0x1ffffe,
    0xffffe,    0x7fffe,    0x3fffe,    0x1fffe,    0xfffe,    0x7ffe,
    0x3ffe,     0x1ffe,     0xffe,      0x7fe,      0x3fe,     0x1fe,
    0xfe,       0x7e,       0x3e,       0x1e,       0xe,       0x6,
    0x2,        0x0,
};

// kExpectedNetmask[i] == Netmask() for subnet with prefix i.
constexpr uint32_t kExpectedNetmask[] = {
    0x00000000, 0x80000000, 0xc0000000, 0xe0000000, 0xf0000000, 0xf8000000,
    0xfc000000, 0xfe000000, 0xff000000, 0xff800000, 0xffc00000, 0xffe00000,
    0xfff00000, 0xfff80000, 0xfffc0000, 0xfffe0000, 0xffff0000, 0xffff8000,
    0xffffc000, 0xffffe000, 0xfffff000, 0xfffff800, 0xfffffc00, 0xfffffe00,
    0xffffff00, 0xffffff80, 0xffffffc0, 0xffffffe0, 0xfffffff0, 0xfffffff8,
    0xfffffffc, 0xfffffffe,
};

class VmSubnetTest : public ::testing::TestWithParam<size_t> {};
class ContainerSubnetTest : public ::testing::TestWithParam<size_t> {};
class PrefixTest : public ::testing::TestWithParam<size_t> {};

void DoNothing() {}

void SetTrue(bool* value) {
  *value = true;
}

}  // namespace

TEST_P(VmSubnetTest, AddressAtOffset) {
  size_t index = GetParam();
  Subnet subnet(kVmBaseAddress + index * 4, kVmSubnetPrefix,
                base::Bind(&DoNothing));

  for (uint32_t offset = 0; offset < subnet.AvailableCount(); ++offset) {
    uint32_t address = htonl(kVmBaseAddress + index * 4 + offset + 1);
    EXPECT_EQ(address, subnet.AddressAtOffset(offset));
  }
}

INSTANTIATE_TEST_CASE_P(AllValues,
                        VmSubnetTest,
                        ::testing::Range(size_t{1}, size_t{32}));

TEST_P(ContainerSubnetTest, AddressAtOffset) {
  size_t index = GetParam();
  Subnet subnet(kContainerBaseAddress + index * 16, kContainerSubnetPrefix,
                base::Bind(&DoNothing));

  for (uint32_t offset = 0; offset < subnet.AvailableCount(); ++offset) {
    uint32_t address = htonl(kContainerBaseAddress + index * 16 + offset + 1);
    EXPECT_EQ(address, subnet.AddressAtOffset(offset));
  }
}

INSTANTIATE_TEST_CASE_P(AllValues,
                        ContainerSubnetTest,
                        ::testing::Range(size_t{1}, size_t{4}));

TEST_P(PrefixTest, AvailableCount) {
  size_t prefix = GetParam();

  Subnet subnet(0, prefix, base::Bind(&DoNothing));
  EXPECT_EQ(kExpectedAvailableCount[prefix], subnet.AvailableCount());
}

TEST_P(PrefixTest, Netmask) {
  size_t prefix = GetParam();

  Subnet subnet(0, prefix, base::Bind(&DoNothing));
  EXPECT_EQ(htonl(kExpectedNetmask[prefix]), subnet.Netmask());
}

INSTANTIATE_TEST_CASE_P(AllValues,
                        PrefixTest,
                        ::testing::Range(size_t{0}, size_t{32}));

// Tests that the Subnet runs the provided cleanup callback when it gets
// destroyed.
TEST(Subnet, Cleanup) {
  bool called = false;

  { Subnet subnet(0, 24, base::Bind(&SetTrue, &called)); }

  EXPECT_TRUE(called);
}

}  // namespace concierge
}  // namespace vm_tools
