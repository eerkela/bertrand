"""This package holds the glue necessary to attach and detach
``pdcast``-related functionality to/from pandas data structures, as well as
to dispatch method calls based on type.

Functions
---------
attach()
    globally attach all ``pdcast`` functionality to ``pandas.Series`` and
    ``pandas.DataFrame`` objects.

detach()
    globally detach all ``pdcast`` functionality from ``pandas.Series`` and
    ``pandas.DataFrame`` objects, returning them to their original state.

Classes
-------
Namespace
    A virtual namespace for ``pandas.Series`` instances.  These are created
    on the fly by the ``@dispatch`` decorator.

DispatchMethod
    A decorator for a series method that automatically dispatches to an
    implementation of the appropriate type.
"""
from . import series
from . import dataframe
from .virtual import DispatchMethod, Namespace


def attach() -> None:
    """Attach dispatched ``pdcast`` functionality to pandas objects."""
    series.attach()
    dataframe.attach()


def detach() -> None:
    """Remove all ``pdcast`` functionality from pandas objects."""
    series.detach()
    dataframe.detach()
