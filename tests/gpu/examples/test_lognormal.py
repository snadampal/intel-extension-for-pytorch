import torch
from torch.testing._internal.common_utils import TestCase
import torch_ipex
import torch.nn as nn
import matplotlib.pyplot as plt
import pytest

cpu_device = torch.device('cpu')
dpcpp_device = torch.device('dpcpp')


class TestNNMethod(TestCase):
    @pytest.mark.skipif("not torch_ipex._onemkl_is_enabled()")
    def test_lognormal(self, dtype=torch.float):
        lognormal = torch.ones(1000000, device=dpcpp_device)
        lognormal.log_normal_(std=1/4)
        
        print("normal_dpcpp device", lognormal.device)
        print("normal_dpcpp", lognormal.to("cpu"))
        
        np_data = lognormal.cpu().detach().numpy()
        
        print("numpy ", np_data)
        plt.hist(np_data, 100)
        plt.show()