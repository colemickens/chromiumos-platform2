// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_QMI_CONSTANTS_H_
#define HERMES_QMI_CONSTANTS_H_

namespace hermes {

// QMI UIM Info1 tag as specified in SGP.22 ES10b.GetEuiccInfo
constexpr uint16_t kEsimInfo1 = 0xBF20;

// QMI UIM command codes as specified by QMI UIM service.
enum class QmiCommand {
  kSendApdu = 0x003B,
  kLogicalChannel = 0x003F,
  kOpenLogicalChannel = 0x0042,
};

}  // namespace hermes

#endif  // HERMES_QMI_CONSTANTS_H_