// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb_bouncer/entry_manager.h"

#include <base/files/file_enumerator.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "usb_bouncer/util.h"

using base::TimeDelta;

namespace usb_bouncer {

namespace {

constexpr TimeDelta kModeSwitchThreshold = TimeDelta::FromMilliseconds(1000);

constexpr TimeDelta kCleanupThreshold = TimeDelta::FromDays(365 / 4);

constexpr char kDevpathRoot[] = "sys/devices";

}  // namespace

EntryManager* EntryManager::GetInstance() {
  static EntryManager instance;
  if (instance.global_db_path_.empty() || !instance.global_entries_) {
    LOG(ERROR) << "Failed to open global DB.";
    return nullptr;
  }
  return &instance;
}

bool EntryManager::CreateDefaultGlobalDB() {
  base::FilePath db_path;
  return GetDBFromPath(base::FilePath("/").Append(kDefaultGlobalDir),
                       &db_path) != nullptr;
}

EntryManager::EntryManager()
    : EntryManager("/", GetUserDBDir(), GetRuleFromDevPath) {}

EntryManager::EntryManager(const std::string& root_dir,
                           const base::FilePath& user_db_dir,
                           DevpathToRuleCallback rule_from_devpath)
    : root_dir_(root_dir), rule_from_devpath_(rule_from_devpath) {
  global_entries_ =
      GetDBFromPath(root_dir_.Append(kDefaultGlobalDir), &global_db_path_);

  if (!user_db_dir.empty()) {
    user_entries_ = GetDBFromPath(user_db_dir, &user_db_path_);
  }
}

EntryManager::~EntryManager() {}

bool EntryManager::GarbageCollect() {
  size_t num_removed = GarbageCollectInternal(false);

  if (num_removed == 0)
    return true;

  return PersistChanges();
}

std::string EntryManager::GenerateRules() const {
  // Include user specific whitelist rules first so that they take precedence
  // over any blacklist rules.
  std::string result = "";
  if (user_entries_) {
    for (const auto& rule : UniqueRules(user_entries_->entries())) {
      result.append(rule);
      result.append("\n");
    }
  } else {
    for (const auto& rule : UniqueRules(global_entries_->entries())) {
      result.append(rule);
      result.append("\n");
    }
  }

  // Include base set of rules in sorted order.
  std::vector<base::FilePath> rules_d_files;
  base::FileEnumerator file_enumerator(root_dir_.Append(kUsbguardPolicyDir),
                                       false, base::FileEnumerator::FILES);
  base::FilePath next_path;
  while (!(next_path = file_enumerator.Next()).empty()) {
    if (base::EndsWith(next_path.value(), ".conf",
                       base::CompareCase::INSENSITIVE_ASCII)) {
      rules_d_files.push_back(next_path);
    }
  }
  std::sort(rules_d_files.begin(), rules_d_files.end());

  for (const auto& rules_d_file : rules_d_files) {
    std::string contents;
    if (base::ReadFileToString(rules_d_file, &contents)) {
      result.append(contents);
      if (contents.back() != '\n') {
        result.append("\n");
      }
    }
  }
  return result;
}

bool EntryManager::HandleUdev(UdevAction action, const std::string& devpath) {
  if (!ValidateDevPath(devpath)) {
    LOG(ERROR) << "Failed to validate devpath \"" << devpath << "\".";
    return false;
  }

  std::string global_key = Hash(devpath);
  EntryMap* global_map = global_entries_->mutable_entries();

  switch (action) {
    case UdevAction::kAdd: {
      std::string rule = rule_from_devpath_(devpath);
      if (!ValidateRule(rule)) {
        LOG(ERROR) << "Unable convert devpath to USBGuard whitelist rule.";
        return false;
      }

      RuleEntry& entry = (*global_map)[global_key];
      UpdateTimestamp(entry.mutable_last_used());

      // Prepend any mode changes for the same device.
      GarbageCollectInternal(true /*global_only*/);
      const EntryMap& trash = global_entries_->trash();
      auto itr = trash.find(global_key);
      if (itr != trash.end()) {
        for (const auto& previous_mode : itr->second.rules()) {
          if (rule != previous_mode) {
            *entry.mutable_rules()->Add() = previous_mode;
          }
        }
      }

      *entry.mutable_rules()->Add() = rule;
      if (user_entries_) {
        (*user_entries_->mutable_entries())[Hash(entry.rules())] = entry;
      }
      return PersistChanges();
    }
    case UdevAction::kRemove: {
      // We only remove entries from the global db here because it represents
      // whitelist rules for the current state of the system. These entries
      // cannot be generated on-the-fly because of mode switching devices, and
      // are not removed from the user db because the user db represents devices
      // that have been used some time by a user that should be trusted.
      auto itr = global_map->find(global_key);
      if (itr != global_map->end()) {
        (*global_entries_->mutable_trash())[global_key].Swap(&itr->second);
        global_map->erase(itr);
        return PersistChanges();
      }
      return true;
    }
  }
  LOG(ERROR) << "Unrecognized action.";
  return false;
}

bool EntryManager::HandleUserLogin() {
  if (!user_entries_) {
    LOG(ERROR) << "Unable to access user db.";
    return false;
  }

  EntryMap* user_entries = user_entries_->mutable_entries();

  for (const auto& entry : global_entries_->entries()) {
    if (!entry.second.rules().empty()) {
      (*user_entries)[Hash(entry.second.rules())] = entry.second;
    }
  }
  return PersistChanges();
}

size_t EntryManager::GarbageCollectInternal(bool global_only) {
  size_t num_removed = RemoveEntriesOlderThan(kModeSwitchThreshold,
                                              global_entries_->mutable_trash());

  if (!global_only) {
    if (user_entries_) {
      num_removed += RemoveEntriesOlderThan(kCleanupThreshold,
                                            user_entries_->mutable_entries());
    } else {
      LOG(WARNING) << "Unable to access user db.";
    }
  }

  return num_removed;
}

bool EntryManager::ValidateDevPath(const std::string& devpath) {
  if (devpath.empty()) {
    return false;
  }

  base::FilePath to_normalize =
      root_dir_.Append("sys").Append(StripLeadingPathSeparators(devpath));
  base::FilePath normalized_devpath = base::MakeAbsoluteFilePath(to_normalize);
  if (normalized_devpath.empty()) {
    LOG(ERROR) << "Failed to normalize \"" << to_normalize.value() << "\"";
    return false;
  }

  if (normalized_devpath.ReferencesParent()) {
    LOG(ERROR) << "The path \"" << normalized_devpath.value()
               << "\" has a parent reference.";
    return false;
  }

  if (!root_dir_.Append(kDevpathRoot).IsParent(normalized_devpath)) {
    LOG(ERROR) << "Failed \"" << normalized_devpath.value()
               << "\" is not a devpath.";
    return false;
  }
  return true;
}

bool EntryManager::PersistChanges() {
  bool success = true;
  if (!WriteProtoToPath(global_db_path_, global_entries_.get())) {
    LOG(ERROR) << "Failed to writeback global DB.";
    success = false;
  }
  if (!user_db_path_.empty() && user_entries_) {
    if (!WriteProtoToPath(user_db_path_, user_entries_.get())) {
      LOG(ERROR) << "Failed to writeback user DB.";
      success = false;
    }
  }
  return success;
}

}  // namespace usb_bouncer
