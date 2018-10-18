// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/flag_helper.h>

#include "oobe_config/usb_common.h"
#include "oobe_config/utils.h"

using base::FilePath;
using std::string;
using std::vector;

namespace oobe_config {

// Enumerates /dev/disk/by-id/ to find which persistent disk identifier
// |mount_dev| corresponds to.
FilePath FindPersistentMountDevice(const FilePath& mount_dev) {
  base::FileEnumerator by_id(FilePath(kDevDiskById), false,
                             base::FileEnumerator::FILES);
  for (auto link = by_id.Next(); !link.empty(); link = by_id.Next()) {
    // |link| points to something like:
    //   usb-_Some_Memory_<serial>-0:0-part1 -> ../../sdb1
    FilePath target;
    CHECK(base::NormalizeFilePath(link, &target));
    if (target == mount_dev) {
      LOG(INFO) << link.value() << " points to " << target.value();
      return link;
    }
  }

  LOG(ERROR) << "Couldn't find persistent device mapping for "
             << mount_dev.value();
  return FilePath();
}

// Finds the persistent block device that |src_dir| is mounted on, and signs
// the path using |priv_key|, and writes it to |dst_dir|/.../block_device.sig.
void SignSourcePartitionFile(const FilePath& priv_key,
                             const FilePath& src_dev,
                             const FilePath& dst_dir) {
  auto mount_dev = FindPersistentMountDevice(src_dev);
  CHECK(base::PathExists(mount_dev));
  auto disk = mount_dev.value();
  FilePath temp_disk;
  CHECK(base::CreateTemporaryFile(&temp_disk));
  CHECK(base::WriteFile(temp_disk, disk.c_str(), disk.length()) ==
        disk.length());
  CHECK(SignFile(
      priv_key, temp_disk,
      dst_dir.Append(kUnencryptedOobeConfigDir).Append(kUsbDevicePathSigFile)));
}

void SignOobeFiles(const FilePath& priv_key,
                   const FilePath& pub_key,
                   const FilePath& src_stateful,
                   const FilePath& dst_stateful) {
  auto src_config_dir = src_stateful.Append(kUnencryptedOobeConfigDir);
  auto dst_config_dir = dst_stateful.Append(kUnencryptedOobeConfigDir);

  // /stateful/unencrypted/oobe_auto_config might not exist on the target
  // device, so create it here.
  CHECK(base::CreateDirectory(dst_config_dir));

  CHECK(SignFile(priv_key, src_config_dir.Append(kConfigFile),
                 dst_config_dir.Append(kConfigFile).AddExtension("sig")));

  // If the media was provisioned for auto-enrollment, sign the domain name
  // as well.
  if (base::PathExists(src_config_dir.Append(kDomainFile))) {
    CHECK(SignFile(priv_key, src_config_dir.Append(kDomainFile),
                   dst_config_dir.Append(kDomainFile).AddExtension("sig")));
  }
}

}  // namespace oobe_config

int main(int argc, char** argv) {
  DEFINE_string(priv_key, "",
                "Path to private key to sign OOBE auto-configuration with.");
  DEFINE_string(pub_key, "",
                "Path to public key to validate OOBE auto-configuration with.");
  DEFINE_string(src_device, "", "Path to recovery media block device.");
  DEFINE_string(src_stateful, "", "Path to recovery media stateful mount.");
  DEFINE_string(dst_stateful, "", "Path to target device stateful mount.");

  brillo::FlagHelper::Init(
      argc, argv,
      "finish_oobe_auto_config\n"
      "\n"
      "This utility performs OOBE auto-configuration setup in\n"
      "chromeos-installer.\n"
      "\n"
      "It:\n"
      "* creates $dst_stateful/unencrypted/oobe_auto_config/\n"
      "* signs $src_stateful/config.json\n"
      "* if it exists, signs $src_stateful/enrollment_domain\n"
      "* writes public key to target device stateful\n"
      "* determines a persistent block device for $src_device and writes a\n"
      "  digest of it to target device stateful\n");

  FilePath priv_key(FLAGS_priv_key);
  FilePath pub_key(FLAGS_pub_key);
  FilePath src_device(FLAGS_src_device);
  FilePath src_stateful(FLAGS_src_stateful);
  FilePath dst_stateful(FLAGS_dst_stateful);

  CHECK(base::PathExists(priv_key));
  CHECK(base::PathExists(pub_key));
  CHECK(base::PathExists(src_device));
  CHECK(base::DirectoryExists(src_stateful));
  CHECK(base::DirectoryExists(dst_stateful));

  // Generate digests for the configuration and domain files.
  oobe_config::SignOobeFiles(priv_key, pub_key, src_stateful, dst_stateful);

  // Generate digest for the source stateful device name.
  oobe_config::SignSourcePartitionFile(priv_key, src_device, dst_stateful);

  // Copy the public key into the target stateful for use in validation.
  CHECK(base::CopyFile(
      pub_key, dst_stateful.Append(oobe_config::kUnencryptedOobeConfigDir)
                   .Append(oobe_config::kKeyFile)));
  return 0;
}