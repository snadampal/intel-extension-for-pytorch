#pragma once

#include "General.h"
#include <ATen/native/TensorIterator.h>

namespace at {
namespace AtenIpexTypeXPU {

static Tensor wrapped_scalar_tensor(Scalar scalar, const Device device = at::kCPU) {
  auto tensor=scalar_to_tensor(scalar, device);
  tensor.unsafeGetTensorImpl()->set_wrapped_number(true);
  return tensor;
}

static bool is_wrapped_number(const Tensor& t) {
  return t.unsafeGetTensorImpl()->is_wrapped_number();
}

}} // at::AtenIpexTypeXPU