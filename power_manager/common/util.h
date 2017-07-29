// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_COMMON_UTIL_H_
#define POWER_MANAGER_COMMON_UTIL_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>

namespace power_manager {
namespace util {

// Clamps |percent| in the range [0.0, 100.0].
double ClampPercent(double percent);

// Returns |delta| as a string of the format "4h3m45s".
std::string TimeDeltaToString(base::TimeDelta delta);

// Writes the given buffer into the file, overwriting any data that was
// previously there. Returns true if all bytes are written or false otherwise.
bool WriteFileFully(const base::FilePath& filename, const char* data, int size);

// Reads a base-10 int64 value from |path| to |value|, ignoring trailing
// whitespace. Logs an error and returns false on failure.
bool ReadInt64File(const base::FilePath& path, int64_t* value_out);

// Writes the base-10 representation of |value| to |path| without a trailing
// newline. Logs an error and returns false on failure.
bool WriteInt64File(const base::FilePath& path, int64_t value);

}  // namespace util
}  // namespace power_manager

#endif  // POWER_MANAGER_COMMON_UTIL_H_
