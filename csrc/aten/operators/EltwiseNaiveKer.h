#pragma once

#include <utils/DPCPP.h>


DPCPP_DEF_K2(EltwiseBinNaive, typename func_t, typename scalar_t);

// requirements
// 1. same strides (contiguous)
// 2. same format
template <typename func_t, typename scalar_t>
void eltwise_binary_naive_kernel(scalar_t* res_ptr,
                                 scalar_t* op1_ptr,
                                 scalar_t* op2_ptr,
                                 int nelem, const func_t& op) {
  auto& dpcpp_queue = dpcppGetCurrentQueue();
  uint64_t subgp_size = 32;
  uint64_t wg_num = nelem / subgp_size + 1;
  auto cgf = DPCPP_Q_CGF(__cgh) {
    __cgh.parallel_for<DPCPP_K(EltwiseBinNaive, func_t, scalar_t)>(
        DPCPP::nd_range<1>(DPCPP::range<1>(wg_num * subgp_size),
                           DPCPP::range<1>(subgp_size)),
        [=](DPCPP::nd_item<1> item_id) {
          auto off = item_id.get_global_linear_id();
          if (off < nelem)
            op(res_ptr + off, op1_ptr + off, op2_ptr + off);
        }
    );
  };
  DPCPP_Q_ASYNC_SUBMIT(dpcpp_queue, cgf);
}