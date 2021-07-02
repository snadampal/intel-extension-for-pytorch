#include <ATen/Context.h>
#include <ATen/native/BinaryOps.h>
#include <ATen/native/TensorIterator.h>
#include <intrinsic/ipex_intrinsic.h>

#include <utils/DPCPP.h>
#include "comm/Pointwise.h"
#include "comm/ScalarOps.h"
#include <oneDNN/oneDNN.h>

#include "Loops.h"

using namespace xpu::dpcpp;

namespace at {
namespace impl {

// Note: dpcpp compiler does not support uname type in template.
class SyclOpAdd {};

static void add_kernel_dpcpp(TensorIterator& iter, Scalar alpha_scalar) {
  IPEX_DISPATCH_ALL_TYPES_AND3(
      at::ScalarType::Half,
      at::ScalarType::BFloat16,
      at::ScalarType::Bool,
      iter.dtype(),
      "add",
      [&]() {
        auto alpha = alpha_scalar.to<scalar_t>();
          dpcpp_kernel_with_scalars<SyclOpAdd>(
            iter,
            [=](scalar_t a, scalar_t b) -> scalar_t { return a + alpha * b; });
      });
}

static void sub_kernel_dpcpp(TensorIterator& iter, Scalar alpha_scalar) {
  return add_kernel_dpcpp(iter, -alpha_scalar);
}

// alpha_check
static inline void alpha_check(const TensorIterator& iter, Scalar alpha) {
  TORCH_CHECK(
      !alpha.isBoolean() || iter.dtype() == ScalarType::Bool,
      "Boolean alpha only supported for Boolean results.");
  TORCH_CHECK(
      isFloatingType(iter.dtype()) || alpha.isIntegral(true),
      "For integral input tensors, argument alpha must not be a floating "
      "point number.");
}

// Basic checking for all sub functions.
static inline void sub_check(const Tensor& self, const Tensor& other) {
  TORCH_CHECK(
      self.scalar_type() != kBool || other.scalar_type() != kBool,
      "Subtraction, the `-` operator, with two bool tensors is not supported. "
      "Use the `^` or `logical_xor()` operator instead.");
  TORCH_CHECK(
      self.scalar_type() != kBool && other.scalar_type() != kBool,
      "Subtraction, the `-` operator, with a bool tensor is not supported. "
      "If you are trying to invert a mask, use the `~` or `logical_not()` "
      "operator instead.");
}

} // namespace impl

namespace AtenIpexTypeXPU {

Tensor& add_out(
    Tensor& result,
    const Tensor& _self,
    const Tensor& _other,
    Scalar alpha) {
  Tensor self, other;
 if (1.0 == alpha.to<float>() && _self.defined() && _other.defined() &&
      xpu::oneDNN::is_supported_dtype_in_binary(_self.scalar_type(), _other.scalar_type()) &&
      _self.dim() > 0 && _other.dim() > 0 &&
      _self.dim() == _other.dim() &&
      _self.is_contiguous() && _other.is_contiguous() &&
      !(DPCPPTensorContext::is_plain(_self) &&
        !DPCPPTensorContext::is_plain(_other) &&
        _self.sizes() != _other.sizes()) &&
      !(is_expandable_to(_self.sizes(), _other.sizes()) &&
      !is_expandable_to(_other.sizes(), _self.sizes())) &&
      !is_wrapped_number(_self) && !is_wrapped_number(_other)) {
    xpu::oneDNN::bin<dnnl::algorithm::binary_add>(result, _self, _other);
    return result;
  } else {
    result = to_plain_if_needed(result);
    self = to_plain_if_needed(_self);
    other = to_plain_if_needed(_other);
  }

  auto iter = TensorIterator::binary_op(result, self, other);
  impl::alpha_check(iter, alpha);
  impl::add_kernel_dpcpp(iter, alpha);
  TORCH_INTERNAL_ASSERT(result.scalar_type() == iter.output().dtype());
  return result;
}

Tensor add(const Tensor& _self, const Tensor& _other, Scalar alpha) {
  Tensor result, self, other;
 if (1.0 == alpha.to<float>() && _self.defined() && _other.defined() &&
      xpu::oneDNN::is_supported_dtype_in_binary(_self.scalar_type(), _other.scalar_type()) &&
      _self.dim() > 0 && _other.dim() > 0 &&
      _self.dim() == _other.dim() &&
      _self.is_contiguous() && _other.is_contiguous() &&
      !(DPCPPTensorContext::is_plain(_self) &&
        !DPCPPTensorContext::is_plain(_other) &&
        _self.sizes() != _other.sizes()) &&
      !(is_expandable_to(_self.sizes(), _other.sizes()) &&
      !is_expandable_to(_other.sizes(), _self.sizes())) &&
      !is_wrapped_number(_self) && !is_wrapped_number(_other)) {
    xpu::oneDNN::bin<dnnl::algorithm::binary_add>(result, _self, _other);
    return result;
  } else {
    self = to_plain_if_needed(_self);
    other = to_plain_if_needed(_other);
  }

  auto iter = TensorIterator::binary_op(result, self, other);
  impl::alpha_check(iter, alpha);
  impl::add_kernel_dpcpp(iter, alpha);
  return iter.output();
}

Tensor& add_(Tensor& self, const Tensor& other, Scalar alpha) {
  return at::AtenIpexTypeXPU::add_out(self, self, other, alpha);
}

Tensor add(const Tensor& self, Scalar other, Scalar alpha) {
  return at::AtenIpexTypeXPU::add(
      self, wrapped_scalar_tensor(other), alpha);
}

Tensor& add_(Tensor& self, Scalar other, Scalar alpha) {
  return at::AtenIpexTypeXPU::add_(
      self, wrapped_scalar_tensor(other), alpha);
}

Tensor& sub_out(
    Tensor& result,
    const Tensor& self,
    const Tensor& other,
    Scalar alpha) {
  impl::sub_check(self, other);
  auto iter = TensorIterator::binary_op(
      result,
      self,
      other);
  impl::alpha_check(iter, alpha);
  impl::sub_kernel_dpcpp(iter, alpha);
  TORCH_INTERNAL_ASSERT(result.scalar_type() == iter.output().dtype());
  return result;
}

Tensor sub(const Tensor& self, const Tensor& other, Scalar alpha) {
  impl::sub_check(self, other);
  Tensor result;
  auto iter = TensorIterator::binary_op(result, self, other);
  impl::alpha_check(iter, alpha);
  impl::sub_kernel_dpcpp(iter, alpha);
  return iter.output();
}

Tensor& sub_(Tensor& self, const Tensor& other, Scalar alpha) {
  return at::AtenIpexTypeXPU::sub_out(self, self, other, alpha);
}

Tensor rsub(const Tensor& self, const Tensor& other, Scalar alpha) {
  return at::AtenIpexTypeXPU::sub(other, self, alpha);
}

Tensor sub(const Tensor& self, Scalar other, Scalar alpha) {
  return at::AtenIpexTypeXPU::sub(
      self, wrapped_scalar_tensor(other), alpha);
}

Tensor& sub_(Tensor& self, Scalar other, Scalar alpha) {
  return at::AtenIpexTypeXPU::sub_(
      self, wrapped_scalar_tensor(other), alpha);
}

Tensor rsub(const Tensor& self, Scalar other, Scalar alpha) {
  return at::AtenIpexTypeXPU::rsub(
      self, wrapped_scalar_tensor(other), alpha);
}

} // namespace AtenIpexTypeXPU


namespace AtenIpexTypeQuantizedXPU {

Tensor add(const Tensor& _self, const Tensor& _other, Scalar alpha) {
  Tensor result, self, other;
  if (1.0 == alpha.to<float>() &&
      _self.defined() &&
      _other.defined() &&
      _self.sizes() == _other.sizes() &&
      !is_wrapped_number(_self) &&
      !is_wrapped_number(_other) &&
      (!DPCPPTensorContext::is_plain(_self) ||
       !DPCPPTensorContext::is_plain(_other))) {
    xpu::oneDNN::sum(result, {_self.contiguous(), _other.contiguous()}, {1.0, 1.0});
    return result;
  } else {
    self = to_plain_if_needed(_self);
    other = to_plain_if_needed(_other);
  }

  auto iter = TensorIterator::binary_op(result, self, other);
  impl::alpha_check(iter, alpha);
  impl::add_kernel_dpcpp(iter, alpha);
  return iter.output();
}

} // namespace AtenIpexTypeQuantizedXPU
} // namespace at