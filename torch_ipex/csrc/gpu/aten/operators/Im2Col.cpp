#include <ATen/ATen.h>
#include <ATen/div_rtn.h>
#include <ATen/native/TensorIterator.h>

#include <ATen/aten_ipex_type_dpcpp.h>
#include <core/ApplyUtils.h>
#include <core/DPCPP.h>
#include <core/DPCPPUtils.h>
#include <core/Memory.h>
#include <core/TensorImplUtils.h>

#include <utils/ATDispatch.h>

#include <mkldnn.hpp>

#include "Im2Col.h"
#include "Im2ColShapeCheck.h"

using namespace mkldnn;
using namespace at::dpcpp;
using namespace at::native;

namespace at {
namespace AtenIpexTypeDPCPP {
namespace impl {

static void im2col_out_template(
    Tensor& output,
    const Tensor& input_,
    IntArrayRef kernel_size,
    IntArrayRef dilation,
    IntArrayRef padding,
    IntArrayRef stride) {
  TORCH_CHECK(
      kernel_size.size() == 2,
      "It is expected kernel_size equals to 2, but got size ",
      kernel_size.size());

  TORCH_CHECK(
      dilation.size() == 2,
      "It is expected dilation equals to 2, but got size ",
      dilation.size());

  TORCH_CHECK(
      padding.size() == 2,
      "It is expected padding equals to 2, but got size ",
      padding.size());

  TORCH_CHECK(
      stride.size() == 2,
      "It is expected stride equals to 2, but got size ",
      stride.size());

  int64_t kernel_height = kernel_size[0];
  int64_t kernel_width = kernel_size[1];
  int64_t dilation_height = dilation[0];
  int64_t dilation_width = dilation[1];
  int64_t pad_height = padding[0];
  int64_t pad_width = padding[1];
  int64_t stride_height = stride[0];
  int64_t stride_width = stride[1];

  im2col_shape_check(
      input_,
      Tensor(),
      kernel_height,
      kernel_width,
      dilation_height,
      dilation_width,
      pad_height,
      pad_width,
      stride_height,
      stride_width);

  Tensor input = input_.contiguous();

  bool batched_input = true;

  if (input.dim() == 3) {
    batched_input = false;
    input.resize_({1, input.size(0), input.size(1), input.size(2)});
  }

  int64_t batch_size = input.size(0);
  int64_t n_input_plane = input.size(1);
  int64_t input_height = input.size(2);
  int64_t input_width = input.size(3);

  int64_t output_height = (input_height + 2 * pad_height -
                           (dilation_height * (kernel_height - 1) + 1)) /
          stride_height +
      1;
  int64_t output_width = (input_width + 2 * pad_width -
                          (dilation_width * (kernel_width - 1) + 1)) /
          stride_width +
      1;
  int64_t n_output_plane = n_input_plane * kernel_width * kernel_height;
  int64_t output_length = output_height * output_width;

  output.resize_({batch_size, n_output_plane, output_length});
  output.zero_();

  IPEX_DISPATCH_FLOATING_TYPES_AND_HALF(
      input.scalar_type(), "im2col_out_dpcpp", [&] {
        Tensor input_n;
        Tensor output_n;

        for (int64_t elt = 0; elt < batch_size; elt++) {
          input_n = input.select(0, elt);
          output_n = output.select(0, elt);

          ::im2col_kernel<scalar_t>(
              input_n.data_ptr<scalar_t>(),
              n_input_plane,
              input_height,
              input_width,
              output_height,
              output_width,
              kernel_height,
              kernel_width,
              pad_height,
              pad_width,
              stride_height,
              stride_width,
              dilation_height,
              dilation_width,
              output_n.data_ptr<scalar_t>());
        }

        if (!batched_input) {
          output.resize_({n_output_plane, output_length});
        }
      });
}

static void im2col_backward_out_template(
    Tensor& grad_input,
    const Tensor& grad_output,
    IntArrayRef input_size,
    IntArrayRef kernel_size,
    IntArrayRef dilation,
    IntArrayRef padding,
    IntArrayRef stride) {
  TORCH_CHECK(
      input_size.size() == 2,
      "It is expected input_size equals to 2, but got size ",
      input_size.size());
  at::AtenIpexTypeDPCPP::col2im_out(
      grad_input,
      grad_output,
      input_size,
      kernel_size,
      dilation,
      padding,
      stride);
}

} // namespace impl

Tensor& im2col_out(
    Tensor& out,
    const Tensor& self,
    IntArrayRef kernel_size,
    IntArrayRef dilation,
    IntArrayRef padding,
    IntArrayRef stride) {
  impl::im2col_out_template(out, self, kernel_size, dilation, padding, stride);
  return out;
}

Tensor im2col(
    const Tensor& self,
    IntArrayRef kernel_size,
    IntArrayRef dilation,
    IntArrayRef padding,
    IntArrayRef stride) {
  Tensor output = at::empty_like(self);

  impl::im2col_out_template(
      output, self, kernel_size, dilation, padding, stride);
  return output;
}

Tensor& im2col_backward_out(
    Tensor& grad_input,
    const Tensor& grad_output,
    IntArrayRef input_size,
    IntArrayRef kernel_size,
    IntArrayRef dilation,
    IntArrayRef padding,
    IntArrayRef stride) {
  impl::im2col_backward_out_template(
      grad_input,
      grad_output,
      input_size,
      kernel_size,
      dilation,
      padding,
      stride);
  return grad_input;
}

Tensor im2col_backward(
    const Tensor& grad_output,
    IntArrayRef input_size,
    IntArrayRef kernel_size,
    IntArrayRef dilation,
    IntArrayRef padding,
    IntArrayRef stride) {
  Tensor grad_input = at::empty_like(grad_output);

  impl::im2col_backward_out_template(
      grad_input,
      grad_output,
      input_size,
      kernel_size,
      dilation,
      padding,
      stride);
  return grad_input;
}

} // namespace AtenIpexTypeDPCPP
} // namespace at
