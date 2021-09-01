#include <ATen/ATen.h>
#include <ATen/AtenIpexTypeXPU.h>
#include <ATen/Context.h>
#include <ATen/SparseTensorUtils.h>
#include <ATen/native/BinaryOps.h>
#include <ATen/native/TensorIterator.h>
#include <ATen/record_function.h>
#include <oneDNN/oneDNN.h>

#include <runtime/Utils.h>
#include <utils/DPCPP.h>

#include "Loops.h"
#include "comm/AccumulateType.h"
#include "comm/Pointwise.h"

#ifdef USE_ONEDPL
#include <oneapi/dpl/algorithm>
#include <oneapi/dpl/execution>
#include <oneapi/dpl/iterator>
#include <oneapi/dpl/numeric>
#endif

using namespace xpu::dpcpp;
using namespace at::sparse;

namespace at {
namespace AtenIpexTypeXPU {
namespace impl {

static void mul_add_kernel_dpcpp(TensorIterator& iter, Scalar alpha_scalar) {
  IPEX_DISPATCH_ALL_TYPES_AND2(
      at::ScalarType::Half,
      at::ScalarType::BFloat16,
      iter.dtype(),
      "mul_add",
      [&]() {
        auto alpha = alpha_scalar.to<scalar_t>();
        dpcpp_kernel_for_tensor_iter(
            iter, [=](scalar_t a, scalar_t b, scalar_t c) -> scalar_t {
              return a * b + alpha * c;
            });
      });
}

// Basic checking for all input tensors.
static inline void dim_check(
    const Tensor& self,
    const Tensor& other,
    const Tensor& accumu) {
  int64_t self_ndims = self.ndimension();
  int64_t other_ndims = other.ndimension();
  int64_t accumu_ndims = accumu.ndimension();

  TORCH_CHECK(
      self_ndims == other_ndims || other_ndims == accumu_ndims,
      "The dimensions of three inputs tensor not equal is not supported. ");
}

} // namespace impl

Tensor mul_add(
    const Tensor& self,
    const Tensor& other,
    const Tensor& accumu,
    Scalar alpha) {
  impl::dim_check(self, other, accumu);
  Tensor _self, _other, _accumu, result;
  if (check_has_opaque_and_no_padding({self, other, accumu})) {
    std::vector<Tensor> inputs;
    inputs.push_back(self);

    // align shape
    if (self.numel() != other.numel())
      inputs.push_back(other.expand_as(self).contiguous());
    else
      inputs.push_back(other);

    if (self.numel() != accumu.numel())
      inputs.push_back(accumu.expand_as(self).contiguous());
    else
      inputs.push_back(accumu);

    // align format
    std::vector<Tensor> _inputs;

    Tensor tar;
    for (int i = 0; i < inputs.size(); ++i) {
      if (DPCPPTensorConvertor::is_opaque_tensor(inputs[i])) {
        tar = inputs[i];
        break;
      }
    }

    auto tar_ctx = AtenIpexTypeXPU::DPCPPTensorContext::get_tensor_ctx(tar);

    for (int i = 0; i < inputs.size(); ++i) {
      if (!tar.is_same(inputs[i])) {
        Tensor cur = inputs[i];
        auto cur_ctx = AtenIpexTypeXPU::DPCPPTensorContext::get_tensor_ctx(cur);
        if (cur_ctx.meta() != tar_ctx.meta()) {
          cur = empty_opaque_tensor(
              tar_ctx.meta(), inputs[i].options(), c10::nullopt);
          xpu::oneDNN::reorder(inputs[i], cur);
        }
        _inputs.push_back(cur);
      } else {
        _inputs.push_back(tar);
      }
    }
    _self = _inputs.at(0);
    _other = _inputs.at(1);
    _accumu = _inputs.at(2);
    result = empty_opaque_tensor(tar_ctx.meta(), tar.options(), c10::nullopt);
  } else {
    _self = to_plain_if_needed(self);
    _other = to_plain_if_needed(other);
    _accumu = to_plain_if_needed(accumu);
    result = at::empty_like(self);
  }

  auto iter = TensorIteratorConfig()
                  .set_check_mem_overlap(true)
                  .add_output(result)
                  .add_input(_self)
                  .add_input(_other)
                  .add_input(_accumu)
                  .build();
  impl::mul_add_kernel_dpcpp(iter, alpha);
  TORCH_INTERNAL_ASSERT(result.scalar_type() == iter.output().dtype());
  return result;
}

template <typename scalar_t>
static inline void packed_add_kernel(
    unsigned short* __restrict__ w_MSB,
    unsigned short* __restrict__ w_LSB,
    const at::BFloat16* __restrict__ gw,
    int num_elem,
    float lr) {
  union packed_bf16 {
    unsigned short s[2];
    float f;
  };
  auto& dpcpp_queue = dpcppGetCurrentQueue();

  auto cgf = DPCPP_Q_CGF(cgh) {
    auto MSB_data = w_MSB;
    auto LSB_data = w_LSB;
    auto gw_data = gw;

    cgh.parallel_for(DPCPP::range<1>(num_elem), [=](DPCPP::item<1> item) {
      int64_t gid = item.get_linear_id();
      auto MSB_p = MSB_data;
      auto LSB_p = LSB_data;
      auto gw_p = gw_data;

      packed_bf16 p16;
      p16.s[0] = LSB_p[gid];
      p16.s[1] = MSB_p[gid];
      p16.f += lr * (float)(gw_p[gid]);
      LSB_p[gid] = p16.s[0];
      MSB_p[gid] = p16.s[1];
    });
  };
  DPCPP_Q_ASYNC_SUBMIT(dpcpp_queue, cgf);
}

template <typename scalar_t>
static inline void sparse_packed_add_kernel(
    unsigned short* __restrict__ w_MSB,
    unsigned short* __restrict__ w_LSB,
    const at::BFloat16* __restrict__ values,
    int64_t* indices1D,
    int64_t* origIndices,
    int64_t* uniqueOffsets,
    int64_t stride,
    int64_t nnz,
    float lr) {
#ifndef USE_ONEDPL
  throw std::runtime_error("no oneDPL found when compile");
#else
  using accscalar_t = acc_type<scalar_t>;
  union packed_bf16 {
    unsigned short s[2];
    float f;
  };
  auto& dpcpp_queue = dpcppGetCurrentQueue();
  auto policy = oneapi::dpl::execution::make_device_policy(dpcpp_queue);

  int64_t newNnz;
  {
    auto countIterI = oneapi::dpl::counting_iterator<int64_t>(0);
    auto countIterO = oneapi::dpl::counting_iterator<int64_t>(0);

    std::copy(policy, countIterI, countIterI + nnz, origIndices);
    std::copy(policy, countIterO, countIterO + nnz, uniqueOffsets);

    // auto indices1D_ptr = indices1D.data_ptr<int64_t>();
    auto zipped_indices =
        oneapi::dpl::make_zip_iterator(indices1D, origIndices);
    std::sort(
        policy, zipped_indices, zipped_indices + nnz, [](auto lhs, auto rhs) {
          using std::get;
          return get<0>(lhs) < get<0>(rhs);
        });
    auto zipped_uniqueOffsets =
        oneapi::dpl::make_zip_iterator(indices1D, uniqueOffsets);
    auto newEnd = std::unique(
        policy,
        zipped_uniqueOffsets,
        zipped_uniqueOffsets + nnz,
        [](auto lhs, auto rhs) {
          using std::get;
          return get<0>(lhs) == get<0>(rhs);
        });
    newNnz = std::distance(zipped_uniqueOffsets, newEnd);
  }

  const int num_group_0 = CeilDiv(newNnz, (int64_t)4);
  const int num_group_1 = CeilDiv(stride, (int64_t)64);

  auto cgf = DPCPP_Q_CGF(cgh) {
    // auto newValues_data = newValues.data_ptr<scalar_t>();
    auto kfn = DPCPP_Q_KFN(DPCPP::nd_item<2> item) {
      auto MSB_p = w_MSB;
      auto LSB_p = w_LSB;
      auto uniqueOffsets_ptr = uniqueOffsets;
      auto origIndices_ptr = origIndices;
      auto values_ptr = values;
      auto indices1D_ptr = indices1D;
      // auto newValues_ptr = newValues_data;

      int seg = item.get_global_id()[0];

      if (seg < newNnz) {
        const int newValueRow = seg * stride;
        const int begin = uniqueOffsets_ptr[seg];
        const int end = (seg < newNnz - 1) ? uniqueOffsets_ptr[seg + 1] : nnz;
        const int featureDim = item.get_global_id()[1];

        accscalar_t tmp = 0;
        for (int row = begin; row < end; row++) {
          const int valueRow = ((int)origIndices_ptr[row]) * stride;
          if (featureDim < stride) {
            tmp += static_cast<accscalar_t>(values_ptr[valueRow + featureDim]);
          }
        }
        if (featureDim < stride) {
          const int weight_index = indices1D_ptr[seg] * stride + featureDim;
          packed_bf16 p16;
          p16.s[0] = LSB_p[weight_index];
          p16.s[1] = MSB_p[weight_index];
          p16.f += lr * (float)(tmp);
          LSB_p[weight_index] = p16.s[0];
          MSB_p[weight_index] = p16.s[1];
          // newValues_ptr[newValueRow + featureDim] =
          // static_cast<scalar_t>(tmp);
        }
      }
    };

    // kick off kernel
    cgh.parallel_for(
        DPCPP::nd_range<2>(
            DPCPP::range<2>(num_group_0 * 4, num_group_1 * 64),
            DPCPP::range<2>(4, 64)),
        kfn);
  };
  DPCPP_Q_ASYNC_SUBMIT(dpcpp_queue, cgf);
#endif
}

Tensor packed_add(
    at::Tensor& top_half,
    at::Tensor& bot_half,
    const at::Tensor& grad,
    float alpha) {
  RECORD_FUNCTION(
      "packed_add", std::vector<c10::IValue>({top_half, bot_half, grad}));
  if (grad.is_sparse()) {
    Tensor values = grad._values();
    LongTensor indices = grad._indices();
    int64_t nDim = top_half.dim();
    int64_t nDimI = grad.sparse_dim();
    const int64_t nnz = grad._nnz();
    LongTensor indices1D = flatten_indices(indices, grad.sizes(), 0);
    int64_t view_rows = 1;
    int64_t view_columns = 1;
    for (int i = 0; i < nDimI; i++) {
      view_rows *= top_half.size(i);
    }
    for (int i = nDimI; i < nDim; i++) {
      view_columns *= top_half.size(i);
    }

    Tensor top_half_view = top_half.view({view_rows, view_columns});
    Tensor bot_half_view = bot_half.view({view_rows, view_columns});
    values = values.contiguous();
    int64_t stride = at::prod_intlist(values.sizes().slice(1));
    LongTensor origIndices = at::empty({nnz}, indices.options());
    LongTensor uniqueOffsets = at::empty({nnz}, indices.options());

    IPEX_DISPATCH_FLOATING_TYPES_AND(
        at::ScalarType::BFloat16,
        top_half.scalar_type(),
        "sparse_packed_add_kernel",
        [&]() {
          sparse_packed_add_kernel<scalar_t>(
              (unsigned short*)top_half.data_ptr<scalar_t>(),
              (unsigned short*)bot_half.data_ptr<scalar_t>(),
              values.data_ptr<at::BFloat16>(),
              indices1D.data_ptr<int64_t>(),
              origIndices.data_ptr<int64_t>(),
              uniqueOffsets.data_ptr<int64_t>(),
              stride,
              nnz,
              static_cast<float>(alpha));
        });
  } else {
    IPEX_DISPATCH_FLOATING_TYPES_AND(
        at::ScalarType::BFloat16,
        top_half.scalar_type(),
        "packed_add_kernel",
        [&]() {
          packed_add_kernel<scalar_t>(
              (unsigned short*)top_half.data_ptr<scalar_t>(),
              (unsigned short*)bot_half.data_ptr<scalar_t>(),
              grad.data_ptr<at::BFloat16>(),
              top_half.numel(),
              static_cast<float>(alpha));
        });
  }
  return top_half;
}

template <typename scalar_t>
static inline void fusion_amdd_kernel(
    scalar_t* __restrict__ p,
    scalar_t* __restrict__ d_p,
    scalar_t* __restrict__ buf,
    size_t num_element,
    float weight_decay,
    float momentum,
    float dampening,
    float lr) {
  auto& dpcpp_queue = dpcppGetCurrentQueue();

  auto cgf = DPCPP_Q_CGF(cgh) {
    auto w = p;
    auto m_buf = buf;
    auto gw = d_p;

    cgh.parallel_for(DPCPP::range<1>(num_element), [=](DPCPP::item<1> item) {
      auto id = item.get_linear_id();
      gw[id] += w[id] * weight_decay;
      m_buf[id] = m_buf[id] * momentum + gw[id];
      w[id] += m_buf[id] * lr;
    });
  };
  DPCPP_Q_ASYNC_SUBMIT(dpcpp_queue, cgf);
}

Tensor fusion_amdd(
    at::Tensor& p,
    at::Tensor& d_p,
    at::Tensor& buf,
    float weight_decay,
    float momentum,
    float dampening,
    float lr) {
  // RECORD_FUNCTION("fusion_amdd", std::vector<c10::IValue>({top_half,
  // bot_half, grad})); There is only for FP32 update
  IPEX_DISPATCH_FLOATING_TYPES_AND(
      at::ScalarType::BFloat16, d_p.scalar_type(), "fusion_amdd_kernel", [&]() {
        fusion_amdd_kernel<scalar_t>(
            p.data_ptr<scalar_t>(),
            d_p.data_ptr<scalar_t>(),
            buf.data_ptr<scalar_t>(),
            p.numel(),
            static_cast<float>(weight_decay),
            static_cast<float>(momentum),
            static_cast<float>(dampening),
            static_cast<float>(lr));
      });
  return p;
}

} // namespace AtenIpexTypeXPU
} // namespace at