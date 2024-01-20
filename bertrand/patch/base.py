import pandas as pd

from pdcast.check import typecheck
from pdcast.convert import cast
from pdcast.detect import detect_type

from .round import round
from .dt.tz_convert import tz_convert
from .dt.tz_localize import tz_localize


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
    tz_localize.attach_to(pd.Series, namespace="dt")
    tz_convert.attach_to(pd.Series, namespace="dt")

    # DataFrame
    cast.attach_to(pd.DataFrame)
    detect_type.attach_to(pd.DataFrame, pattern="property", name="element_type")
    typecheck.attach_to(pd.DataFrame)


def detach() -> None:
    """Grouped :meth:`detach <pdcast.Attachable.detach>` operations.
    """
    # pylint: disable=no-member

    # Series
    pd.Series.cast.detach()
    pd.Series.element_type.detach()
    pd.Series.typecheck.detach()
    pd.Series.round.detach()
    pd.Series.dt.tz_localize.detach()
    pd.Series.dt.tz_convert.detach()

    # DataFrame
    pd.DataFrame.cast.detach()
    pd.DataFrame.element_type.detach()
    pd.DataFrame.typecheck.detach()
