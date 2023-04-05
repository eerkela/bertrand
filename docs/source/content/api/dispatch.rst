.. currentmodule:: pdcast

.. TODO: move prologue into description of attach()

Attach
======
If :func:`pdcast.attach() <attach>` is invoked, each of these methods will be
attached directly to ``pandas.Series`` and ``pandas.DataFrame`` objects under
the given endpoints.  The only exception is the :func:`@dispatch() <dispatch>`
decorator itself, which allows users to add new methods to this list.

These methods may not be defined for all types.  If a type does not define a
method, it will default to the standard pandas implementation if one exists.
See each method's description for a list of types that it applies to.  If there
is any doubt, an explicit check can be performed by testing whether a series'
``.element_type`` has an attribute of the same name.  If this is the case, then
the overloaded definition will be used for that series.

.. doctest:: attach

    >>> import pandas as pd
    >>> import pdcast; pdcast.attach()
    >>> hasattr(pd.Series([1, 2, 3]).element_type, "round")
    True
    >>> hasattr(pd.Series(["a", "b", "c"]).element_type, "round")
    False

If a series contains elements of more than one type, then each type must be
checked individually.

.. doctest:: attach

    >>> series = pd.Series([True, 2, 3., "4"], dtype="O")
    >>> series.element_type   # doctest: +SKIP
    CompositeType({bool, int, float, string})
    >>> [hasattr(x, "round") for x in series.element_type]  # doctest: +SKIP
    [True, True, True, False]

If :func:`pdcast.detach() <detach>` is invoked, all of the above functionality
will be removed, leaving ``pandas.Series`` and ``pandas.DataFrame`` objects in
their original form, the same as they were before
:func:`pdcast.attach() <attach>` was called.

.. doctest:: attach

    >>> pdcast.detach()
    >>> pd.Series([1, 2, 3]).cast("float")
    Traceback (most recent call last):
        ...
    AttributeError: 'Series' object has no attribute 'cast'. Did you mean: 'last'?

.. raw:: html

    <h2>Interface</h2>

.. autosummary::
    :toctree: ../../generated

    attach
    detach


.. raw:: html

    <h2>Series</h2>

.. list-table::

    * - :meth:`pandas.Series.cast`
      - TODO
    * - :meth:`pandas.Series.typecheck`
      - TODO
    * - :attr:`pandas.Series.element_type`
      - TODO
    * - :meth:`pandas.Series.round`
      - TODO
    * - :meth:`pandas.Series.dt.tz_localize`
      - TODO
    * - :meth:`pandas.Series.dt.tz_convert`
      - TODO

.. toctree::
    :hidden:

    pandas.Series.cast <abstract/Series/cast>
    pandas.Series.typecheck <abstract/Series/typecheck>
    pandas.Series.element_type <abstract/Series/element_type>
    pandas.Series.round <abstract/Series/round>
    pandas.Series.dt.tz_localize <abstract/Series/dt/tz_localize>
    pandas.Series.dt.tz_convert <abstract/Series/dt/tz_convert>
