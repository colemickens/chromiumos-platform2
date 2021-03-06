// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>

#include <iostream>
#include <memory>

#include <base/command_line.h>
#include <base/strings/string_number_conversions.h>

#include "screenshot/capture.h"
#include "screenshot/crtc.h"
#include "screenshot/png.h"

namespace screenshot {
namespace {

constexpr const char kHelpSwitch[] = "help";
constexpr const char kInternalSwitch[] = "internal";
constexpr const char kExternalSwitch[] = "external";
constexpr const char kCrtcIdSwitch[] = "crtc-id";
constexpr const char kCropSwitch[] = "crop";

constexpr const char kHelp[] =
    "Usage: screenshot [options...] path/to/output.png\n"
    "\n"
    "Takes a screenshot and saves as a PNG file.\n"
    "By default, a screenshot is captured from any active display.\n"
    "\n"
    "Options:\n"
    "  --internal: Capture from internal display.\n"
    "  --external: Capture from external display.\n"
    "  --crtc-id=ID: Capture from the specified display.\n"
    "  --crop=WxH+X+Y: Specify a subregion to capture.\n";

void PrintHelp() {
  std::cerr << kHelp;
}

int Main() {
  auto* cmdline = base::CommandLine::ForCurrentProcess();

  if (cmdline->HasSwitch(kHelpSwitch) || cmdline->GetArgs().empty()) {
    PrintHelp();
    return 1;
  }

  if (cmdline->GetArgs().size() != 1) {
    LOG(ERROR) << "Must specify single output path";
    return 1;
  }

  int crtc_specs =
      (cmdline->HasSwitch(kInternalSwitch) ? 1 : 0) +
      (cmdline->HasSwitch(kExternalSwitch) ? 1 : 0) +
      (cmdline->HasSwitch(kCrtcIdSwitch) ? 1 : 0);
  if (crtc_specs > 1) {
    LOG(ERROR) << "--internal, --external and --crtc-id are exclusive";
    return 1;
  }

  bool crop_set = false;
  uint32_t x, y, width, height;
  if (cmdline->HasSwitch(kCropSwitch)) {
    auto spec = cmdline->GetSwitchValueASCII(kCropSwitch);
    int read_size;
    int scan_size = sscanf(
        spec.c_str(), "%" SCNu32 "x%" SCNu32 "+%" SCNu32 "+%" SCNu32 "%n",
        &width, &height, &x, &y, &read_size);
    if (scan_size != 4 || read_size != static_cast<int>(spec.size())) {
      LOG(ERROR) << "Invalid --crop specification";
      return 1;
    }
    CHECK_GT(width, 0);
    CHECK_GT(height, 0);
    crop_set = true;
  }

  std::unique_ptr<Crtc> crtc;
  if (cmdline->HasSwitch(kInternalSwitch)) {
    crtc = screenshot::CrtcFinder::FindInternalDisplay();
  } else if (cmdline->HasSwitch(kExternalSwitch)) {
    crtc = screenshot::CrtcFinder::FindExternalDisplay();
  } else if (cmdline->HasSwitch(kCrtcIdSwitch)) {
    uint32_t crtc_id;
    if (!base::StringToUint(
            cmdline->GetSwitchValueASCII(kCrtcIdSwitch), &crtc_id)) {
      LOG(ERROR) << "Invalid --crtc-id specification";
      return 1;
    }
    crtc = screenshot::CrtcFinder::FindById(crtc_id);
  } else {
    crtc = screenshot::CrtcFinder::FindAnyDisplay();
  }

  if (!crtc) {
    LOG(ERROR) << "CRTC not found. Is the screen on?";
    return 1;
  }

  const uint32_t crtc_width = crtc->fb()->width;
  const uint32_t crtc_height = crtc->fb()->height;
  if (!crop_set) {
    x = 0;
    y = 0;
    width = crtc_width;
    height = crtc_height;
  }
  CHECK_LT(x, crtc_width);
  CHECK_LT(y, crtc_height);
  CHECK_LE(x + width, crtc_width);
  CHECK_LE(y + height, crtc_height);

  auto map = screenshot::Capture(*crtc, x, y, width, height);

  screenshot::SaveAsPng(cmdline->GetArgs()[0].c_str(), map->buffer(),
                        map->width(), map->height(), map->stride());
  return 0;
}

}  // namespace
}  // namespace screenshot

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);

  return screenshot::Main();
}
