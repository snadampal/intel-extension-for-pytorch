import torch
from typing import cast, Iterable, List, Union
# from . import _lazy_init, _lazy_call, device_count, current_device
from . import current_device, device_count
from torch import Tensor

__all__ = ['get_rng_state', 'get_rng_state_all',
           'set_rng_state', 'set_rng_state_all',
           'manual_seed', 'manual_seed_all',
           'seed', 'seed_all', 'initial_seed']


def get_rng_state(device: Union[int, str, torch.device] = 'xpu') -> Tensor:
    r"""Returns the random number generator state of the specified GPU as a ByteTensor.

    Args:
        device (torch.device or int, optional): The device to return the RNG state of.
            Default: ``'xpu'`` (i.e., ``torch.device('xpu')``, the current XPU device).

    .. warning::
        This function eagerly initializes XPU.
    """

    if isinstance(device, str):
        device = torch.device(device)
    elif isinstance(device, int):
        device = torch.device('xpu', device)
    idx = device.index
    if idx is None:
        idx = current_device()
    default_generator = torch.xpu.default_generators[idx]
    return default_generator.get_state()


def get_rng_state_all() -> List[Tensor]:
    r"""Returns a list of ByteTensor representing the random number states of all devices."""

    results = []
    for i in range(device_count()):
        results.append(get_rng_state(i))
    return results


def set_rng_state(new_state: Tensor, device: Union[int, str, torch.device] = 'xpu') -> None:
    r"""Sets the random number generator state of the specified GPU.

    Args:
        new_state (torch.ByteTensor): The desired state
        device (torch.device or int, optional): The device to set the RNG state.
            Default: ``'xpu'`` (i.e., ``torch.device('xpu')``, the current XPU device).
    """
    new_state_copy = new_state.clone(memory_format=torch.contiguous_format)
    if isinstance(device, str):
        device = torch.device(device)
    elif isinstance(device, int):
        device = torch.device('xpu', device)

    idx = cast(torch.device, device).index
    if idx is None:
        idx = current_device()
    default_generator = torch.xpu.default_generators[idx]
    default_generator.set_state(new_state_copy)


def set_rng_state_all(new_states: Iterable[Tensor]) -> None:
    r"""Sets the random number generator state of all devices.

    Args:
        new_states (Iterable of torch.ByteTensor): The desired state for each device"""
    for i, state in enumerate(new_states):
        set_rng_state(state, i)


def manual_seed(seed: int) -> None:
    r"""Sets the seed for generating random numbers for the current GPU.
    It's safe to call this function if XPU is not available; in that
    case, it is silently ignored.

    Args:
        seed (int): The desired seed.

    .. warning::
        If you are working with a multi-GPU model, this function is insufficient
        to get determinism.  To seed all GPUs, use :func:`manual_seed_all`.
    """
    seed = int(seed)
    
    idx = current_device()
    default_generator = torch.xpu.default_generators[idx]
    default_generator.manual_seed(seed)


def manual_seed_all(seed: int) -> None:
    r"""Sets the seed for generating random numbers on all GPUs.
    It's safe to call this function if XPU is not available; in that
    case, it is silently ignored.

    Args:
        seed (int): The desired seed.
    """
    seed = int(seed)

    for i in range(device_count()):
        default_generator = torch.xpu.default_generators[i]
        default_generator.manual_seed(seed)


def seed() -> None:
    r"""Sets the seed for generating random numbers to a random number for the current GPU.
    It's safe to call this function if XPU is not available; in that
    case, it is silently ignored.

    .. warning::
        If you are working with a multi-GPU model, this function will only initialize
        the seed on one GPU.  To initialize all GPUs, use :func:`seed_all`.
    """
    idx = current_device()
    default_generator = torch.xpu.default_generators[idx]
    default_generator.seed()


def seed_all() -> None:
    r"""Sets the seed for generating random numbers to a random number on all GPUs.
    It's safe to call this function if XPU is not available; in that
    case, it is silently ignored.
    """
    random_seed = 0
    seeded = False
    for i in range(device_count()):
        default_generator = torch.xpu.default_generators[i]
        if not seeded:
            default_generator.seed()
            random_seed = default_generator.initial_seed()
            seeded = True
        else:
            default_generator.manual_seed(random_seed)


def initial_seed() -> int:
    r"""Returns the current random seed of the current GPU.

    .. warning::
        This function eagerly initializes XPU.
    """

    idx = current_device()
    default_generator = torch.xpu.default_generators[idx]
    return default_generator.initial_seed()
