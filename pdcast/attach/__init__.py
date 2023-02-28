import sys

from . import series
from . import dataframe


def detach() -> None:
    """Remove all `pdcast` functionality from pandas objects."""
    series.detach()
    dataframe.detach()

    # prepare to reimport
    sys.modules.pop("pdcast.attach")
