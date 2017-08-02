// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hammerd/usb_utils.h"

#include <stdio.h>

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

namespace hammerd {
namespace {
constexpr int kError = -1;
constexpr unsigned int kTimeoutMs = 1000;  // Default timeout value.
bool is_libusb_inited = false;
}  // namespace

bool InitLibUSB() {
  DLOG(INFO) << "Call InitLibUSB";
  if (is_libusb_inited) {
    DLOG(INFO) << "libusb is already initialized. Ignored.";
    return true;
  }
  int r = libusb_init(nullptr);
  if (r < 0) {
    LogUSBError("libusb_init", r);
    return false;
  }
  is_libusb_inited = true;
  return true;
}

void ExitLibUSB() {
  DLOG(INFO) << "Call ExitLibUSB";
  if (!is_libusb_inited) {
    LOG(INFO) << "libusb is not initialized. Ignored.";
    return;
  }
  libusb_exit(nullptr);
  is_libusb_inited = false;
}

void LogUSBError(const char* func_name, int return_code) {
  LOG(ERROR) << func_name << " returned " << return_code << " ("
             << libusb_strerror(static_cast<libusb_error>(return_code)) << ")";
}

UsbEndpoint::UsbEndpoint(uint16_t vendor_id, uint16_t product_id)
    : UsbEndpoint(vendor_id, product_id, -1, -1) {}

UsbEndpoint::UsbEndpoint(uint16_t vendor_id, uint16_t product_id,
                         int bus, int port)
    : vendor_id_(vendor_id), product_id_(product_id), bus_(bus), port_(port),
      devh_(nullptr), iface_num_(-1), ep_num_(-1), chunk_len_(0) {}

UsbEndpoint::~UsbEndpoint() {
  Close();
  ExitLibUSB();
}

bool UsbEndpoint::Connect() {
  // NOTE: If multiple interfaces matched the VID/PID, then we connect
  // to the first one found, and ignore others.
  if (IsConnected()) {
    LOG(INFO) << "Already initialized. Ignore.";
    return true;
  }

  InitLibUSB();
  LOG(INFO) << base::StringPrintf(
      "open_device %x:%x", vendor_id_, product_id_);
  // TODO(kitching): We should use libusb_get_device_list for device
  //                 enumeration instead, looking to see which device is
  //                 on the particular bus/port.
  devh_ = libusb_open_device_with_vid_pid(nullptr, vendor_id_, product_id_);
  if (!devh_) {
    LOG(ERROR) << "Can't find device.";
    Close();
    return false;
  }

  LOG(INFO) << "Checking for bus " << bus_ << " port " << port_ << "...";
  libusb_device* dev = libusb_get_device(devh_);
  int bus = libusb_get_bus_number(dev);
  int port = libusb_get_port_number(dev);

  if ((bus_ != -1 && bus != bus_) || (port_ != -1 && port != port_)) {
    LOG(ERROR) << "Invalid bus " << bus << " and port " << port << ".";
    Close();
    return false;
  }

  iface_num_ = FindInterface();
  if (iface_num_ < 0) {
    LOG(ERROR) << "USB FW update not supported by that device";
    Close();
    return false;
  }
  if (!chunk_len_) {
    LOG(ERROR) << "wMaxPacketSize isn't valid";
    Close();
    return false;
  }

  LOG(INFO) << "found interface " << iface_num_ << ", endpoint "
            << static_cast<int>(ep_num_) << ", chunk_len " << chunk_len_;

  libusb_set_auto_detach_kernel_driver(devh_, 1);
  int r = libusb_claim_interface(devh_, iface_num_);
  if (r < 0) {
    LogUSBError("libusb_claim_interface", r);
    Close();
    return false;
  }
  LOG(INFO) << "USB endpoint is initialized successfully.";
  return true;
}

// Release USB device.
void UsbEndpoint::Close() {
  if (iface_num_ >= 0) {
    libusb_release_interface(devh_, iface_num_);
  }
  if (devh_ != nullptr) {
    libusb_close(devh_);
    devh_ = nullptr;
  }
  iface_num_ = -1;
  ep_num_ = -1;
  chunk_len_ = 0;
}

bool UsbEndpoint::IsConnected() const {
  return (devh_ != nullptr);
}

int UsbEndpoint::Transfer(const void* outbuf,
                          int outlen,
                          void* inbuf,
                          int inlen,
                          bool allow_less,
                          unsigned int timeout_ms) {
  if (Send(outbuf, outlen, timeout_ms) != outlen) {
    return kError;
  }
  if (inlen == 0) {
    return 0;
  }
  return Receive(inbuf, inlen, allow_less, timeout_ms);
}

int UsbEndpoint::Send(const void* outbuf, int outlen, unsigned int timeout_ms) {
  // BulkTransfer() does not modify the buffer while using LIBUSB_ENDPOINT_OUT
  // direction mask.
  int actual = BulkTransfer(
      const_cast<void*>(outbuf), LIBUSB_ENDPOINT_OUT, outlen, timeout_ms);
  DLOG(INFO) << "Sent " << actual << "/" << outlen << " bytes";
  if (actual != outlen) {
    LOG(ERROR) << "Failed to send the complete data.";
  }
  return actual;
}

int UsbEndpoint::Receive(void* inbuf,
                         int inlen,
                         bool allow_less,
                         unsigned int timeout_ms) {
  int actual = BulkTransfer(inbuf, LIBUSB_ENDPOINT_IN, inlen, timeout_ms);
  DLOG(INFO) << "Received " << actual << "/" << inlen << " bytes";
  if ((actual != inlen) && !allow_less) {
    LOG(ERROR) << "Failed to receive the complete data.";
    return kError;
  }
  return actual;
}

// Adapted from platform/mist/usb_device.h.
std::string UsbEndpoint::GetStringDescriptorAscii(uint8_t index) {
  if (!devh_) {
    LOG(ERROR) << "devh_ is nullptr.";
    return std::string();
  }

  // libusb_get_string_descriptor_ascii uses an internal buffer that can only
  // hold up to 128 ASCII characters.
  uint8_t data[128];
  int r = libusb_get_string_descriptor_ascii(devh_, index, data, sizeof(data));
  if (r < 0) {
    LogUSBError("libusb_get_string_descriptor", r);
    return std::string();
  }

  return std::string(reinterpret_cast<const char*>(data), r);
}

int UsbEndpoint::FindInterface() {
  if (!devh_) {
    LOG(ERROR) << "devh_ is nullptr.";
    return kError;
  }

  libusb_device* dev = libusb_get_device(devh_);
  libusb_config_descriptor* conf = nullptr;
  int r = libusb_get_active_config_descriptor(dev, &conf);
  if (r < 0) {
    LogUSBError("libusb_get_active_config_descriptor", r);
    libusb_free_config_descriptor(conf);
    return kError;
  }

  int iface_num = kError;
  for (int i = 0; i < conf->bNumInterfaces; i++) {
    if (FindEndpoint(&conf->interface[i]) != kError) {
      iface_num = i;
      break;
    }
  }

  // Store the configuration string value for this device.
  configuration_string_ = GetStringDescriptorAscii(conf->iConfiguration);
  LOG(INFO) << "Configuration string descriptor: " << configuration_string_;

  libusb_free_config_descriptor(conf);
  return iface_num;
}

int UsbEndpoint::FindEndpoint(const libusb_interface* iface) {
  const libusb_interface_descriptor* iface_desc = nullptr;
  for (int iface_idx = 0; iface_idx < iface->num_altsetting; iface_idx++) {
    iface_desc = &iface->altsetting[iface_idx];
    if (iface_desc->bInterfaceClass == 255 &&
        iface_desc->bInterfaceSubClass == kUsbSubclassGoogleUpdate &&
        iface_desc->bInterfaceProtocol == kUsbProtocolGoogleUpdate &&
        iface_desc->bNumEndpoints) {
      const libusb_endpoint_descriptor* ep = &iface_desc->endpoint[0];
      ep_num_ = ep->bEndpointAddress & 0x7f;
      chunk_len_ = ep->wMaxPacketSize;
      return iface_idx;
    }
  }
  return kError;
}

int UsbEndpoint::BulkTransfer(void* buf,
                              enum libusb_endpoint_direction direction_mask,
                              int len,
                              unsigned int timeout_ms) {
  if (timeout_ms == 0) {
    timeout_ms = kTimeoutMs;
  }

  int actual = 0;
  int r = libusb_bulk_transfer(devh_,
                               ep_num_ | direction_mask,
                               reinterpret_cast<uint8_t*>(buf),
                               len,
                               &actual,
                               timeout_ms);
  if (r < 0) {
    LogUSBError("libusb_bulk_transfer", r);
    return kError;
  }
  return actual;
}

}  // namespace hammerd
