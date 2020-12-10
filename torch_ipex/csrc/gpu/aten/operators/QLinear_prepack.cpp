#include <ATen/core/op_registration/op_registration.h>
#include <ATen/native/quantized/cpu/packed_params.h>
#include <core/DPCPPUtils.h>
#include <core/Runtime.h>

#include <utils/ParamUtils.h>

#include "QUtil.h"

using namespace dnnl;
using namespace at::dpcpp;
using namespace at::native;

c10::intrusive_ptr<LinearPackedParamsBase> at::AtenIpexTypeQuantizedXPU::PackedLinearWeightQDPCPP::prepack(
        at::Tensor weight,
        c10::optional<at::Tensor> bias) {
  c10::intrusive_ptr<LinearPackedParamsBase> ret_ptr = c10::make_intrusive<PackedLinearWeightQDPCPP>(
      at::AtenIpexTypeQuantizedXPU::PackedLinearWeightQDPCPP{weight, bias});
  return ret_ptr;
}

namespace at {
namespace AtenIpexTypeQuantizedXPU {

c10::intrusive_ptr<LinearPackedParamsBase> dpcppLinearPrepack(Tensor weight, c10::optional<Tensor> bias) {
  // This is just align with Pytorch Python API!
  auto ret_ptr = PackedLinearWeightQDPCPP::prepack(weight, bias);
  return ret_ptr;
}

TORCH_LIBRARY_IMPL(quantized, QuantizedXPU, m) {
  m.impl("linear_prepack", dpcppLinearPrepack);
}

} // namespace AtenIpexTypeQuantizedXPU
} // namespace at