// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_UDEV_DEVICE_H_
#define MIST_UDEV_DEVICE_H_

#include <stdint.h>
#include <sys/types.h>

#include <base/macros.h>

#include "mist/udev_list_entry.h"

struct udev_device;

namespace mist {

// A udev device, which wraps a udev_device C struct from libudev and related
// library functions into a C++ object.
class UdevDevice {
 public:
  // Constructs a UdevDevice object by taking a raw pointer to a udev_device
  // struct as |device|. The ownership of |device| is not transferred, but its
  // reference count is increased by one during the lifetime of this object.
  explicit UdevDevice(udev_device* device);

  // Destructs this UdevDevice object and decreases the reference count of the
  // underlying udev_device struct by one.
  virtual ~UdevDevice();

  // Wraps udev_device_get_parent(). The returned UdevDevice object is not
  // managed and should be deleted by the caller after use.
  virtual UdevDevice* GetParent() const;

  // Wraps udev_device_get_parent_with_subsystem_devtype(). The returned
  // UdevDevice object is not managed and should be deleted by the caller after
  // use.
  virtual UdevDevice* GetParentWithSubsystemDeviceType(
      const char* subsystem, const char* device_type) const;

  // Wraps udev_device_get_is_initialized().
  virtual bool IsInitialized() const;

  // Wraps udev_device_get_usec_since_initialized().
  virtual uint64_t GetMicrosecondsSinceInitialized() const;

  // Wraps udev_device_get_seqnum().
  virtual uint64_t GetSequenceNumber() const;

  // Wraps udev_device_get_devpath().
  virtual const char* GetDevicePath() const;

  // Wraps udev_device_get_devnode().
  virtual const char* GetDeviceNode() const;

  // Wraps udev_device_get_devnum().
  virtual dev_t GetDeviceNumber() const;

  // Wraps udev_device_get_devtype().
  virtual const char* GetDeviceType() const;

  // Wraps udev_device_get_driver().
  virtual const char* GetDriver() const;

  // Wraps udev_device_get_subsystem().
  virtual const char* GetSubsystem() const;

  // Wraps udev_device_get_syspath().
  virtual const char* GetSysPath() const;

  // Wraps udev_device_get_sysname().
  virtual const char* GetSysName() const;

  // Wraps udev_device_get_sysnum().
  virtual const char* GetSysNumber() const;

  // Wraps udev_device_get_action().
  virtual const char* GetAction() const;

  // Wraps udev_device_get_devlinks_list_entry(). The returned UdevListEntry
  // object is not managed and should be deleted by the caller after use.
  virtual UdevListEntry* GetDeviceLinksListEntry() const;

  // Wraps udev_device_get_properties_list_entry(). The returned UdevListEntry
  // object is not managed and should be deleted by the caller after use.
  virtual UdevListEntry* GetPropertiesListEntry() const;

  // Wraps udev_device_get_property_value().
  virtual const char* GetPropertyValue(const char* key) const;

  // Wraps udev_device_get_tags_list_entry(). The returned UdevListEntry
  // object is not managed and should be deleted by the caller after use.
  virtual UdevListEntry* GetTagsListEntry() const;

  // Wraps udev_device_get_sysattr_list_entry(). The returned UdevListEntry
  // object is not managed and should be deleted by the caller after use.
  virtual UdevListEntry* GetSysAttributeListEntry() const;

  // Wraps udev_device_get_sysattr_value().
  virtual const char* GetSysAttributeValue(const char* attribute) const;

 private:
  // Allows MockUdevDevice to invoke the private default constructor below.
  friend class MockUdevDevice;

  // Constructs a UdevDevice object without referencing a udev_device struct,
  // which is only allowed to be called by MockUdevDevice.
  UdevDevice();

  udev_device* device_;

  DISALLOW_COPY_AND_ASSIGN(UdevDevice);
};

}  // namespace mist

#endif  // MIST_UDEV_DEVICE_H_
