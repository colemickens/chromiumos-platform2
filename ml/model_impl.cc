// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/model_impl.h"

#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <tensorflow/contrib/lite/context.h>
#include <tensorflow/contrib/lite/interpreter.h>
#include <tensorflow/contrib/lite/kernels/register.h>

namespace ml {

using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::GraphExecutor;
using ::chromeos::machine_learning::mojom::GraphExecutorRequest;
using ::chromeos::machine_learning::mojom::ModelRequest;

ModelImpl::ModelImpl(const std::map<std::string, int>& required_inputs,
                     const std::map<std::string, int>& required_outputs,
                     std::unique_ptr<tflite::FlatBufferModel> model,
                     ModelRequest request)
    : required_inputs_(required_inputs),
      required_outputs_(required_outputs),
      model_(std::move(model)),
      binding_(this, std::move(request)) {}

void ModelImpl::set_connection_error_handler(
    base::Closure connection_error_handler) {
  binding_.set_connection_error_handler(std::move(connection_error_handler));
}

int ModelImpl::num_graph_executors_for_testing() const {
  return graph_executors_.size();
}

void ModelImpl::CreateGraphExecutor(
    GraphExecutorRequest request, const CreateGraphExecutorCallback& callback) {
  if (model_ == nullptr) {
    LOG(ERROR) << "Null model provided.";
    callback.Run(CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR);
    return;
  }

  // Instantiate interpreter.
  tflite::ops::builtin::BuiltinOpResolver resolver;
  std::unique_ptr<tflite::Interpreter> interpreter;
  const TfLiteStatus resolve_status =
      tflite::InterpreterBuilder(*model_, resolver)(&interpreter);
  if (resolve_status != kTfLiteOk || !interpreter) {
    LOG(ERROR) << "Could not resolve model ops.";
    callback.Run(CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR);
    return;
  }

  // Allocate memory for tensors.
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    callback.Run(CreateGraphExecutorResult::MEMORY_ALLOCATION_ERROR);
    return;
  }

  // Add graph executor and schedule its deletion on pipe closure.
  graph_executors_.emplace_front(required_inputs_, required_outputs_,
                                 std::move(interpreter), std::move(request));
  graph_executors_.front().set_connection_error_handler(
      base::Bind(&ModelImpl::EraseGraphExecutor, base::Unretained(this),
                 graph_executors_.begin()));

  callback.Run(CreateGraphExecutorResult::OK);
}

void ModelImpl::EraseGraphExecutor(
    const std::list<GraphExecutorImpl>::const_iterator it) {
  graph_executors_.erase(it);
}

}  // namespace ml
