#include <ATen/native/UpSample.h>
#include <ATen/ipex_type_dpcpp_customized.h>
#include <tensor/Context.h>
#include "UpSample.h"

#ifdef USE_PRIMITIVE_CACHE
#include <oneDNN/LRUCache.h>
#endif

using namespace dnnl;
using namespace at::dpcpp;
using namespace at::native;
using namespace at::AtenIpexTypeXPU;

namespace at {
namespace AtenIpexTypeXPU {
namespace impl {

static void upsample_nearest_out_dpcpp_kernel(
    Tensor& output,
    const Tensor& input_,
    IntArrayRef output_size,
    const double& scales_w = 0.0,
    const double& scales_h = 0.0,
    const double& scales_d = 0.0) {
  auto input = input_.contiguous();

  auto strm = GpuStreamManager::Instance().get_stream();
  Device curDevice = Device(kXPU, current_device());
  auto eng = GpuEngineManager::Instance().get_engine(curDevice);

  bool is_customer_scales =
      scales_w != 0.0 || scales_h != 0.0 || scales_d != 0.0;

  int64_t ndims = input.ndimension();
  IntArrayRef input_size = input.sizes();
  memory::dims src_dims, dst_dims;
  std::vector<float> factors;
  set_params(
      input_size,
      output_size,
      src_dims,
      dst_dims,
      factors,
      ndims,
      scales_w,
      scales_h,
      scales_d);

  output.resize_(dst_dims);

  memory::format_tag data_format = ndims == 5
      ? memory::format_tag::ncdhw
      : (ndims == 4 ? memory::format_tag::nchw : memory::format_tag::ncw);
  memory::format_tag format_any = memory::format_tag::any;
  memory::data_type data_type = dt_to_dnnl(input.scalar_type());

  std::shared_ptr<memory::desc> dst_md;
  if (!is_customer_scales)
    dst_md.reset(new memory::desc(dst_dims, data_type, format_any));

  auto src_ctx = at::AtenIpexTypeXPU::DPCPPTensorContext::get_tensor_ctx(input);
  auto src_md = src_ctx.is_plain() ? memory::desc(src_dims, data_type, data_format) :
      src_ctx.meta();

#ifdef USE_PRIMITIVE_CACHE
  lru_key_t key;
  if (!is_customer_scales) {
    create_key(key, algorithm::resampling_nearest, factors, src_md, *dst_md);
  } else {
    create_key(key, algorithm::resampling_nearest, factors, src_md);
  }
#endif

  auto resampling_desc = resampling_forward::desc(
      prop_kind::forward,
      algorithm::resampling_nearest,
      factors,
      src_md,
      *dst_md);
  auto resampling_pd = resampling_forward::primitive_desc(resampling_desc, eng);

#ifdef USE_PRIMITIVE_CACHE
  auto resample_forward = fetch_or_create_m<resampling_forward>(key, resampling_pd);
#else
  auto resample_forward = resampling_forward(resampling_pd);
#endif

  if (!src_ctx.is_plain()) {
    output = empty_opaque_tensor(resampling_pd.dst_desc(), input.options(), c10::nullopt);
  }
  memory src_memory = dpcpp_onednn_memory(resampling_pd.src_desc(), eng, input.data_ptr());
  memory dst_memory = dpcpp_onednn_memory(resampling_pd.dst_desc(), eng, output.data_ptr());

  DPCPP_ONEDNN_EXEC(resample_forward, strm, {{DNNL_ARG_SRC, src_memory}, {DNNL_ARG_DST, dst_memory}});
}

static void upsample_nearest_backward_out_dpcpp_kernel(
    Tensor& grad_input,
    const Tensor& grad_output_,
    IntArrayRef output_size,
    IntArrayRef input_size,
    const double& scales_w = 0.0,
    const double& scales_h = 0.0,
    const double& scales_d = 0.0) {
  auto grad_output = grad_output_.contiguous();

  auto strm = GpuStreamManager::Instance().get_stream();
  Device curDevice = Device(kXPU, current_device());
  auto eng = GpuEngineManager::Instance().get_engine(curDevice);

  bool is_customer_scales =
      scales_w != 0.0 || scales_h != 0.0 || scales_d != 0.0;

  int64_t ndims = grad_output.ndimension();
  memory::dims src_dims, dst_dims;
  std::vector<float> factors;
  set_params(
      input_size,
      output_size,
      src_dims,
      dst_dims,
      factors,
      ndims,
      scales_w,
      scales_h,
      scales_d);

  grad_input.resize_(src_dims);

  memory::format_tag data_format = ndims == 5
      ? memory::format_tag::ncdhw
      : (ndims == 4 ? memory::format_tag::nchw : memory::format_tag::ncw);
  memory::format_tag format_any = memory::format_tag::any;
  memory::data_type data_type = dt_to_dnnl(grad_output.scalar_type());

  std::shared_ptr<memory::desc> dst_md;
  auto src_md = memory::desc(src_dims, data_type, data_format);
  auto diff_src_md = memory::desc(src_dims, data_type, format_any);
  if (!is_customer_scales)
    dst_md.reset(new memory::desc(dst_dims, data_type, data_format));

  auto resampling_desc = resampling_forward::desc(
      prop_kind::forward,
      algorithm::resampling_nearest,
      factors,
      src_md,
      *dst_md);
  auto resampling_pd = resampling_forward::primitive_desc(resampling_desc, eng);

  auto diff_dst_ctx = at::AtenIpexTypeXPU::DPCPPTensorContext::get_tensor_ctx(grad_output);
  auto diff_dst_md = diff_dst_ctx.is_plain() ? resampling_pd.dst_desc() :
      diff_dst_ctx.meta();

#ifdef USE_PRIMITIVE_CACHE
  lru_key_t key;
  if (!is_customer_scales) {
    create_key(key, algorithm::resampling_nearest, factors, src_md, *dst_md, diff_src_md, diff_dst_md);
  } else {
    create_key(key, algorithm::resampling_nearest, factors, src_md, diff_src_md, diff_dst_md);
  }
#endif

  auto resampling_bwd_desc = resampling_backward::desc(
      algorithm::resampling_nearest,
      factors,
      diff_src_md,
      diff_dst_md);
  auto resampling_bwd_pd = resampling_backward::primitive_desc(resampling_bwd_desc, eng, resampling_pd);
#ifdef USE_PRIMITIVE_CACHE
  auto resampling_bwd = fetch_or_create_m<resampling_backward>(key, resampling_bwd_pd);
#else
  auto resampling_bwd = resampling_backward(resampling_bwd_pd);
#endif

  if (!diff_dst_ctx.is_plain()) {
    grad_input = empty_opaque_tensor(resampling_bwd_pd.diff_src_desc(), grad_output.options(), c10::nullopt);
  }
  memory diff_src_memory = dpcpp_onednn_memory(
      resampling_bwd_pd.diff_src_desc(), eng, grad_input.data_ptr());
  memory diff_dst_memory = dpcpp_onednn_memory(
      resampling_bwd_pd.diff_dst_desc(), eng, grad_output.data_ptr());

  DPCPP_ONEDNN_EXEC(resampling_bwd, strm, {{DNNL_ARG_DIFF_SRC, diff_src_memory}, {DNNL_ARG_DIFF_DST, diff_dst_memory}});
}

} // namespace impl

using namespace impl;
using at::native::upsample::compute_output_size;
using at::native::upsample::get_scale_value;

Tensor& upsample_nearest3d_out(
    Tensor& output,
    const Tensor& input,
    IntArrayRef output_size,
    c10::optional<double> scales_d,
    c10::optional<double> scales_h,
    c10::optional<double> scales_w) {
  upsample_nearest_out_dpcpp_kernel(
      output,
      input,
      output_size,
      scales_w.has_value() ? static_cast<double>(scales_w.value()) : 0.0,
      scales_h.has_value() ? static_cast<double>(scales_h.value()) : 0.0,
      scales_d.has_value() ? static_cast<double>(scales_d.value()) : 0.0);
  return output;
}

Tensor upsample_nearest3d(
    const Tensor& input,
    IntArrayRef output_size,
    c10::optional<double> scales_d,
    c10::optional<double> scales_h,
    c10::optional<double> scales_w) {
  auto output = at::empty({0}, input.options());
  upsample_nearest_out_dpcpp_kernel(
      output,
      input,
      output_size,
      scales_w.has_value() ? static_cast<double>(scales_w.value()) : 0.0,
      scales_h.has_value() ? static_cast<double>(scales_h.value()) : 0.0,
      scales_d.has_value() ? static_cast<double>(scales_d.value()) : 0.0);
  return output;
}

Tensor upsample_nearest3d(
        const Tensor& input,
        c10::optional<IntArrayRef> output_size,
        c10::optional<ArrayRef<double>> scale_factors) {
  auto output = at::empty({0}, input.options());
  auto osize = compute_output_size(input.sizes(), output_size, scale_factors);
  auto scale_d = get_scale_value(scale_factors, 0);
  auto scale_h = get_scale_value(scale_factors, 1);
  auto scale_w = get_scale_value(scale_factors, 2);
  upsample_nearest_out_dpcpp_kernel(
          output,
          input,
          osize,
          scale_w.has_value() ? static_cast<double>(scale_w.value()) : 0.0,
          scale_h.has_value() ? static_cast<double>(scale_h.value()) : 0.0,
          scale_d.has_value() ? static_cast<double>(scale_d.value()) : 0.0);
  return output;
}

Tensor& upsample_nearest3d_backward_out(
    Tensor& grad_input,
    const Tensor& grad_output,
    IntArrayRef output_size,
    IntArrayRef input_size,
    c10::optional<double> scales_d,
    c10::optional<double> scales_h,
    c10::optional<double> scales_w) {
  upsample_nearest_backward_out_dpcpp_kernel(
      grad_input,
      grad_output,
      output_size,
      input_size,
      scales_w.has_value() ? static_cast<double>(scales_w.value()) : 0.0,
      scales_h.has_value() ? static_cast<double>(scales_h.value()) : 0.0,
      scales_d.has_value() ? static_cast<double>(scales_d.value()) : 0.0);
  return grad_input;
}

Tensor upsample_nearest3d_backward(
    const Tensor& grad_output,
    IntArrayRef output_size,
    IntArrayRef input_size,
    c10::optional<double> scales_d,
    c10::optional<double> scales_h,
    c10::optional<double> scales_w) {
  auto grad_input = at::empty({0}, grad_output.options());
  upsample_nearest_backward_out_dpcpp_kernel(
      grad_input,
      grad_output,
      output_size,
      input_size,
      scales_w.has_value() ? static_cast<double>(scales_w.value()) : 0.0,
      scales_h.has_value() ? static_cast<double>(scales_h.value()) : 0.0,
      scales_d.has_value() ? static_cast<double>(scales_d.value()) : 0.0);
  return grad_input;
}

Tensor upsample_nearest3d_backward(
        const Tensor& grad_output,
        c10::optional<IntArrayRef> output_size,
        IntArrayRef input_size,
        c10::optional<ArrayRef<double>> scale_factors) {
  auto osize = compute_output_size(input_size, output_size, scale_factors);
  auto scale_d = get_scale_value(scale_factors, 0);
  auto scale_h = get_scale_value(scale_factors, 1);
  auto scale_w = get_scale_value(scale_factors, 2);
  auto grad_input = at::empty({0}, grad_output.options());
  upsample_nearest_backward_out_dpcpp_kernel(
          grad_input,
          grad_output,
          osize,
          input_size,
          scale_w.has_value() ? static_cast<double>(scale_w.value()) : 0.0,
          scale_h.has_value() ? static_cast<double>(scale_h.value()) : 0.0,
          scale_d.has_value() ? static_cast<double>(scale_d.value()) : 0.0);
  return grad_input;
}

Tensor& upsample_nearest2d_out(
    Tensor& output,
    const Tensor& input,
    IntArrayRef output_size,
    c10::optional<double> scales_h,
    c10::optional<double> scales_w) {
  upsample_nearest_out_dpcpp_kernel(
      output,
      input,
      output_size,
      scales_w.has_value() ? static_cast<double>(scales_w.value()) : 0.0,
      scales_h.has_value() ? static_cast<double>(scales_h.value()) : 0.0);
  return output;
}

Tensor upsample_nearest2d(
    const Tensor& input,
    IntArrayRef output_size,
    c10::optional<double> scales_h,
    c10::optional<double> scales_w) {
  auto output = at::empty({0}, input.options());
  upsample_nearest_out_dpcpp_kernel(
      output,
      input,
      output_size,
      scales_w.has_value() ? static_cast<double>(scales_w.value()) : 0.0,
      scales_h.has_value() ? static_cast<double>(scales_h.value()) : 0.0);
  return output;
}

Tensor upsample_nearest2d(
        const Tensor& input,
        c10::optional<IntArrayRef> output_size,
        c10::optional<ArrayRef<double>> scale_factors) {
  auto output = at::empty_like(input, LEGACY_CONTIGUOUS_MEMORY_FORMAT);
  auto osize = compute_output_size(input.sizes(), output_size, scale_factors);
  auto scale_h = get_scale_value(scale_factors, 0);
  auto scale_w = get_scale_value(scale_factors, 1);
  upsample_nearest_out_dpcpp_kernel(
          output,
          input,
          osize,
          scale_w.has_value() ? static_cast<double>(scale_w.value()) : 0.0,
          scale_h.has_value() ? static_cast<double>(scale_h.value()) : 0.0);
  return output;
}

Tensor& upsample_nearest2d_backward_out(
    Tensor& grad_input,
    const Tensor& grad_output,
    IntArrayRef output_size,
    IntArrayRef input_size,
    c10::optional<double> scales_h,
    c10::optional<double> scales_w) {
  upsample_nearest_backward_out_dpcpp_kernel(
      grad_input,
      grad_output,
      output_size,
      input_size,
      scales_w.has_value() ? static_cast<double>(scales_w.value()) : 0.0,
      scales_h.has_value() ? static_cast<double>(scales_h.value()) : 0.0);
  return grad_input;
}

Tensor upsample_nearest2d_backward(
    const Tensor& grad_output,
    IntArrayRef output_size,
    IntArrayRef input_size,
    c10::optional<double> scales_h,
    c10::optional<double> scales_w) {
  auto grad_input = at::empty({0}, grad_output.options());
  upsample_nearest_backward_out_dpcpp_kernel(
      grad_input,
      grad_output,
      output_size,
      input_size,
      scales_w.has_value() ? static_cast<double>(scales_w.value()) : 0.0,
      scales_h.has_value() ? static_cast<double>(scales_h.value()) : 0.0);
  return grad_input;
}

Tensor upsample_nearest2d_backward(
        const Tensor& grad_output,
        c10::optional<IntArrayRef> output_size,
        IntArrayRef input_size,
        c10::optional<ArrayRef<double>> scale_factors) {
  auto osize = compute_output_size(input_size, output_size, scale_factors);
  auto scale_h = get_scale_value(scale_factors, 0);
  auto scale_w = get_scale_value(scale_factors, 1);
  auto grad_input = at::empty({0}, grad_output.options());
  upsample_nearest_backward_out_dpcpp_kernel(
          grad_input,
          grad_output,
          osize,
          input_size,
          scale_w.has_value() ? static_cast<double>(scale_w.value()) : 0.0,
          scale_h.has_value() ? static_cast<double>(scale_h.value()) : 0.0);
  return grad_input;
}

Tensor& upsample_nearest1d_out(
    Tensor& output,
    const Tensor& input,
    IntArrayRef output_size,
    c10::optional<double> scales) {
  upsample_nearest_out_dpcpp_kernel(
      output,
      input,
      output_size,
      scales.has_value() ? static_cast<double>(scales.value()) : 0.0);
  return output;
}

Tensor upsample_nearest1d(
    const Tensor& input,
    IntArrayRef output_size,
    c10::optional<double> scales) {
  auto output = at::empty({0}, input.options());
  upsample_nearest_out_dpcpp_kernel(
      output,
      input,
      output_size,
      scales.has_value() ? static_cast<double>(scales.value()) : 0.0);
  return output;
}

Tensor upsample_nearest1d(
    const Tensor& input,
    c10::optional<IntArrayRef> output_size,
    c10::optional<ArrayRef<double>> scale_factors) {
  auto output = at::empty_like(input, LEGACY_CONTIGUOUS_MEMORY_FORMAT);
  auto osize = compute_output_size(input.sizes(), output_size, scale_factors);
  auto scale_w = get_scale_value(scale_factors, 0);
  upsample_nearest_out_dpcpp_kernel(
          output,
          input,
          osize,
          scale_w.has_value() ? static_cast<double>(scale_w.value()) : 0.0);
  return output;
}

Tensor& upsample_nearest1d_backward_out(
    Tensor& grad_input,
    const Tensor& grad_output,
    IntArrayRef output_size,
    IntArrayRef input_size,
    c10::optional<double> scales) {
  upsample_nearest_backward_out_dpcpp_kernel(
      grad_input,
      grad_output,
      output_size,
      input_size,
      scales.has_value() ? static_cast<double>(scales.value()) : 0.0);
  return grad_input;
}

Tensor upsample_nearest1d_backward(
    const Tensor& grad_output,
    IntArrayRef output_size,
    IntArrayRef input_size,
    c10::optional<double> scales) {
  auto grad_input = at::empty({0}, grad_output.options());
  upsample_nearest_backward_out_dpcpp_kernel(
      grad_input,
      grad_output,
      output_size,
      input_size,
      scales.has_value() ? static_cast<double>(scales.value()) : 0.0);
  return grad_input;
}

Tensor upsample_nearest1d_backward(
        const Tensor& grad_output,
        c10::optional<IntArrayRef> output_size,
        IntArrayRef input_size,
        c10::optional<ArrayRef<double>> scale_factors) {
  auto osize = compute_output_size(input_size, output_size, scale_factors);
  auto scale_w = get_scale_value(scale_factors, 0);
  auto grad_input = at::empty({0}, grad_output.options());
  upsample_nearest_backward_out_dpcpp_kernel(
          grad_input,
          grad_output,
          osize,
          input_size,
          scale_w.has_value() ? static_cast<double>(scale_w.value()) : 0.0);
  return grad_input;
}

} // namespace AtenIpexTypeXPU
} // namespace at