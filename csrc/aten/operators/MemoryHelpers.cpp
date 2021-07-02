#include <core/Memory.h>
#include <core/Stream.h>

#include <utils/DPCPP.h>
#include <runtime/Utils.h>
#include "MemoryHelpers.h"

DPCPP_DEF_K1(memory_scale);
DPCPP_DEF_K1(memory_scale1);
DPCPP_DEF_K1(memory_scale2);

namespace at {
namespace AtenIpexTypeXPU {

// dst = src * alpha
void dpcppMemoryScale(
    void* dst,
    const void* src,
    size_t n_elements,
    const float alpha) {
  auto& dpcpp_queue = dpcppGetCurrentQueue();
  auto total_threads =
      dpcpp_queue.get_device().template get_info<dpcpp_dev_max_wgroup_size>();

  auto cgf = DPCPP_Q_CGF(cgh) {
    cgh.parallel_for<DPCPP_K(memory_scale)>(
        DPCPP::range<1>(total_threads), [=](DPCPP::item<1> itemId) {
          auto in_ptr = (float*)src;
          auto out_ptr = (float*)dst;
          auto id = itemId.get_id(0);
          for (auto i = id; i < n_elements; i += itemId.get_range()[0])
            out_ptr[i] = in_ptr[i] * alpha;
        });
  };

  // launch kernel
  DPCPP_Q_ASYNC_SUBMIT(dpcpp_queue, cgf);
}

// dst = src * eps + dst * (1 - eps)
void dpcppMemoryScale1(
    void* dst,
    const void* src,
    size_t n_elements,
    const double eps) {
  auto& dpcpp_queue = dpcppGetCurrentQueue();
  auto total_threads =
      dpcpp_queue.get_device().template get_info<dpcpp_dev_max_wgroup_size>();

  auto cgf = DPCPP_Q_CGF(cgh) {
    cgh.parallel_for<DPCPP_K(memory_scale1)>(
        DPCPP::range<1>(total_threads), [=](DPCPP::item<1> itemId) {
          auto in_ptr = (float*)src;
          auto out_ptr = (float*)dst;
          auto id = itemId.get_id(0);
          for (auto i = id; i < n_elements; i += itemId.get_range()[0])
            out_ptr[i] = in_ptr[i] * eps + out_ptr[i] * (1 - eps);
        });
  };

  // launch kernel
  DPCPP_Q_ASYNC_SUBMIT(dpcpp_queue, cgf);
}

// dst = src * alpha * eps + dst * (1 - eps)
void dpcppMemoryScale2(
    void* dst,
    const void* src,
    size_t n_elements,
    const float alpha,
    const double eps) {
  auto& dpcpp_queue = dpcppGetCurrentQueue();
  auto total_threads =
      dpcpp_queue.get_device().template get_info<dpcpp_dev_max_wgroup_size>();

  auto cgf = DPCPP_Q_CGF(cgh) {
    cgh.parallel_for<DPCPP_K(memory_scale2)>(
        DPCPP::range<1>(total_threads), [=](DPCPP::item<1> itemId) {
          auto in_ptr = (float*)src;
          auto out_ptr = (float*)dst;
          auto id = itemId.get_id(0);
          for (auto i = id; i < n_elements; i += itemId.get_range()[0])
            out_ptr[i] = in_ptr[i] * alpha * eps + out_ptr[i] * (1 - eps);
        });
  };

  // launch kernel
  DPCPP_Q_ASYNC_SUBMIT(dpcpp_queue, cgf);
}

} // namespace AtenIpexTypeXPU
} // namespace at