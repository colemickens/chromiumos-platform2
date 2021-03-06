// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media_perception/media_perception_controller_impl.h"

#include <base/bind.h>
#include <utility>

#include "base/logging.h"
#include "media_perception/media_perception_impl.h"

namespace mri {

namespace {

// To avoid passing a lambda as a base::Closure.
void OnConnectionClosedOrError(
    const MediaPerceptionImpl* const media_perception_impl) {
  DLOG(INFO) << "Got closed connection.";
  delete media_perception_impl;
}

}  // namespace

MediaPerceptionControllerImpl::MediaPerceptionControllerImpl(
    chromeos::media_perception::mojom::MediaPerceptionControllerRequest request)
  : binding_(this, std::move(request)) {}

void MediaPerceptionControllerImpl::set_connection_error_handler(
    base::Closure connection_error_handler) {
  binding_.set_connection_error_handler(std::move(connection_error_handler));
}

void MediaPerceptionControllerImpl::ActivateMediaPerception(
    chromeos::media_perception::mojom::MediaPerceptionRequest request) {
  DLOG(INFO) << "Got request to activate media perception.";

  // Use a connection error handler to strongly bind |media_perception_impl|
  // to |request|.
  MediaPerceptionImpl* const media_perception_impl =
      new MediaPerceptionImpl(std::move(request));
  media_perception_impl->set_connection_error_handler(
      base::Bind(&OnConnectionClosedOrError,
                 base::Unretained(media_perception_impl)));
}

}  // namespace mri
