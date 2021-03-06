// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb_bouncer/entry_manager_test_util.h"

#include <base/files/file_util.h>
#include <base/logging.h>

#include "usb_bouncer/util.h"

namespace usb_bouncer {

namespace {

constexpr char kUsbguardTestConfigFilename[] = "99-rules.conf";
constexpr char kUsbguardTestConfigFooter[] = "block\n";

bool EntryMapContains(const EntryMap& entries,
                      const std::string& key,
                      const std::string& value) {
  auto entry = entries.find(key);

  if (entry == entries.end()) {
    return false;
  }

  if (entry->second.rules().size() != 1) {
    return false;
  }

  return value == entry->second.rules(0);
}

bool ExpireEntryHelper(EntryMap* entries, const std::string& key) {
  auto entry = entries->find(key);

  if (entry == entries->end()) {
    return false;
  }

  if (entry->second.rules().size() != 1) {
    return false;
  }

  entry->second.mutable_last_used()->clear_seconds();
  return true;
}

}  // namespace

EntryManagerTestUtil::EntryManagerTestUtil() {
  CHECK(scoped_temp_dir_.CreateUniqueTempDir());
  temp_dir_ = scoped_temp_dir_.GetPath();
  CHECK(base::SetPosixFilePermissions(temp_dir_, 0755));

  base::FilePath temp_etc_usbguard =
      CreateTestDir(kUsbguardPolicyDir, true /*force_empty*/);
  CHECK_NE(base::WriteFile(
               temp_etc_usbguard.Append(kUsbguardTestConfigFilename),
               kUsbguardTestConfigFooter, sizeof(kUsbguardTestConfigFooter)),
           -1);

  CreateTestDir(std::string("sys") + kDefaultDevpath, true /*force_empty*/);

  RecreateEntryManager(base::FilePath() /*userdb_dir*/);
}

EntryManager* EntryManagerTestUtil::Get() {
  return entry_manager_.get();
}

void EntryManagerTestUtil::RefreshDB(bool include_user_db, bool new_db) {
  if (new_db && entry_manager_) {
    if (!base::DeleteFile(entry_manager_->global_db_path_, true)) {
      LOG(FATAL) << "Unable to delete \""
                 << entry_manager_->global_db_path_.value() << "\"!";
    }
  }
  if (include_user_db) {
    base::FilePath user_db_dir = CreateTestDir(kUserdbDir, new_db);
    CHECK(!user_db_dir.empty());
    RecreateEntryManager(user_db_dir);
  } else {
    RecreateEntryManager(base::FilePath());
  }
}

void EntryManagerTestUtil::ReplaceDB(const RuleDB& replacement) {
  *entry_manager_->global_entries_ = replacement;
}

void EntryManagerTestUtil::ExpireEntry(bool expect_user,
                                       const std::string& devpath,
                                       const std::string& rule) {
  CHECK(ExpireEntryHelper(entry_manager_->global_entries_->mutable_trash(),
                          Hash(devpath)));

  if (expect_user) {
    CHECK(entry_manager_->user_entries_);
    CHECK(ExpireEntryHelper(entry_manager_->user_entries_->mutable_entries(),
                            Hash(rule)));
  }
}

size_t EntryManagerTestUtil::GarbageCollectInternal(bool global_only) {
  return entry_manager_->GarbageCollectInternal(global_only);
}

bool EntryManagerTestUtil::GlobalDBContainsEntry(const std::string& devpath,
                                                 const std::string& rule) {
  return EntryMapContains(entry_manager_->global_entries_->entries(),
                          Hash(devpath), rule);
}

bool EntryManagerTestUtil::GlobalTrashContainsEntry(const std::string& devpath,
                                                    const std::string& rule) {
  return EntryMapContains(entry_manager_->global_entries_->trash(),
                          Hash(devpath), rule);
}

bool EntryManagerTestUtil::UserDBContainsEntry(const std::string& rule) {
  CHECK(entry_manager_->user_entries_);
  return EntryMapContains(entry_manager_->user_entries_->entries(), Hash(rule),
                          rule);
}

base::FilePath EntryManagerTestUtil::CreateTestDir(const std::string& dir,
                                                   bool force_empty) {
  base::FilePath result = temp_dir_.Append(dir);
  if (force_empty) {
    if (!base::DeleteFile(result, true)) {
      LOG(FATAL) << "Unable to clear directory \"" << result.value() << "\"!";
    }
  }
  base::File::Error error;
  if (!base::CreateDirectoryAndGetError(result, &error)) {
    LOG(FATAL) << "Unable to create temp directory \"" << result.value()
               << "\"!";
  }
  return result;
}

void EntryManagerTestUtil::RecreateEntryManager(
    const base::FilePath& userdb_dir) {
  // std::make_unique was not used because a private constructor was needed.
  entry_manager_ = std::unique_ptr<EntryManager>(
      new EntryManager(temp_dir_.value(), userdb_dir,
                       [](const std::string& devpath) -> std::string {
                         if (devpath.empty()) {
                           return "";
                         }
                         return kDefaultRule;
                       }));
  CHECK(!entry_manager_->global_db_path_.empty());
  CHECK(entry_manager_->global_entries_);
}

}  // namespace usb_bouncer
