// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_profile.h"

#include <string>

#include <base/memory/ref_counted.h>
#include <base/stringprintf.h>
#include <gmock/gmock.h>

#include "shill/refptr_types.h"

namespace shill {

class ControlInterface;
class GLib;
class Manager;

MockProfile::MockProfile(ControlInterface *control,
                         GLib *glib,
                         Manager *manager)
    : Profile(control, glib, manager, Identifier("mock"), "", false) {
}

MockProfile::MockProfile(ControlInterface *control,
                         GLib *glib,
                         Manager *manager,
                         const std::string &identifier)
    : Profile(control, glib, manager, Identifier(identifier), "", false) {
}

MockProfile::~MockProfile() {}

}  // namespace shill
