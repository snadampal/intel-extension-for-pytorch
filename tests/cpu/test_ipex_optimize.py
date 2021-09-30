import torch
import intel_extension_for_pytorch as ipex
from torch.testing._internal.common_utils import TestCase
import unittest
import itertools
import copy

class TestModule(torch.nn.Module):
    def __init__(self):
        super(TestModule, self).__init__()
        self.linear = torch.nn.Linear(5, 10)
        self.conv = torch.nn.Conv2d(1, 10, 5, 1)
        self.bn = torch.nn.BatchNorm2d(num_features=10)
        self.embeddingbag = torch.nn.EmbeddingBag(10, 3, mode='sum')

    def forward(self, x, y, indices, offsets):
        x = self.conv(x)
        x = self.bn(x)
        y = self.linear(y)
        z = self.embeddingbag(indices, offsets)
        return x + y


class ConvBatchNorm(torch.nn.Module):
    def __init__(self,):
        super(ConvBatchNorm, self).__init__()
        self.conv = torch.nn.Conv2d(3, 64, kernel_size=(7, 7), stride=(2, 2), padding=(3, 3), bias=False)
        self.bn = torch.nn.BatchNorm2d(64, eps=1e-05, momentum=0.1, affine=True, track_running_stats=True)

    def forward(self, x):
        return self.bn(self.conv(x))

class TestOptimizeCases(TestCase):
    def test_optimize_parameters_behavior(self):
        model = ConvBatchNorm().eval()
        for level in ["O0", "O1"]:
            # disbale conv_bn folding
            opt_M = ipex.optimize(model, level=level, dtype=torch.float, conv_bn_folding=False)
            with torch.no_grad():
                x = torch.randn(1, 3, 224, 224)
                traced_model = torch.jit.trace(opt_M, x)
                trace_graph = traced_model.graph_for(x)
            self.assertTrue(any(n.kind() == "aten::batch_norm" for n in trace_graph.nodes()))
            # TODO check weight_prepack.

    def test_optimize_inplace_behavior_eval_mode(self):
          M_ori = TestModule()
          options = itertools.product([torch.float32, torch.bfloat16], ["O0", "O1"])
          for dtype, level in options:
              # non-inplace
              M = copy.deepcopy(M_ori).eval()
              opt_M = ipex.optimize(M, dtype=dtype, level=level, inplace=False)
              self.assertTrue(M.linear.weight.data_ptr() != opt_M.linear.weight.data_ptr())
              self.assertTrue(M.conv.weight.data_ptr() != opt_M.conv.weight.data_ptr())
              self.assertTrue(M.embeddingbag.weight.data_ptr() != opt_M.embeddingbag.weight.data_ptr())

              # inplace
              M = copy.deepcopy(M_ori).eval()
              opt_M = ipex.optimize(M, dtype=dtype, level=level, inplace=True)
              # fused part cannot be inplaced
              if level == "O1":
                  self.assertTrue(M.conv.weight.data_ptr() != opt_M.conv.weight.data_ptr())
                  self.assertTrue(M.linear.weight.data_ptr() == opt_M.linear.weight.data_ptr())
              # non optimized part should be inplaced
              self.assertTrue(M.embeddingbag.weight.data_ptr() == opt_M.embeddingbag.weight.data_ptr())

    def test_optimize_inplace_behavior_training_mode_with_optimizer(self):
          M_ori = TestModule()
          options = itertools.product([torch.float32, torch.bfloat16], ["O0", "O1"])
          for dtype, level in options:
              # non-inplace
              M = copy.deepcopy(M_ori).train()
              sgd = torch.optim.SGD(M.parameters(), lr=0.1)
              opt_M, _ = ipex.optimize(M, dtype=dtype, optimizer=sgd, level=level, inplace=False)
              self.assertTrue(M.linear.weight.data_ptr() != opt_M.linear.weight.data_ptr())
              self.assertTrue(M.conv.weight.data_ptr() != opt_M.conv.weight.data_ptr())
              self.assertTrue(M.embeddingbag.weight.data_ptr() != opt_M.embeddingbag.weight.data_ptr())
              if level == "O1":
                  self.assertEqual(M.linear.weight.dtype, torch.float)
                  self.assertEqual(M.conv.weight.dtype, torch.float)
                  self.assertEqual(M.embeddingbag.weight.dtype, torch.float)
                  self.assertEqual(M.bn.weight.dtype, torch.float)
                  self.assertEqual(opt_M.linear.weight.dtype, dtype)
                  self.assertEqual(opt_M.conv.weight.dtype, dtype)
                  self.assertEqual(opt_M.embeddingbag.weight.dtype, dtype)
                  self.assertEqual(opt_M.bn.weight.dtype, torch.float)

              # inplace
              M = copy.deepcopy(M_ori).train()
              sgd = torch.optim.SGD(M.parameters(), lr=0.1)
              opt_M, _ = ipex.optimize(M, dtype=dtype, optimizer=sgd, level=level, inplace=True)
              self.assertTrue(M.linear.weight.data_ptr() == opt_M.linear.weight.data_ptr())
              self.assertTrue(M.conv.weight.data_ptr() == opt_M.conv.weight.data_ptr())
              self.assertTrue(M.embeddingbag.weight.data_ptr() == opt_M.embeddingbag.weight.data_ptr())
              if level == "O1":
                  self.assertEqual(M.linear.weight.dtype, dtype)
                  self.assertEqual(M.conv.weight.dtype, dtype)
                  self.assertEqual(M.embeddingbag.weight.dtype, dtype)
                  self.assertEqual(M.bn.weight.dtype, torch.float)

    def _test_tensor_convert(self, tensor, bf16_tensor):
        top_half, bot_half = torch.ops.torch_ipex.split_float_bfloat16(tensor)
        # truncated top half should equal with convert fp32 to bf16 by ".bfloat()"
        self.assertEqual(bf16_tensor, top_half)
        # recovery float tensor with top half and bottom half
        float_tensor = torch.ops.torch_ipex.cat_bfloat16_float(top_half, bot_half)
        self.assertEqual(tensor, float_tensor)
        self.assertEqual(tensor.stride(), top_half.stride())
        self.assertEqual(tensor.stride(), float_tensor.stride())

    def test_tensor_convert(self):
        # contiguous case
        tensor = torch.rand(100, 100)
        self._test_tensor_convert(tensor, tensor.bfloat16())
        # transposed case
        self._test_tensor_convert(tensor.t(), tensor.bfloat16().t())
        # sliced-out case
        self._test_tensor_convert(tensor[2:5, 2:5], tensor.bfloat16()[2:5, 2:5])
        # nc11 channel-last case
        tensor = torch.rand(128, 256, 1, 1).to(memory_format=torch.channels_last)
        self._test_tensor_convert(tensor, tensor.bfloat16())

if __name__ == '__main__':
    test = unittest.main()