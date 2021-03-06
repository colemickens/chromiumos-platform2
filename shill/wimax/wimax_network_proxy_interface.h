// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIMAX_WIMAX_NETWORK_PROXY_INTERFACE_H_
#define SHILL_WIMAX_WIMAX_NETWORK_PROXY_INTERFACE_H_

#include <string>

#include <base/callback_forward.h>

#include "shill/accessor_interface.h"

namespace shill {

// Generally, a string representation of a Network's Identifier. We may group
// several different network identifiers into a single representative
// WiMaxNetworkId, if necessary.
using WiMaxNetworkId = std::string;

class Error;

// These are the methods that a WiMaxManager.Network proxy must support. The
// interface is provided so that it can be mocked in tests.
class WiMaxNetworkProxyInterface {
 public:
  using SignalStrengthChangedCallback = base::Callback<void(int)>;

  virtual ~WiMaxNetworkProxyInterface() {}

  virtual RpcIdentifier path() const = 0;

  virtual void set_signal_strength_changed_callback(
      const SignalStrengthChangedCallback& callback) = 0;

  // Properties.
  virtual uint32_t Identifier(Error* error) = 0;
  virtual std::string Name(Error* error) = 0;
  virtual int Type(Error* error) = 0;
  virtual int CINR(Error* error) = 0;
  virtual int RSSI(Error* error) = 0;
  virtual int SignalStrength(Error* error) = 0;
};

}  // namespace shill

#endif  // SHILL_WIMAX_WIMAX_NETWORK_PROXY_INTERFACE_H_
