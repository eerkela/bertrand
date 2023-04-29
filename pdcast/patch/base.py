import pandas as pd

from pdcast.check import typecheck
from pdcast.convert import cast
from pdcast.detect import detect_type

from .round import round


# ignore this file when doing frame-based object lookups in resolve_type()
IGNORE_FRAME_OBJECTS = True


def attach() -> None:
    """Grouped :meth:`attach_to <pdcast.Attachable.attach_to>` operations.
    """
    # Series
    cast.attach_to(pd.Series)
    detect_type.attach_to(pd.Series, pattern="property", name="element_type")
    typecheck.attach_to(pd.Series)
    round.attach_to(pd.Series)

    # DataFrame
    cast.attach_to(pd.DataFrame)
    detect_type.attach_to(pd.DataFrame, pattern="property", name="element_type")
    typecheck.attach_to(pd.DataFrame)


def detach() -> None:
    """Grouped :meth:`detach <pdcast.Attachable.detach>` operations.
    """
    # Series
    pd.Series.cast.detach()
    pd.Series.element_type.detach()
    pd.Series.typecheck.detach()
    pd.Series.round.detach()

    # DataFrame
    pd.DataFrame.cast.detach()
    pd.DataFrame.element_type.detach()
    pd.DataFrame.typecheck.detach()
