// Autogenerated file by gen_code.py
// /home/gta/work/rebase_pt110/frameworks.ai.pytorch.ipex-gpu/scripts/gpu/gen_code.py
// --declarations-path
// /home/gta/work/rebase_pt110/frameworks.ai.pytorch.ipex-gpu/scripts/declarations/Declarations.yaml
// --out
// /home/gta/work/rebase_pt110/frameworks.ai.pytorch.ipex-gpu/csrc/aten/generated/ATen/
// --source-path /home/gta/work/rebase_pt110/frameworks.ai.pytorch.ipex-gpu. Do
// not edit directly!
#pragma once

#include <ATen/Tensor.h>
#include <intrinsic/ipex_intrinsic.h>

namespace at {

namespace AtenIpexTypeQuantizedXPU {

void RegisterAtenTypeFunctions();

Tensor add(const Tensor& self, const Tensor& other, Scalar alpha);
Tensor as_strided(
    const Tensor& self,
    IntArrayRef size,
    IntArrayRef stride,
    c10::optional<int64_t> storage_offset);
Tensor& copy_(Tensor& self, const Tensor& src, bool non_blocking);
Tensor empty(
    IntArrayRef size,
    const TensorOptions& options,
    c10::optional<MemoryFormat> memory_format);
Tensor _empty_affine_quantized(
    IntArrayRef size,
    const TensorOptions& options,
    double scale,
    int64_t zero_point,
    c10::optional<MemoryFormat> memory_format);
const Tensor& resize_(
    const Tensor& self,
    IntArrayRef size,
    c10::optional<MemoryFormat> memory_format);
Tensor empty_strided(
    IntArrayRef size,
    IntArrayRef stride,
    const TensorOptions& options);
Tensor quantized_max_pool2d(
    const Tensor& self,
    IntArrayRef kernel_size,
    IntArrayRef stride,
    IntArrayRef padding,
    IntArrayRef dilation,
    bool ceil_mode);
Tensor clone(const Tensor& self, c10::optional<MemoryFormat> memory_format);
Tensor addmm(
    const Tensor& self,
    const Tensor& mat1,
    const Tensor& mat2,
    Scalar beta,
    Scalar alpha);
Tensor quantize_per_tensor(
    const Tensor& self,
    double scale,
    int64_t zero_point,
    ScalarType dtype);
Tensor dequantize(const Tensor& self);
double q_scale(const Tensor& self);
int64_t q_zero_point(const Tensor& self);
Tensor q_per_channel_scales(const Tensor& self);
Tensor q_per_channel_zero_points(const Tensor& self);
int64_t q_per_channel_axis(const Tensor& self);
Tensor int_repr(const Tensor& self);
QScheme qscheme(const Tensor& self);
Tensor& set_quantizer_(Tensor& self, ConstQuantizerPtr quantizer);
Tensor view(const Tensor& self, IntArrayRef size);
bool equal(const Tensor& self, const Tensor& other);
Tensor leaky_relu(const Tensor& self, Scalar negative_slope);
Tensor& leaky_relu_(Tensor& self, Scalar negative_slope);
Tensor& adaptive_avg_pool2d_out(
    Tensor& out,
    const Tensor& self,
    IntArrayRef output_size);
Tensor adaptive_avg_pool2d(const Tensor& self, IntArrayRef output_size);
Tensor _adaptive_avg_pool2d(const Tensor& self, IntArrayRef output_size);
Tensor& avg_pool2d_out(
    Tensor& out,
    const Tensor& self,
    IntArrayRef kernel_size,
    IntArrayRef stride,
    IntArrayRef padding,
    bool ceil_mode,
    bool count_include_pad,
    c10::optional<int64_t> divisor_override);
Tensor avg_pool2d(
    const Tensor& self,
    IntArrayRef kernel_size,
    IntArrayRef stride,
    IntArrayRef padding,
    bool ceil_mode,
    bool count_include_pad,
    c10::optional<int64_t> divisor_override);
std::tuple<Tensor, Tensor> max_pool2d_with_indices(
    const Tensor& self,
    IntArrayRef kernel_size,
    IntArrayRef stride,
    IntArrayRef padding,
    IntArrayRef dilation,
    bool ceil_mode);
Tensor upsample_nearest2d(
    const Tensor& input,
    c10::optional<IntArrayRef> output_size,
    c10::optional<ArrayRef<double>> scale_factors);
Tensor upsample_nearest2d(
    const Tensor& self,
    IntArrayRef output_size,
    c10::optional<double> scales_h,
    c10::optional<double> scales_w);
void record_stream(Tensor& self, Stream s);
} // namespace AtenIpexTypeQuantizedXPU

} // namespace at