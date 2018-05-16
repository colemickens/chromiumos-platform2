// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/sshfs_helper.h"

#include <string>
#include <vector>

#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/uri.h"

using std::string;
using std::vector;
using testing::DoAll;
using testing::Eq;
using testing::HasSubstr;
using testing::Not;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;
using testing::_;

namespace cros_disks {

namespace {

const uid_t kMountUID = 200;
const gid_t kMountGID = 201;
const uid_t kFilesUID = 700;
const uid_t kFilesGID = 701;
const uid_t kFilesAccessGID = 1501;
const base::FilePath kWorkingDir("/wkdir");
const base::FilePath kMountDir("/mnt");
const Uri kSomeSource("sshfs", "src");

// Mock Platform implementation for testing.
class MockPlatform : public Platform {
 public:
  MockPlatform() = default;

  bool GetUserAndGroupId(const string& user,
                         uid_t* user_id,
                         gid_t* group_id) const override {
    if (user == "fuse-sshfs") {
      if (user_id)
        *user_id = kMountUID;
      if (group_id)
        *group_id = kMountGID;
      return true;
    }
    if (user == FUSEHelper::kFilesUser) {
      if (user_id)
        *user_id = kFilesUID;
      if (group_id)
        *group_id = kFilesGID;
      return true;
    }
    return false;
  }

  bool GetGroupId(const string& group, gid_t* group_id) const override {
    if (group == FUSEHelper::kFilesGroup) {
      if (group_id)
        *group_id = kFilesAccessGID;
      return true;
    }
    return false;
  }

  MOCK_CONST_METHOD2(CopyFile, bool(const string& src, const string& dst));
  MOCK_CONST_METHOD3(SetOwnership,
                     bool(const string& path, uid_t user_id, gid_t group_id));
  MOCK_CONST_METHOD2(SetPermissions, bool(const string& path, mode_t mode));
};

}  // namespace

class SshfsHelperTest : public ::testing::Test {
 public:
  SshfsHelperTest() : helper_(&platform_) {
    ON_CALL(platform_, SetOwnership(_, kMountUID, getgid()))
        .WillByDefault(Return(true));
    ON_CALL(platform_, SetPermissions(_, 0770)).WillByDefault(Return(true));
  }

 protected:
  MockPlatform platform_;
  SshfsHelper helper_;
};

// Verifies that CreateMounter creates mounter in a simple case.
TEST_F(SshfsHelperTest, CreateMounter_SimpleOptions) {
  auto mounter = helper_.CreateMounter(kWorkingDir, kSomeSource, kMountDir, {});
  EXPECT_EQ("sshfs", mounter->filesystem_type());
  EXPECT_EQ("src", mounter->source_path());
  EXPECT_EQ("/mnt", mounter->target_path());
  string opts = mounter->mount_options().ToString();
  EXPECT_THAT(opts, HasSubstr("BatchMode=yes"));
  EXPECT_THAT(opts, HasSubstr("PasswordAuthentication=no"));
  EXPECT_THAT(opts, HasSubstr("KbdInteractiveAuthentication=no"));
  EXPECT_THAT(opts, HasSubstr("allow_other"));
  EXPECT_THAT(opts, HasSubstr("default_permissions"));
  EXPECT_THAT(opts, HasSubstr("uid=700"));
  EXPECT_THAT(opts, HasSubstr("gid=1501"));
}

// Verifies that CreateMounter copies files to the working dir when provided.
TEST_F(SshfsHelperTest, CreateMounter_CopyFiles) {
  EXPECT_CALL(platform_, CopyFile("/foo/bar", "/wkdir/bar"))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CopyFile("/foo/baz", "/wkdir/baz"))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions("/wkdir/bar", 0600))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions("/wkdir/baz", 0600))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions("/wkdir", 0770)).WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership("/wkdir/bar", kMountUID, kMountGID))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership("/wkdir/baz", kMountUID, kMountGID))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership("/wkdir", kMountUID, getgid()))
      .WillOnce(Return(true));
  auto mounter = helper_.CreateMounter(
      kWorkingDir, kSomeSource, kMountDir,
      {"IdentityFile=/foo/bar", "UserKnownHostsFile=/foo/baz",
       "HostName=localhost", "Port=2222"});
  string opts = mounter->mount_options().ToString();
  EXPECT_THAT(opts, HasSubstr("IdentityFile=/wkdir/bar"));
  EXPECT_THAT(opts, HasSubstr("UserKnownHostsFile=/wkdir/baz"));
  EXPECT_THAT(opts, HasSubstr("HostName=localhost"));
  EXPECT_THAT(opts, HasSubstr("Port=2222"));
  EXPECT_THAT(opts, Not(HasSubstr("/foo/bar")));
  EXPECT_THAT(opts, Not(HasSubstr("/foo/baz")));
}

// Verifies that CreateMounter fails if files are in unsafe format.
TEST_F(SshfsHelperTest, CreateMounter_CopyUnsafe) {
  EXPECT_CALL(platform_, CopyFile(_, _)).Times(0);
  auto mounter = helper_.CreateMounter(
      kWorkingDir, kSomeSource, kMountDir,
      {"IdentityFile=foo/bar", "UserKnownHostsFile=/foo/../baz"});
  EXPECT_FALSE(mounter);
}

// Verifies that CreateMounter fails if fails to copy identity files.
TEST_F(SshfsHelperTest, CreateMounter_CopyFails) {
  EXPECT_CALL(platform_, CopyFile("/foo/bar", "/wkdir/bar"))
      .WillRepeatedly(Return(false));
  auto mounter = helper_.CreateMounter(kWorkingDir, kSomeSource, kMountDir,
                                       {"IdentityFile=/foo/bar"});
  EXPECT_FALSE(mounter);
}

// Verifies that CanMount correctly identifies handleable URIs.
TEST_F(SshfsHelperTest, CanMount) {
  EXPECT_TRUE(helper_.CanMount(Uri::Parse("sshfs://foo")));
  EXPECT_FALSE(helper_.CanMount(Uri::Parse("sshfss://foo")));
  EXPECT_FALSE(helper_.CanMount(Uri::Parse("ssh://foo")));
  EXPECT_FALSE(helper_.CanMount(Uri::Parse("sshfs://")));
}

// Verifies that GetTargetSuffix escapes unwanted chars in URI.
TEST_F(SshfsHelperTest, GetTargetSuffix) {
  EXPECT_EQ("foo", helper_.GetTargetSuffix(Uri::Parse("sshfs://foo")));
  EXPECT_EQ("usr@host_com:",
            helper_.GetTargetSuffix(Uri::Parse("sshfs://usr@host.com:")));
  EXPECT_EQ("host:$some$path$__",
            helper_.GetTargetSuffix(Uri::Parse("sshfs://host:/some/path/..")));
}

}  // namespace cros_disks