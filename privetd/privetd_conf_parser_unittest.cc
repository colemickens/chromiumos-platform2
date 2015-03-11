// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "privetd/privetd_conf_parser.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <chromeos/strings/string_utils.h>
#include <gtest/gtest.h>

#include "privetd/security_delegate.h"

using chromeos::string_utils::Join;
using chromeos::KeyValueStore;
using std::map;
using std::string;

namespace privetd {

namespace {

const char kWiFiBootstrapMode[] = "wifi_bootstrapping_mode";
const char kGcdBootstrapMode[] = "gcd_bootstrapping_mode";
const char kConnectTimeout[] = "connect_timeout_seconds";
const char kBootstrapTimeout[] = "bootstrap_timeout_seconds";
const char kMonitorTimeout[] = "monitor_timeout_seconds";
const char kDeviceServices[] = "device_services";
const char kDeviceClass[] = "device_class";
const char kDeviceMake[] = "device_make";
const char kDeviceModel[] = "device_model";
const char kDeviceModelId[] = "device_model_id";
const char kDeviceName[] = "device_name";
const char kDeviceDescription[] = "device_description";
const char kPairingModes[] = "pairing_modes";
const char kEmbeddedCodePath[] = "embedded_code_path";

}  // namespace

class PrivetdConfParserTest : public testing::Test {
 public:
  using ConfDict = map<string, string>;

  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());
    temp_file_ = temp_dir_.path().Append("temp.conf");
  }

  bool IsValid(const ConfDict& conf_dict) {
    KeyValueStore store;
    FillKeyValueStore(conf_dict, &store);
    PrivetdConfigParser config;
    return config.Parse(store);
  }

  void FillKeyValueStore(const ConfDict& conf_dict, KeyValueStore* store) {
    std::vector<string> file_pieces;
    for (const auto& it : conf_dict) {
      file_pieces.push_back(Join('=', it.first, it.second));
    }
    string blob{Join('\n', file_pieces)};
    int expected_len = blob.length();
    CHECK(expected_len == base::WriteFile(temp_file_,
                                          blob.c_str(),
                                          expected_len));
    CHECK(store->Load(temp_file_));
  }

 private:
  base::FilePath temp_file_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(PrivetdConfParserTest, ShouldRejectInvalidTimeouts) {
  EXPECT_FALSE(IsValid({{kConnectTimeout, "-1"}}));
  EXPECT_FALSE(IsValid({{kConnectTimeout, "a"}}));
  EXPECT_FALSE(IsValid({{kConnectTimeout, ""}}));
  EXPECT_FALSE(IsValid({{kConnectTimeout, "30 430"}}));
}

TEST_F(PrivetdConfParserTest, ShouldRejectInvalidWiFiBootstrapModes) {
  EXPECT_FALSE(IsValid({{kWiFiBootstrapMode, ""}}));
  EXPECT_FALSE(IsValid({{kWiFiBootstrapMode, "clown_shoes"}}));
  EXPECT_FALSE(IsValid({{kWiFiBootstrapMode, "off is invalid"}}));
  EXPECT_FALSE(IsValid({{kWiFiBootstrapMode, "30"}}));
}

TEST_F(PrivetdConfParserTest, ShouldRejectInvalidGcdBootstrapModes) {
  EXPECT_FALSE(IsValid({{kGcdBootstrapMode, ""}}));
  EXPECT_FALSE(IsValid({{kGcdBootstrapMode, "clown_shoes"}}));
  EXPECT_FALSE(IsValid({{kGcdBootstrapMode, "off is invalid"}}));
  EXPECT_FALSE(IsValid({{kGcdBootstrapMode, "30"}}));
}

TEST_F(PrivetdConfParserTest, ShouldRejectInvalidServices) {
  EXPECT_FALSE(IsValid({{kDeviceServices, "_a,b"}}));
}

TEST_F(PrivetdConfParserTest, ShouldRejectInvalidDeviceClass) {
  EXPECT_FALSE(IsValid({{kDeviceClass, ""}}));
  EXPECT_FALSE(IsValid({{kDeviceClass, "a"}}));
  EXPECT_FALSE(IsValid({{kDeviceClass, "aaaa"}}));
}

TEST_F(PrivetdConfParserTest, ShouldRejectInvalidModelId) {
  EXPECT_FALSE(IsValid({{kDeviceModelId, ""}}));
  EXPECT_FALSE(IsValid({{kDeviceModelId, "a"}}));
  EXPECT_FALSE(IsValid({{kDeviceModelId, "bb"}}));
  EXPECT_FALSE(IsValid({{kDeviceModelId, "cccc"}}));
}

TEST_F(PrivetdConfParserTest, ShouldRejectInvalidName) {
  EXPECT_FALSE(IsValid({{kDeviceName, ""}}));
}

