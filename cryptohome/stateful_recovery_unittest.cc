// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/stateful_recovery.h"

#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_service.h"

namespace cryptohome {
using std::string;
using std::ostringstream;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrEq;
using ::testing::SetArgPointee;

TEST(StatefulRecovery, ValidRequestV1) {
  MockPlatform platform;
  MockService service;
  std::string flag_content = "1";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(false));
  EXPECT_CALL(platform, StatVFS(StatefulRecovery::kRecoverSource, _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, WriteStringToFile(StatefulRecovery::kRecoverBlockUsage,
                                          _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform,
              ReportFilesystemDetails(StatefulRecovery::kRecoverSource,
                StatefulRecovery::kRecoverFilesystemDetails))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, Copy(StatefulRecovery::kRecoverSource,
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_TRUE(recovery.Recover());
}

TEST(StatefulRecovery, ValidRequestV1WriteProtected) {
  MockPlatform platform;
  MockService service;
  std::string flag_content = "1";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(true));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_FALSE(recovery.Recover());
}

TEST(StatefulRecovery, ValidRequestV2) {
  MockPlatform platform;
  MockService service;
  gboolean result = true;
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  std::string mount_path = "/home/.shadow/hashhashash/mount";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(service, Mount(StrEq(user), StrEq(passkey), false, false,
                             _, _, _))
    .WillOnce(DoAll(SetArgPointee<5>(result), Return(true)));
  EXPECT_CALL(service, GetMountPointForUser(StrEq(user), _))
    .WillOnce(DoAll(SetArgPointee<1>(mount_path), Return(true)));
  EXPECT_CALL(platform, Copy(StrEq(mount_path),
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(service, UnmountForUser(StrEq(user), _, _))
    .WillOnce(DoAll(SetArgPointee<1>(result), Return(true)));

  EXPECT_CALL(service, IsOwner(_))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(true));

  // CopyPartitionInfo
  EXPECT_CALL(platform, StatVFS(StatefulRecovery::kRecoverSource, _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, WriteStringToFile(StatefulRecovery::kRecoverBlockUsage,
                                          _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform,
              ReportFilesystemDetails(StatefulRecovery::kRecoverSource,
                StatefulRecovery::kRecoverFilesystemDetails))
    .WillOnce(Return(true));

  // CopyPartitionContents
  EXPECT_CALL(platform, Copy(StatefulRecovery::kRecoverSource,
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_TRUE(recovery.Recover());
}

TEST(StatefulRecovery, ValidRequestV2NotOwner) {
  MockPlatform platform;
  MockService service;
  gboolean result = true;
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  std::string mount_path = "/home/.shadow/hashhashash/mount";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(service, Mount(StrEq(user), StrEq(passkey), false, false,
                             _, _, _))
    .WillOnce(DoAll(SetArgPointee<5>(result), Return(true)));
  EXPECT_CALL(service, GetMountPointForUser(StrEq(user), _))
    .WillOnce(DoAll(SetArgPointee<1>(mount_path), Return(true)));
  EXPECT_CALL(platform, Copy(StrEq(mount_path),
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(service, UnmountForUser(StrEq(user), _, _))
    .WillOnce(DoAll(SetArgPointee<1>(result), Return(true)));

  EXPECT_CALL(service, IsOwner(_))
    .WillOnce(Return(false));
  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(true));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_TRUE(recovery.Recover());
}

TEST(StatefulRecovery, ValidRequestV2BadUser) {
  MockPlatform platform;
  MockService service;
  gboolean result = true;
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  std::string mount_path = "/home/.shadow/hashhashash/mount";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(service, Mount(StrEq(user), StrEq(passkey), false, false,
                             _, _, _))
    .WillOnce(DoAll(SetArgPointee<5>(result), Return(false)));

  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(true));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_FALSE(recovery.Recover());
}

TEST(StatefulRecovery, ValidRequestV2BadUserNotWriteProtected) {
  MockPlatform platform;
  MockService service;
  gboolean result = true;
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  std::string mount_path = "/home/.shadow/hashhashash/mount";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(service, Mount(StrEq(user), StrEq(passkey), false, false,
                             _, _, _))
    .WillOnce(DoAll(SetArgPointee<5>(result), Return(false)));

  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(false));

  // CopyPartitionInfo
  EXPECT_CALL(platform, StatVFS(StatefulRecovery::kRecoverSource, _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, WriteStringToFile(StatefulRecovery::kRecoverBlockUsage,
                                          _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform,
              ReportFilesystemDetails(StatefulRecovery::kRecoverSource,
                StatefulRecovery::kRecoverFilesystemDetails))
    .WillOnce(Return(true));

  // CopyPartitionContents
  EXPECT_CALL(platform, Copy(StatefulRecovery::kRecoverSource,
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_TRUE(recovery.Recover());
}

TEST(StatefulRecovery, ValidRequestV2NotOwnerNotWriteProtected) {
  MockPlatform platform;
  MockService service;
  gboolean result = true;
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  std::string mount_path = "/home/.shadow/hashhashash/mount";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(service, Mount(StrEq(user), StrEq(passkey), false, false,
                             _, _, _))
    .WillOnce(DoAll(SetArgPointee<5>(result), Return(true)));
  EXPECT_CALL(service, GetMountPointForUser(StrEq(user), _))
    .WillOnce(DoAll(SetArgPointee<1>(mount_path), Return(true)));
  EXPECT_CALL(platform, Copy(StrEq(mount_path),
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(service, UnmountForUser(StrEq(user), _, _))
    .WillOnce(DoAll(SetArgPointee<1>(result), Return(true)));

  EXPECT_CALL(service, IsOwner(_))
    .WillOnce(Return(false));
  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(false));

  // CopyPartitionInfo
  EXPECT_CALL(platform, StatVFS(StatefulRecovery::kRecoverSource, _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, WriteStringToFile(StatefulRecovery::kRecoverBlockUsage,
                                          _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform,
              ReportFilesystemDetails(StatefulRecovery::kRecoverSource,
                StatefulRecovery::kRecoverFilesystemDetails))
    .WillOnce(Return(true));

  // CopyPartitionContents
  EXPECT_CALL(platform, Copy(StatefulRecovery::kRecoverSource,
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_TRUE(recovery.Recover());
}

TEST(StatefulRecovery, InvalidFlagFileContents) {
  MockPlatform platform;
  MockService service;
  std::string flag_content = "0 hello";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  StatefulRecovery recovery(&platform, &service);
  EXPECT_FALSE(recovery.Requested());
  EXPECT_FALSE(recovery.Recover());
}

TEST(StatefulRecovery, UnreadableFlagFile) {
  MockPlatform platform;
  MockService service;
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(Return(false));
  StatefulRecovery recovery(&platform, &service);
  EXPECT_FALSE(recovery.Requested());
  EXPECT_FALSE(recovery.Recover());
}

TEST(StatefulRecovery, UncopyableData) {
  MockPlatform platform;
  MockService service;
  std::string flag_content = "1";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(false));
  EXPECT_CALL(platform, Copy(StatefulRecovery::kRecoverSource,
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(false));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_FALSE(recovery.Recover());
}

TEST(StatefulRecovery, DirectoryCreationFailure) {
  MockPlatform platform;
  MockService service;
  std::string flag_content = "1";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(false));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_FALSE(recovery.Recover());
}

TEST(StatefulRecovery, StatVFSFailure) {
  MockPlatform platform;
  MockService service;
  std::string flag_content = "1";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(false));
  EXPECT_CALL(platform, Copy(StatefulRecovery::kRecoverSource,
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, StatVFS(StatefulRecovery::kRecoverSource, _))
    .WillOnce(Return(false));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_FALSE(recovery.Recover());
}

TEST(StatefulRecovery, FilesystemDetailsFailure) {
  MockPlatform platform;
  MockService service;
  std::string flag_content = "1";
  EXPECT_CALL(platform, ReadFileToString(StatefulRecovery::kFlagFile, _))
    .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(platform, CreateDirectory(StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, FirmwareWriteProtected())
    .WillOnce(Return(false));
  EXPECT_CALL(platform, Copy(StatefulRecovery::kRecoverSource,
                             StatefulRecovery::kRecoverDestination))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, StatVFS(StatefulRecovery::kRecoverSource, _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform, WriteStringToFile(StatefulRecovery::kRecoverBlockUsage,
                                          _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform,
              ReportFilesystemDetails(StatefulRecovery::kRecoverSource,
                StatefulRecovery::kRecoverFilesystemDetails))
    .WillOnce(Return(false));

  StatefulRecovery recovery(&platform, &service);
  EXPECT_TRUE(recovery.Requested());
  EXPECT_FALSE(recovery.Recover());
}

TEST(StatefulRecovery, MountsParseOk) {
  Platform platform;
  base::FilePath mtab;
  FILE *fp;
  std::string filesystem, device_in, device_out, mtab_path, mtab_contents;

  filesystem = "/second/star/to/the/right";
  device_in = "/dev/pan";

  mtab_contents.append(device_in);
  mtab_contents.append(" ");
  mtab_contents.append(filesystem);
  mtab_contents.append(" pixie default 0 0\n");

  fp = base::CreateAndOpenTemporaryFile(&mtab);
  ASSERT_TRUE(fp != NULL);
  EXPECT_EQ(fwrite(mtab_contents.c_str(), mtab_contents.length(), 1, fp), 1);
  EXPECT_EQ(fclose(fp), 0);

  mtab_path = mtab.value();
  platform.set_mtab_path(mtab_path);

  /* Fails if item is missing. */
  EXPECT_FALSE(platform.FindFilesystemDevice("monkey", &device_out));

  /* Works normally. */
  device_out.clear();
  EXPECT_TRUE(platform.FindFilesystemDevice(filesystem, &device_out));
  EXPECT_TRUE(device_out == device_in);

  /* Trailing slashes are okay. */
  filesystem += "///";
  device_out.clear();
  EXPECT_TRUE(platform.FindFilesystemDevice(filesystem, &device_out));
  EXPECT_TRUE(device_out == device_in);

  /* Clean up. */
  EXPECT_TRUE(base::DeleteFile(mtab, false));
}

TEST(StatefulRecovery, UsageReportOk) {
  Platform platform;

  struct statvfs vfs;
  /* Reporting on a valid location produces output. */
  EXPECT_TRUE(platform.StatVFS("/", &vfs));
  EXPECT_NE(vfs.f_blocks, 0);

  /* Reporting on an invalid location fails. */
  EXPECT_FALSE(platform.StatVFS("/this/is/very/wrong", &vfs));

  /* TODO(keescook): mockable tune2fs, since it's not installed in chroot. */
}

}  // namespace cryptohome
