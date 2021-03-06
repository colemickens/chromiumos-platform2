// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ADAPTOR_INTERFACES_H_
#define SHILL_ADAPTOR_INTERFACES_H_

#include <string>
#include <vector>

#include <base/callback.h>

#include "shill/accessor_interface.h"

namespace shill {

// These are the functions that a Device adaptor must support
class DeviceAdaptorInterface {
 public:
  virtual ~DeviceAdaptorInterface() {}

  // Getter for the opaque identifier that represents this object on the
  // RPC interface to which the implementation is adapting.
  virtual const std::string& GetRpcIdentifier() const = 0;

  virtual void EmitBoolChanged(const std::string& name, bool value) = 0;
  virtual void EmitUintChanged(const std::string& name, uint32_t value) = 0;
  virtual void EmitUint16Changed(const std::string& name, uint16_t value) = 0;
  virtual void EmitIntChanged(const std::string& name, int value) = 0;
  virtual void EmitStringChanged(const std::string& name,
                                 const std::string& value) = 0;
  virtual void EmitStringmapChanged(const std::string& name,
                                    const Stringmap& value) = 0;
  virtual void EmitStringmapsChanged(const std::string& name,
                                     const Stringmaps& value) = 0;
  virtual void EmitStringsChanged(const std::string& name,
                                  const Strings& value) = 0;
  virtual void EmitKeyValueStoreChanged(const std::string& name,
                                        const KeyValueStore& value) = 0;
  virtual void EmitRpcIdentifierChanged(const std::string& name,
                                        const std::string& value) = 0;
  virtual void EmitRpcIdentifierArrayChanged(
      const std::string& name, const std::vector<std::string>& value) = 0;
};

// These are the functions that an IPConfig adaptor must support
class IPConfigAdaptorInterface {
 public:
  virtual ~IPConfigAdaptorInterface() {}

  // Getter for the opaque identifier that represents this object on the
  // RPC interface to which the implementation is adapting.
  virtual const std::string& GetRpcIdentifier() const = 0;

  virtual void EmitBoolChanged(const std::string& name, bool value) = 0;
  virtual void EmitUintChanged(const std::string& name, uint32_t value) = 0;
  virtual void EmitIntChanged(const std::string& name, int value) = 0;
  virtual void EmitStringChanged(const std::string& name,
                                 const std::string& value) = 0;
  virtual void EmitStringsChanged(const std::string& name,
                                  const std::vector<std::string>& value) = 0;
};

// These are the functions that a Manager adaptor must support
class ManagerAdaptorInterface {
 public:
  virtual ~ManagerAdaptorInterface() {}

  virtual void RegisterAsync(
      const base::Callback<void(bool)>& completion_callback) = 0;

  // Getter for the opaque identifier that represents this object on the
  // RPC interface to which the implementation is adapting.
  virtual const std::string& GetRpcIdentifier() const = 0;

  virtual void EmitBoolChanged(const std::string& name, bool value) = 0;
  virtual void EmitUintChanged(const std::string& name, uint32_t value) = 0;
  virtual void EmitIntChanged(const std::string& name, int value) = 0;
  virtual void EmitStringChanged(const std::string& name,
                                 const std::string& value) = 0;
  virtual void EmitStringsChanged(const std::string& name,
                                  const std::vector<std::string>& value) = 0;
  virtual void EmitRpcIdentifierChanged(
      const std::string& name,
      const std::string& value) = 0;
  virtual void EmitRpcIdentifierArrayChanged(
      const std::string& name,
      const std::vector<std::string>& value) = 0;
};

// These are the functions that a Profile adaptor must support
class ProfileAdaptorInterface {
 public:
  virtual ~ProfileAdaptorInterface() {}

  // Getter for the opaque identifier that represents this object on the
  // RPC interface to which the implementation is adapting.
  virtual const std::string& GetRpcIdentifier() const = 0;

  virtual void EmitBoolChanged(const std::string& name, bool value) = 0;
  virtual void EmitUintChanged(const std::string& name, uint32_t value) = 0;
  virtual void EmitIntChanged(const std::string& name, int value) = 0;
  virtual void EmitStringChanged(const std::string& name,
                                 const std::string& value) = 0;
};

// These are the functions that a RPCTask adaptor must support.
class RPCTaskAdaptorInterface {
 public:
  virtual ~RPCTaskAdaptorInterface() {}

  // Getter for the opaque identifier that represents this object on the
  // RPC interface to which the implementation is adapting.
  virtual const std::string& GetRpcIdentifier() const = 0;

  // Getter for the opaque identifier that represents this object's
  // connection to the RPC interface to which the implementation is adapting.
  virtual const std::string& GetRpcConnectionIdentifier() const = 0;
};

// These are the functions that a Service adaptor must support
class ServiceAdaptorInterface {
 public:
  virtual ~ServiceAdaptorInterface() {}

  // Getter for the opaque identifier that represents this object on the
  // RPC interface to which the implementation is adapting.
  virtual const std::string& GetRpcIdentifier() const = 0;

  virtual void EmitBoolChanged(const std::string& name, bool value) = 0;
  virtual void EmitUint8Changed(const std::string& name, uint8_t value) = 0;
  virtual void EmitUint16Changed(const std::string& name, uint16_t value) = 0;
  virtual void EmitUint16sChanged(const std::string& name,
                                  const Uint16s& value) = 0;
  virtual void EmitUintChanged(const std::string& name, uint32_t value) = 0;
  virtual void EmitIntChanged(const std::string& name, int value) = 0;
  virtual void EmitRpcIdentifierChanged(const std::string& name,
                                        const std::string& value) = 0;
  virtual void EmitStringChanged(const std::string& name,
                                 const std::string& value) = 0;
  virtual void EmitStringmapChanged(const std::string& name,
                                    const Stringmap& value) = 0;
};

class ThirdPartyVpnAdaptorInterface {
 public:
  virtual ~ThirdPartyVpnAdaptorInterface() {}

  virtual void EmitPacketReceived(const std::vector<uint8_t>& packet) = 0;

  virtual void EmitPlatformMessage(uint32_t message) = 0;
};

}  // namespace shill

#endif  // SHILL_ADAPTOR_INTERFACES_H_
