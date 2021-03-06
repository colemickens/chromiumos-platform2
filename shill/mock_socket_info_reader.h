// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_SOCKET_INFO_READER_H_
#define SHILL_MOCK_SOCKET_INFO_READER_H_

#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/socket_info_reader.h"

namespace shill {

struct SocketInfo;

class MockSocketInfoReader : public SocketInfoReader {
 public:
  MockSocketInfoReader();
  ~MockSocketInfoReader() override;

  MOCK_METHOD1(LoadTcpSocketInfo, bool(std::vector<SocketInfo>* info_list));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSocketInfoReader);
};

}  // namespace shill

#endif  // SHILL_MOCK_SOCKET_INFO_READER_H_
