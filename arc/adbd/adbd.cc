/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/macros.h>
#include <brillo/flag_helper.h>

namespace {

constexpr base::FilePath::CharType kRuntimePath[] =
    FILE_PATH_LITERAL("/run/arc/adbd");
constexpr base::FilePath::CharType kConfigFSPath[] =
    FILE_PATH_LITERAL("/dev/config");
constexpr base::FilePath::CharType kFunctionFSPath[] =
    FILE_PATH_LITERAL("/dev/usb-ffs/adb");

// The shifted u/gid of the shell user, used by Android's adbd.
constexpr uid_t kShellUgid = 657360;

// The blob that is sent to FunctionFS to setup the adb gadget. This works for
// newer kernels (>=3.18). This and the following blobs were created by
// https://android.googlesource.com/platform/system/core/+/master/adb/daemon/usb.cpp
constexpr const uint8_t kControlPayloadV2[] = {
    0x03, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x42, 0x01,
    0x01, 0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x07, 0x05, 0x82, 0x02,
    0x40, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x42, 0x01, 0x01,
    0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00, 0x07, 0x05, 0x82, 0x02, 0x00,
    0x02, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x42, 0x01, 0x01, 0x07,
    0x05, 0x01, 0x02, 0x00, 0x04, 0x00, 0x06, 0x30, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x04, 0x00, 0x06, 0x30, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x23, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// The blob that is sent to FunctionFS to setup the adb gadget. This works
// for older kernels.
constexpr const uint8_t kControlPayloadV1[] = {
    0x01, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02, 0xFF,
    0x42, 0x01, 0x01, 0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x07,
    0x05, 0x82, 0x02, 0x40, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02,
    0xFF, 0x42, 0x01, 0x01, 0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00};

// The blob that is sent to FunctionFS to setup the name of the gadget. It is
// "ADB Interface".
constexpr const uint8_t kControlStrings[] = {
    0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x09, 0x04, 0x41, 0x44, 0x42, 0x20,
    0x49, 0x6E, 0x74, 0x65, 0x72, 0x66, 0x61, 0x63, 0x65, 0x00};

// Returns the name of the UDC driver that is available in the system, or an
// empty string if none are available.
std::string GetUDCDriver() {
  base::FileEnumerator udc_enum(
      base::FilePath("/sys/class/udc/"), false /* recursive */,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS);
  const base::FilePath name = udc_enum.Next();
  if (name.empty())
    return std::string();
  // We expect to only have one UDC driver in the system, so we can just return
  // the first file in the directory.
  return name.BaseName().value();
}

// Writes a string to a file. Returns false if the full string was not able to
// be written.
bool WriteFile(const base::FilePath& filename, const std::string& contents) {
  int bytes_written =
      base::WriteFile(filename, contents.c_str(), contents.size());
  if (bytes_written == -1) {
    PLOG(ERROR) << "Failed to write '" << contents << "' to "
                << filename.value();
    return false;
  }
  if (bytes_written < contents.size()) {
    LOG(ERROR) << "Truncated write '" << contents << "' to "
               << filename.value();
    return false;
  }
  return true;
}

// Sets up the ConfigFS files to be able to use the ADB gadget. The
// |serialnumber| parameter is used to setup how the device appears in "adb
// devices".
bool SetupConfigFS(const std::string& serialnumber) {
  const base::FilePath configfs_directory(kConfigFSPath);
  if (!base::CreateDirectory(configfs_directory)) {
    PLOG(ERROR) << "Failed to create " << configfs_directory.value();
    return false;
  }
  if (mount("configfs", configfs_directory.value().c_str(), "configfs",
            MS_NOEXEC | MS_NOSUID | MS_NODEV, nullptr) == -1) {
    PLOG(ERROR) << "Failed to mount configfs";
    return false;
  }

  // Setup the gadget.
  const base::FilePath gadget_path = configfs_directory.Append("usb_gadget/g1");
  if (!base::CreateDirectory(gadget_path.Append("functions/ffs.adb"))) {
    PLOG(ERROR) << "Failed to create ffs.adb directory";
    return false;
  }
  if (!base::CreateDirectory(gadget_path.Append("configs/b.1/strings/0x409"))) {
    PLOG(ERROR) << "Failed to create configs/b.1/strings directory";
    return false;
  }
  if (!base::CreateDirectory(gadget_path.Append("strings/0x409"))) {
    PLOG(ERROR) << "Failed to create config strings directory";
    return false;
  }
  const base::FilePath function_symlink_path =
      gadget_path.Append("configs/b.1/f1");
  if (!base::PathExists(function_symlink_path)) {
    if (!base::CreateSymbolicLink(gadget_path.Append("functions/ffs.adb"),
                                  function_symlink_path)) {
      PLOG(ERROR) << "Failed to create symbolic link";
      return false;
    }
  }
  if (!WriteFile(gadget_path.Append("idVendor"), "0x18d1"))
    return false;
  if (!WriteFile(gadget_path.Append("idProduct"), "0x4ee7"))
    return false;
  if (!WriteFile(gadget_path.Append("strings/0x409/serialnumber"),
                 serialnumber)) {
    return false;
  }
  if (!WriteFile(gadget_path.Append("strings/0x409/manufacturer"), "google"))
    return false;
  if (!WriteFile(gadget_path.Append("strings/0x409/product"), "Cheets"))
    return false;
  if (!WriteFile(gadget_path.Append("configs/b.1/MaxPower"), "500"))
    return false;

  return true;
}

// Bind-mounts a file located in |source| to |target|. It also makes it be owned
// and only writable by Android shell.
bool BindMountFile(const base::FilePath& source, const base::FilePath& target) {
  if (!base::PathExists(target)) {
    base::ScopedFD target_file(
        open(target.value().c_str(), O_WRONLY | O_CREAT, 0600));
    if (!target_file.is_valid()) {
      PLOG(ERROR) << "Failed to touch " << target.value();
      return false;
    }
  }
  if (chown(source.value().c_str(), kShellUgid, kShellUgid) == -1) {
    PLOG(ERROR) << "Failed to chown " << source.value()
                << " to Android's shell user";
    return false;
  }
  if (mount(source.value().c_str(), target.value().c_str(), nullptr, MS_BIND,
            nullptr) == -1) {
    PLOG(ERROR) << "Failed to mount " << target.value();
    return false;
  }
  return true;
}

// Sets up FunctionFS and returns an open FD to the control endpoint of the
// fully setup ADB gadget. The gadget will be torn down if the FD is closed when
// this program exits.
base::ScopedFD SetupFunctionFS(const std::string& udc_driver_name) {
  const base::FilePath functionfs_path(kFunctionFSPath);

  // Create the FunctionFS mount.
  if (!base::CreateDirectory(functionfs_path)) {
    PLOG(ERROR) << "Failed to create " << functionfs_path.value();
    return base::ScopedFD();
  }
  if (mount("adb", functionfs_path.value().c_str(), "functionfs",
            MS_NOEXEC | MS_NOSUID | MS_NODEV, nullptr) == -1) {
    PLOG(ERROR) << "Failed to mount functionfs";
    return base::ScopedFD();
  }

  // Send the configuration to the real control endpoint.
  base::ScopedFD control_file(
      open(functionfs_path.Append("ep0").value().c_str(), O_WRONLY));
  if (!control_file.is_valid()) {
    PLOG(ERROR) << "Failed to open control file";
    return base::ScopedFD();
  }
  if (!base::WriteFileDescriptor(
          control_file.get(), reinterpret_cast<const char*>(kControlPayloadV2),
          sizeof(kControlPayloadV2))) {
    PLOG(WARNING) << "Failed to write the V2 control payload, "
                     "trying to write the V1 control payload";
    if (!base::WriteFileDescriptor(
            control_file.get(),
            reinterpret_cast<const char*>(kControlPayloadV1),
            sizeof(kControlPayloadV1))) {
      PLOG(ERROR) << "Failed to write the V1 control payload";
      return base::ScopedFD();
    }
  }
  if (!base::WriteFileDescriptor(control_file.get(),
                                 reinterpret_cast<const char*>(kControlStrings),
                                 sizeof(kControlStrings))) {
    PLOG(ERROR) << "Failed to write the control strings";
    return base::ScopedFD();
  }
  if (!WriteFile(base::FilePath("/dev/config/usb_gadget/g1/UDC"),
                 udc_driver_name)) {
    return base::ScopedFD();
  }

  // Bind-mount the bulk-in/bulk-out endpoints into the shared mount.
  const base::FilePath runtime_path(kRuntimePath);
  for (const auto& endpoint : {"ep1", "ep2"}) {
    if (!BindMountFile(functionfs_path.Append(endpoint),
                       runtime_path.Append(endpoint))) {
      return base::ScopedFD();
    }
  }

  return control_file;
}

// Creates a FIFO at |path|, owned and only writable by the Android shell user.
bool CreatePipe(const base::FilePath& path) {
  // Create the FIFO at a temporary path. We will call rename(2) later to make
  // the whole operation atomic.
  const base::FilePath tmp_path = path.AddExtension(".tmp");
  if (unlink(tmp_path.value().c_str()) == -1 && errno != ENOENT) {
    PLOG(ERROR) << "Failed to remove stale FIFO at " << tmp_path.value();
    return false;
  }
  if (mkfifo(tmp_path.value().c_str(), 0600) == -1) {
    PLOG(ERROR) << "Failed to create FIFO at " << tmp_path.value();
    return false;
  }
  // base::Unretained is safe since the closure will be run before |tmp_path|
  // goes out of scope.
  base::ScopedClosureRunner unlink_fifo(base::Bind(
      base::IgnoreResult(&unlink), base::Unretained(tmp_path.value().c_str())));
  if (chown(tmp_path.value().c_str(), kShellUgid, kShellUgid) == -1) {
    PLOG(ERROR) << "Failed to chown FIFO at " << tmp_path.value()
                << " to Android's shell user";
    return false;
  }
  if (rename(tmp_path.value().c_str(), path.value().c_str()) == -1) {
    PLOG(ERROR) << "Failed to rename FIFO at " << tmp_path.value() << " to "
                << path.value();
    return false;
  }
  ignore_result(unlink_fifo.Release());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  DEFINE_string(serialnumber, "", "Serial number of the Android container");

  base::AtExitManager at_exit;

  brillo::FlagHelper::Init(argc, argv, "ADB over USB proxy.");
  logging::InitLogging(logging::LoggingSettings());

  const base::FilePath runtime_path(kRuntimePath);

  const std::string udc_driver_name = GetUDCDriver();
  if (udc_driver_name.empty()) {
    LOG(INFO)
        << "Unable to find any registered UDC drivers in /sys/class/udc/. "
        << "This device does not support ADB using GadgetFS.";
    return 0;
  }

  const base::FilePath control_pipe_path = runtime_path.Append("ep0");
  if (!CreatePipe(control_pipe_path))
    return 1;

  char buffer[4096];

  bool configured = false;
  base::ScopedFD control_file;
  while (true) {
    LOG(INFO) << "arc-adbd ready to receive connections";
    // O_RDONLY on a FIFO waits until another endpoint has opened the file with
    // O_WRONLY or O_RDWR.
    base::ScopedFD control_pipe(
        open(control_pipe_path.value().c_str(), O_RDONLY));
    if (!control_pipe.is_valid()) {
      PLOG(ERROR) << "Failed to open FIFO at " << control_pipe_path.value();
      return 1;
    }
    LOG(INFO) << "arc-adbd connected";

    // Given that a FIFO can be opened by multiple processes, once a process has
    // opened it, we atomically replace it with a new FIFO (by using rename(2))
    // so no other process can open it. This causes that when that process
    // close(2)s the FD, we will get an EOF when we attempt to read(2) from it.
    // This also causes any other process that attempts to open the new FIFO to
    // block until we are done processing the current one.
    //
    // There is a very small chance there is a race here if multiple processes
    // get to open the FIFO between the point in time where this process opens
    // the FIFO and CreatePipe() returns. That seems unavoidable, but should not
    // present too much of a problem since exactly one process in Android has
    // the correct user to open this file in the first place (adbd).
    if (!CreatePipe(control_pipe_path))
      return 1;

    // Once adbd has opened the control pipe, we set up the adb gadget on behalf
    // of that process, if we have not already.
    if (!configured) {
      if (!SetupConfigFS(FLAGS_serialnumber)) {
        LOG(ERROR) << "Failed to configure ConfigFS";
        return 1;
      }
      control_file = SetupFunctionFS(udc_driver_name);
      if (!control_file.is_valid()) {
        LOG(ERROR) << "Failed to configure FunctionFS";
        return 1;
      }

      configured = true;
    }

    // Drain the FIFO and wait until the other side closes it.
    // The data that is sent is kControlPayloadV2 (or kControlPayloadV1)
    // followed by kControlStrings. We ignore it completely since we have
    // already sent it to the underlying FunctionFS file, and also to avoid
    // parsing it to decrease the attack surface area.
    while (true) {
      ssize_t bytes_read = read(control_pipe.get(), buffer, sizeof(buffer));
      if (bytes_read < 0)
        PLOG(ERROR) << "Failed to read from FIFO";
      if (bytes_read <= 0)
        break;
    }
  }

  return 0;
}