// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/socket_info_reader.h"

#include <algorithm>
#include <limits>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "shill/file_reader.h"
#include "shill/logging.h"

using base::FilePath;
using std::string;
using std::vector;

namespace shill {

namespace {

const char kTcpv4SocketInfoFilePath[] = "/proc/net/tcp";
const char kTcpv6SocketInfoFilePath[] = "/proc/net/tcp6";

}  // namespace

SocketInfoReader::SocketInfoReader() {}

SocketInfoReader::~SocketInfoReader() {}

FilePath SocketInfoReader::GetTcpv4SocketInfoFilePath() const {
  return FilePath(kTcpv4SocketInfoFilePath);
}

FilePath SocketInfoReader::GetTcpv6SocketInfoFilePath() const {
  return FilePath(kTcpv6SocketInfoFilePath);
}

bool SocketInfoReader::LoadTcpSocketInfo(vector<SocketInfo> *info_list) {
  info_list->clear();
  bool v4_loaded = AppendSocketInfo(GetTcpv4SocketInfoFilePath(), info_list);
  bool v6_loaded = AppendSocketInfo(GetTcpv6SocketInfoFilePath(), info_list);
  // Return true if we can load either /proc/net/tcp or /proc/net/tcp6
  // successfully.
  return v4_loaded || v6_loaded;
}

bool SocketInfoReader::AppendSocketInfo(const FilePath &info_file_path,
                                        vector<SocketInfo> *info_list) {
  FileReader file_reader;
  if (!file_reader.Open(info_file_path)) {
    SLOG(Link, 2) << __func__ << ": Failed to open '"
                  << info_file_path.value() << "'.";
    return false;
  }

  string line;
  while (file_reader.ReadLine(&line)) {
    SocketInfo socket_info;
    if (ParseSocketInfo(line, &socket_info))
      info_list->push_back(socket_info);
  }
  return true;
}

bool SocketInfoReader::ParseSocketInfo(const string &input,
                                       SocketInfo *socket_info) {
  vector<string> tokens;
  base::SplitStringAlongWhitespace(input, &tokens);
  if (tokens.size() < 10) {
    return false;
  }

  IPAddress ip_address(IPAddress::kFamilyUnknown);
  uint16 port = 0;

  if (!ParseIPAddressAndPort(tokens[1], &ip_address, &port)) {
    return false;
  }
  socket_info->set_local_ip_address(ip_address);
  socket_info->set_local_port(port);

  if (!ParseIPAddressAndPort(tokens[2], &ip_address, &port)) {
    return false;
  }
  socket_info->set_remote_ip_address(ip_address);
  socket_info->set_remote_port(port);

  SocketInfo::ConnectionState connection_state =
      SocketInfo::kConnectionStateUnknown;
  if (!ParseConnectionState(tokens[3], &connection_state)) {
    return false;
  }
  socket_info->set_connection_state(connection_state);

  uint64 transmit_queue_value = 0, receive_queue_value = 0;
  if (!ParseTransimitAndReceiveQueueValues(
      tokens[4], &transmit_queue_value, &receive_queue_value)) {
    return false;
  }
  socket_info->set_transmit_queue_value(transmit_queue_value);
  socket_info->set_receive_queue_value(receive_queue_value);

  SocketInfo::TimerState timer_state = SocketInfo::kTimerStateUnknown;
  if (!ParseTimerState(tokens[5], &timer_state)) {
    return false;
  }
  socket_info->set_timer_state(timer_state);

  return true;
}

bool SocketInfoReader::ParseIPAddressAndPort(
    const string &input, IPAddress *ip_address, uint16 *port) {
  vector<string> tokens;

  base::SplitString(input, ':', &tokens);
  if (tokens.size() != 2 ||
      !ParseIPAddress(tokens[0], ip_address) ||
      !ParsePort(tokens[1], port)) {
    return false;
  }

  return true;
}

bool SocketInfoReader::ParseIPAddress(const string &input,
                                      IPAddress *ip_address) {
  ByteString byte_string = ByteString::CreateFromHexString(input);
  if (byte_string.IsEmpty())
    return false;

  IPAddress::Family family;
  if (byte_string.GetLength() ==
      IPAddress::GetAddressLength(IPAddress::kFamilyIPv4)) {
    family = IPAddress::kFamilyIPv4;
  } else if (byte_string.GetLength() ==
             IPAddress::GetAddressLength(IPAddress::kFamilyIPv6)) {
    family = IPAddress::kFamilyIPv6;
  } else {
    return false;
  }

  // Linux kernel prints out IP addresses in network order via
  // /proc/net/tcp{,6}.
  byte_string.ConvertFromNetToCPUUInt32Array();

  *ip_address = IPAddress(family, byte_string);
  return true;
}

bool SocketInfoReader::ParsePort(const string &input, uint16 *port) {
  int result = 0;

  if (input.size() != 4 || !base::HexStringToInt(input, &result) ||
      result < 0 || result > std::numeric_limits<uint16>::max()) {
    return false;
  }

  *port = result;
  return true;
}

bool SocketInfoReader::ParseTransimitAndReceiveQueueValues(
    const string &input,
    uint64 *transmit_queue_value, uint64 *receive_queue_value) {
  vector<string> tokens;
  int64 signed_transmit_queue_value = 0, signed_receive_queue_value = 0;

  base::SplitString(input, ':', &tokens);
  if (tokens.size() != 2 ||
      !base::HexStringToInt64(tokens[0], &signed_transmit_queue_value) ||
      !base::HexStringToInt64(tokens[1], &signed_receive_queue_value)) {
    return false;
  }

  *transmit_queue_value = static_cast<uint64>(signed_transmit_queue_value);
  *receive_queue_value = static_cast<uint64>(signed_receive_queue_value);
  return true;
}

bool SocketInfoReader::ParseConnectionState(
    const string &input, SocketInfo::ConnectionState *connection_state) {
  int result = 0;

  if (input.size() != 2 || !base::HexStringToInt(input, &result)) {
    return false;
  }

  if (result > 0 && result < SocketInfo::kConnectionStateMax) {
    *connection_state = static_cast<SocketInfo::ConnectionState>(result);
  } else {
    *connection_state = SocketInfo::kConnectionStateUnknown;
  }
  return true;
}

bool SocketInfoReader::ParseTimerState(
    const string &input, SocketInfo::TimerState *timer_state) {
  vector<string> tokens;
  int result = 0;

  base::SplitString(input, ':', &tokens);
  if (tokens.size() != 2 || tokens[0].size() != 2 ||
      !base::HexStringToInt(tokens[0], &result)) {
    return false;
  }

  if (result < SocketInfo::kTimerStateMax) {
    *timer_state = static_cast<SocketInfo::TimerState>(result);
  } else {
    *timer_state = SocketInfo::kTimerStateUnknown;
  }
  return true;
}

}  // namespace shill
