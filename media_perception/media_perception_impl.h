// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_MEDIA_PERCEPTION_IMPL_H_
#define MEDIA_PERCEPTION_MEDIA_PERCEPTION_IMPL_H_

#include <mojo/public/cpp/bindings/binding.h>

#include "mojom/media_perception.mojom.h"

namespace mri {

class MediaPerceptionImpl :
  public chromeos::media_perception::mojom::MediaPerception {
 public:
  MediaPerceptionImpl(
      chromeos::media_perception::mojom::MediaPerceptionRequest request);

  void set_connection_error_handler(base::Closure connection_error_handler);

 private:
  mojo::Binding<chromeos::media_perception::mojom::MediaPerception>
      binding_;

  DISALLOW_COPY_AND_ASSIGN(MediaPerceptionImpl);
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_MEDIA_PERCEPTION_IMPL_H_
