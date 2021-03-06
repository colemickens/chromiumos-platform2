// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTIER_LL_ADDRESS_H_
#define PORTIER_LL_ADDRESS_H_

#include <linux/if_packet.h>

#include <string>

#include <shill/net/byte_string.h>

namespace portier {

// Used to describe Link-Layer addresses.
class LLAddress {
 public:
  // Link-Layer types.
  // Add more types should their support be needed.
  enum class Type : uint8_t { kInvalid, kEui48, kEui64 };

  // Static methods for Type.
  static std::string GetTypeName(Type type);
  // Expected byte length of LL address.  Will return -1 if the type is invalid.
  static int32_t GetTypeLength(Type type);

  // Converts the enumerated type into the ARP hardware type recognized by
  // the kernel.
  static uint16_t GetTypeArpType(Type type);

  // Creates an all-zero MAC-48 address.
  LLAddress();

  explicit LLAddress(Type type);
  // Construct from raw byte string.  Byte string must be a valid length
  // for the type.
  LLAddress(Type type, const shill::ByteString& address);

  // Construct from string representation.
  LLAddress(Type type, const std::string& ll_address_string);

  // Construct from Kernel supplied struct.
  explicit LLAddress(const struct sockaddr_ll* address_struct);

  ~LLAddress();

  // Copy and assignment operators.
  LLAddress(const LLAddress& other);
  LLAddress& operator=(const LLAddress& other);

  // Getters.
  Type type() const { return type_; }
  uint16_t GetArpType() const;

  const shill::ByteString& address() const { return address_; }
  const uint8_t* GetConstData() const;
  uint32_t GetLength() const;

  // Bytes provided in constructor create a valid LL address based
  // on the specified type.
  bool IsValid() const;

  // Routing Scheme.  Returns false if unknown or unapplicable for the type.
  bool IsUnicast() const;
  bool IsMulticast() const;
  bool IsBroadcast() const;

  // IEEE 802.3 MAC Address specific.
  bool IsUniversal() const;
  bool IsLocal() const;

  // Converts LL address to its string representation.
  std::string ToString() const;

  bool Equals(const LLAddress& other) const;

 private:
  Type type_;
  shill::ByteString address_;
};  // LLAddress

}  // namespace portier

#endif  // PORTIER_LL_ADDRESS_H_
