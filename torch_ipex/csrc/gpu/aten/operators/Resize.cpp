#include <ATen/ATen.h>

#include <core/Context.h>
#include <core/TensorImplUtils.h>

using namespace at::dpcpp;

namespace at {
namespace AtenIpexTypeXPU {
namespace impl {

Tensor& resize_(Tensor& self, IntArrayRef size) {
  auto* self_ = self.unsafeGetTensorImpl();
  TensorImpl_resizeImpl(self_, size, /*strides=*/c10::nullopt);
  return self;
}

Tensor& resize_as_(Tensor& self, const Tensor& the_template) {
  return impl::resize_(self, the_template.sizes());
}

} // namespace impl

Tensor& resize_(
    Tensor& self,
    IntArrayRef size,
    c10::optional<MemoryFormat> memory_format) {
  impl::resize_(self, size);
  return self;
}

Tensor& resize_as_(
    Tensor& self,
    const Tensor& the_template,
    c10::optional<MemoryFormat> memory_format) {
  return at::AtenIpexTypeXPU::resize_(
      self, the_template.sizes(), memory_format);
}
} // namespace AtenIpexTypeXPU


namespace AtenIpexTypeQuantizedXPU {
Tensor& resize_(
  Tensor& self,
  IntArrayRef size,
  c10::optional<MemoryFormat> memory_format) {
  at::AtenIpexTypeXPU::impl::resize_(self, size);
  return self;
}
} // namespace AtenIpexTypeQuantizedXPU
} // namespace at