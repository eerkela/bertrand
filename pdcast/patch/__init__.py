from . import series
from . import dataframe
from .virtual import DispatchMethod, Namespace


def attach() -> None:
    """Attach dispatched ``pdcast`` functionality to pandas objects."""
    series.attach()
    dataframe.attach()


def detach() -> None:
    """Remove all `pdcast` functionality from pandas objects."""
    series.detach()
    dataframe.detach()
