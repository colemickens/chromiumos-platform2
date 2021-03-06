// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/machine_learning_service_impl.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <tensorflow/contrib/lite/model.h>

#include "ml/model_impl.h"
#include "ml/mojom/model.mojom.h"

namespace ml {

namespace {

using ::chromeos::machine_learning::mojom::LoadModelResult;
using ::chromeos::machine_learning::mojom::ModelId;
using ::chromeos::machine_learning::mojom::ModelRequest;
using ::chromeos::machine_learning::mojom::ModelSpecPtr;

constexpr char kSystemModelDir[] = "/opt/google/chrome/ml_models/";

// To avoid passing a lambda as a base::Closure.
void DeleteModelImpl(const ModelImpl* const model_impl) {
  delete model_impl;
}

}  // namespace

MachineLearningServiceImpl::MachineLearningServiceImpl(
    mojo::ScopedMessagePipeHandle pipe,
    base::Closure connection_error_handler,
    const std::string& model_dir)
    : model_metadata_(GetModelMetadata()),
      model_dir_(model_dir),
      binding_(this, std::move(pipe)) {
  binding_.set_connection_error_handler(std::move(connection_error_handler));
}

MachineLearningServiceImpl::MachineLearningServiceImpl(
    mojo::ScopedMessagePipeHandle pipe, base::Closure connection_error_handler)
    : MachineLearningServiceImpl(std::move(pipe),
                                 std::move(connection_error_handler),
                                 kSystemModelDir) {}

void MachineLearningServiceImpl::LoadModel(ModelSpecPtr spec,
                                           ModelRequest request,
                                           const LoadModelCallback& callback) {
  if (spec->id <= ModelId::UNKNOWN || spec->id > ModelId::TEST_MODEL) {
    callback.Run(LoadModelResult::MODEL_SPEC_ERROR);
    return;
  }

  // Shouldn't happen (as we maintain a metadata entry for every valid model),
  // but can't hurt to be defensive.
  const auto metadata_lookup = model_metadata_.find(spec->id);
  if (metadata_lookup == model_metadata_.end()) {
    LOG(ERROR) << "No metadata present for model ID " << spec->id << ".";
    callback.Run(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }
  const ModelMetadata& metadata = metadata_lookup->second;

  // Attempt to load model.
  const std::string model_path = model_dir_ + metadata.model_file;
  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromFile(model_path.c_str());
  if (model == nullptr) {
    LOG(ERROR) << "Failed to load model file '" << model_path << "'.";
    callback.Run(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  // Use a connection error handler to strongly bind |model_impl| to |request|.
  ModelImpl* const model_impl =
      new ModelImpl(metadata.required_inputs, metadata.required_outputs,
                    std::move(model), std::move(request));
  model_impl->set_connection_error_handler(
      base::Bind(&DeleteModelImpl, base::Unretained(model_impl)));
  callback.Run(LoadModelResult::OK);
}

}  // namespace ml
