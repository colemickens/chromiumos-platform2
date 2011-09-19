// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gdk/gdk.h>
#include <gflags/gflags.h>

#include <string>
#include <syslog.h>

// Defines from syslog.h that conflict with base/logging.h. Ugh.
#undef LOG_INFO
#undef LOG_WARNING

#include "base/command_line.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "power_manager/ambient_light_sensor.h"
#include "power_manager/audio_detector.h"
#include "power_manager/backlight.h"
#include "power_manager/monitor_reconfigure.h"
#include "power_manager/power_constants.h"
#include "power_manager/power_manager_service.h"
#include "power_manager/powerd.h"
#include "power_manager/video_detector.h"

#ifndef VCSID
#define VCSID "<not set>"
#endif

using std::string;

DEFINE_string(prefs_dir, "",
              "Directory to store settings.");
DEFINE_string(default_prefs_dir, "",
              "Directory to read default settings (Read Only).");
DEFINE_string(log_dir, "",
              "Directory to store logs.");
DEFINE_string(run_dir, "",
              "Directory to store stateful data for daemon.");

// Returns true on success.
static bool SetUpLogSymlink(const string& symlink_path,
                            const string& log_basename) {
  unlink(symlink_path.c_str());
  if (symlink(log_basename.c_str(), symlink_path.c_str()) == -1) {
    PLOG(ERROR) << "Unable to create symlink " << symlink_path
                << " pointing at " << log_basename;
    return false;
  }
  return true;
}

static string GetTimeAsString(time_t utime) {
  struct tm tm;
  CHECK(localtime_r(&utime, &tm) == &tm);
  char str[16];
  CHECK(strftime(str, sizeof(str), "%Y%m%d-%H%M%S", &tm) == 15);
  return string(str);
}

int main(int argc, char* argv[]) {
  // Sadly we can't use LOG() here - we always want this message logged, even
  // when other logging is turned off.
  openlog("powerd", LOG_PID, LOG_DAEMON);
  syslog(LOG_NOTICE, "vcsid %s", VCSID);
  closelog();
  google::ParseCommandLineFlags(&argc, &argv, true);
  CHECK(!FLAGS_prefs_dir.empty()) << "--prefs_dir is required";
  CHECK(!FLAGS_log_dir.empty()) << "--log_dir is required";
  CHECK(!FLAGS_run_dir.empty()) << "--run_dir is required";
  CHECK(argc == 1) << "Unexpected arguments. Try --help";
  CommandLine::Init(argc, argv);

  const string log_latest =
      StringPrintf("%s/powerd.LATEST", FLAGS_log_dir.c_str());
  const string log_basename =
      StringPrintf("powerd.%s", GetTimeAsString(::time(NULL)).c_str());
  const string log_path = FLAGS_log_dir + "/" + log_basename;
  CHECK(SetUpLogSymlink(log_latest, log_basename));
  logging::InitLogging(log_path.c_str(),
                       logging::LOG_ONLY_TO_FILE,
                       logging::DONT_LOCK_LOG_FILE,
                       logging::APPEND_TO_OLD_LOG_FILE,
                       logging::DISABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS);

  FilePath prefs_dir(FLAGS_prefs_dir);
  FilePath default_prefs_dir(FLAGS_default_prefs_dir.empty() ?
                             "/usr/share/power_manager" :
                             FLAGS_default_prefs_dir);
  power_manager::PowerPrefs prefs(prefs_dir, default_prefs_dir);
  gdk_init(&argc, &argv);
  power_manager::Backlight backlight;
  LOG_IF(WARNING, !backlight.Init(FilePath(power_manager::kBacklightPath),
                                  power_manager::kBacklightPattern))
      << "Cannot initialize backlight";
  power_manager::BacklightController backlight_ctl(&backlight, &prefs);
  LOG_IF(WARNING, !backlight_ctl.Init()) << "Cannot initialize backlight "
      "controller";
  power_manager::AmbientLightSensor als(&backlight_ctl);
  if (!als.Init())
    LOG(WARNING) << "Cannot initialize light sensor";
  MetricsLibrary metrics_lib;
  power_manager::VideoDetector video_detector;
  video_detector.Init();
  power_manager::AudioDetector audio_detector;
  audio_detector.Init();
  metrics_lib.Init();
  power_manager::MonitorReconfigure monitor_reconfigure(&backlight_ctl);
  if (!monitor_reconfigure.Init())
    LOG(WARNING) << "Cannot initialize monitor reconfigure";
  FilePath run_dir(FLAGS_run_dir);
  power_manager::Daemon daemon(&backlight_ctl,
                               &prefs,
                               &metrics_lib,
                               &video_detector,
                               &audio_detector,
                               &monitor_reconfigure,
                               run_dir);

  daemon.Init();
  // Create and initialize the D-Bus power manager service.
  ::g_type_init();
  power_manager::PowerManagerService manager(&daemon);
  if (!manager.Initialize())
    LOG(ERROR) << "Cannot initialize power manager service";
  else if (!manager.Register(chromeos::dbus::GetSystemBusConnection()))
    LOG(ERROR) << "Cannot register power manager service";
  daemon.Run();
  return 0;
}
