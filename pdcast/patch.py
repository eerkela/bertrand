import pandas as pd

from pdcast.check import typecheck
from pdcast.convert import cast
from pdcast.detect import detect_type


# ignore this file when doing frame-based object lookups in resolve_type()
IGNORE_FRAME_OBJECTS = True


def attach() -> None:
    """Grouped :meth:`attach_to <pdcast.Attachable.attach_to>` operations.
    """
    # .cast()
    cast.attach_to(pd.Series)
    cast.attach_to(pd.DataFrame)

    # .element_type
    detect_type.attach_to(pd.Series, pattern="property", name="element_type")
    detect_type.attach_to(pd.DataFrame, pattern="property", name="element_type")

    # .typecheck()
    typecheck.attach_to(pd.Series)
    typecheck.attach_to(pd.DataFrame)


def detach() -> None:
    """Grouped :meth:`detach <pdcast.Attachable.detach>` operations.
    """
    # .cast()
    pd.Series.cast.detach()
    pd.DataFrame.cast.detach()

    # .element_type
    pd.Series.element_type.detach()
    pd.DataFrame.element_type.detach()

    # .typecheck()
    pd.Series.typecheck.detach()
    pd.DataFrame.typecheck.detach()