TEST_F(PrivetdConfParserTest, ShouldParseSettings) {
  const std::set<std::string> kExpectedWiFiInterfaces{"eth1", "clown shoes"};
  const uint32_t kExpectedConnectTimeout{1};
  const uint32_t kExpectedBootstrapTimeout{2};
  const uint32_t kExpectedMonitorTimeout{3};
  const std::set<std::string> kExpectedDeviceServices{"a", "b", "xwz"};
  static const char kExpectedDeviceClass[]{"BB"};
  static const char kExpectedDeviceMake[]{"testMade"};
  static const char kExpectedDeviceModel[]{"testModel"};
  static const char kExpectedDeviceModelId[]{"BBB"};
  static const char kExpectedDeviceName[]{"testDevice"};
  static const char kExpectedDeviceDescription[]{"testDescription"};
  const std::set<PairingType> kExpectedPairingModes{PairingType::kEmbeddedCode,
                                                    PairingType::kPinCode};
  static const char kExpectedEmbeddedCodePath[]{"123ABC"};
  const ConfDict conf_dict{
      {kWiFiBootstrapMode, "automatic"},
      {kGcdBootstrapMode, "automatic"},
      {kWiFiBootstrapInterfaces, Join(',', kExpectedWiFiInterfaces)},
      {kConnectTimeout, std::to_string(kExpectedConnectTimeout)},
      {kBootstrapTimeout, std::to_string(kExpectedBootstrapTimeout)},
      {kMonitorTimeout, std::to_string(kExpectedMonitorTimeout)},
      {kDeviceServices, Join(',', kExpectedDeviceServices)},
      {kDeviceClass, kExpectedDeviceClass},
      {kDeviceMake, kExpectedDeviceMake},
      {kDeviceModel, kExpectedDeviceModel},
      {kDeviceModelId, kExpectedDeviceModelId},
      {kDeviceName, kExpectedDeviceName},
      {kDeviceDescription, kExpectedDeviceDescription},
      {kPairingModes, "pinCode"},
      {kEmbeddedCodePath, kExpectedEmbeddedCodePath},
  };
  KeyValueStore store;
  FillKeyValueStore(conf_dict, &store);
  PrivetdConfigParser parser;
  EXPECT_TRUE(parser.Parse(store));
  EXPECT_EQ(WiFiBootstrapMode::kAutomatic, parser.wifi_bootstrap_mode());
  EXPECT_EQ(GcdBootstrapMode::kAutomatic, parser.gcd_bootstrap_mode());
  EXPECT_EQ(kExpectedWiFiInterfaces, parser.automatic_wifi_interfaces());
  EXPECT_EQ(kExpectedConnectTimeout, parser.connect_timeout_seconds());
  EXPECT_EQ(kExpectedBootstrapTimeout, parser.bootstrap_timeout_seconds());
  EXPECT_EQ(kExpectedMonitorTimeout, parser.monitor_timeout_seconds());
  EXPECT_EQ(kExpectedDeviceServices, parser.device_services());
  EXPECT_EQ(kExpectedDeviceClass, parser.device_class());
  EXPECT_EQ(kExpectedDeviceMake, parser.device_make());
  EXPECT_EQ(kExpectedDeviceModel, parser.device_model());
  EXPECT_EQ(kExpectedDeviceModelId, parser.device_model_id());
  EXPECT_EQ(kExpectedDeviceName, parser.device_name());
  EXPECT_EQ(kExpectedDeviceDescription, parser.device_description());
  EXPECT_EQ(kExpectedPairingModes, parser.pairing_modes());
  EXPECT_EQ(kExpectedEmbeddedCodePath, parser.embedded_code_path().value());
}

TEST_F(PrivetdConfParserTest, CriticalDefaults) {
  PrivetdConfigParser parser;
  EXPECT_EQ(WiFiBootstrapMode::kDisabled, parser.wifi_bootstrap_mode());
  EXPECT_EQ(GcdBootstrapMode::kDisabled, parser.gcd_bootstrap_mode());
  EXPECT_GT(parser.connect_timeout_seconds(), 0);
  EXPECT_GT(parser.bootstrap_timeout_seconds(), 0);
  EXPECT_GT(parser.monitor_timeout_seconds(), 0);
  EXPECT_EQ(2, parser.device_class().size());
  EXPECT_FALSE(parser.device_make().empty());
  EXPECT_FALSE(parser.device_model().empty());
  EXPECT_EQ(3, parser.device_model_id().size());
  EXPECT_FALSE(parser.device_name().empty());
  EXPECT_EQ(std::set<PairingType>{PairingType::kPinCode},
            parser.pairing_modes());
  EXPECT_TRUE(parser.embedded_code_path().empty());
}

}  // namespace privetd
