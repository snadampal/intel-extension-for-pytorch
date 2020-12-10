#include <ATen/Context.h>
#include <ATen/native/PointwiseOps.h>
#include <ATen/native/TensorIterator.h>

#include <core/DPCPP.h>

#include <utils/ATDispatch.h>

#include "Loops.h"

DPCPP_DEF_K1(addcmul);
DPCPP_DEF_K1(addcdiv);

namespace at {
namespace AtenIpexTypeXPU {
namespace impl {

static void addcmul_kernel(TensorIterator& iter, Scalar value) {
  IPEX_DISPATCH_ALL_TYPES_AND2(
    at::ScalarType::Half,
    at::ScalarType::BFloat16,
    iter.dtype(),
    "addcmul_dpcpp", [&]() {
    auto alpha = value.to<scalar_t>();
    dpcpp_kernel_for_tensor_iter<DPCPP_K(addcmul)>(
        iter, [alpha](scalar_t a, scalar_t b, scalar_t c) -> scalar_t {
          return a + alpha * b * c;
        });
  });
}

static void addcdiv_kernel(TensorIterator& iter, Scalar value) {
  IPEX_DISPATCH_ALL_TYPES(iter.dtype(), "addcdiv_dpcpp", [&]() {
    auto alpha = value.to<scalar_t>();
    dpcpp_kernel_for_tensor_iter<DPCPP_K(addcdiv)>(
        iter, [alpha](scalar_t a, scalar_t b, scalar_t c) -> scalar_t {
          return a + alpha * (b / c);
        });
  });
}

} // namespace impl

Tensor& addcmul_out(
    Tensor& out,
    const Tensor& self,
    const Tensor& tensor1,
    const Tensor& tensor2,
    Scalar value) {
  // checkBackend("addcmul_cpu", out, self.options().backend());
  auto iter = at::TensorIteratorConfig()
  .set_check_mem_overlap(true)
  .add_output(out)
  .add_input(self)
  .add_input(tensor1)
  .add_input(tensor2)
  .build();
  impl::addcmul_kernel(iter, value);
  return out;
}

Tensor addcmul(
    const Tensor& self,
    const Tensor& tensor1,
    const Tensor& tensor2,
    Scalar value) {
  Tensor result = at::empty({0}, self.options());
  return at::AtenIpexTypeXPU::addcmul_out(
      result, self, tensor1, tensor2, value);
}

Tensor& addcmul_(
    Tensor& self,
    const Tensor& tensor1,
    const Tensor& tensor2,
    Scalar value) {
  return at::AtenIpexTypeXPU::addcmul_out(
      self, self, tensor1, tensor2, value);
}

Tensor& addcdiv_out(
    Tensor& out,
    const Tensor& self,
    const Tensor& tensor1,
    const Tensor& tensor2,
    Scalar value) {
  // checkBackend("addcdiv_cpu", out, self.options().backend());
  auto iter = TensorIteratorConfig()
  .set_check_mem_overlap(true)
  .add_output(out)
  .add_input(self)
  .add_input(tensor1)
  .add_input(tensor2)
  .build();
  impl::addcdiv_kernel(iter, value);
  return out;
}

Tensor addcdiv(
    const Tensor& self,
    const Tensor& tensor1,
    const Tensor& tensor2,
    Scalar value) {
  Tensor result = at::empty({0}, self.options());
  return at::AtenIpexTypeXPU::addcdiv_out(
      result, self, tensor1, tensor2, value);
}

Tensor& addcdiv_(
    Tensor& self,
    const Tensor& tensor1,
    const Tensor& tensor2,
    Scalar value) {
  return at::AtenIpexTypeXPU::addcdiv_out(
      self, self, tensor1, tensor2, value);
}

} // namespace AtenIpexTypeXPU
} // namespace at