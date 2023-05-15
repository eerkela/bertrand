.. currentmodule:: pdcast

.. _dispatch:

pdcast.dispatch
===============

.. autodecorator:: dispatch



.. original docstring for SeriesWrapper

    """A type-aware wrapper for ``pandas.Series`` objects.

    This is a context manager.  When used in a corresponding ``with``
    statement, it offers a view into a ``pandas.Series`` object that strips
    certain problematic information from it.  Operations can then be performed
    on the wrapped series without considering these special cases, which are
    automatically handled when the wrapper leaves its context.

    Parameters
    ----------
    series : pd.Series
        The series to be wrapped.
    hasnans : bool, default None
        Indicates whether missing values are present in the series.  This
        defaults to ``None``, meaning that missing values will be automatically
        detected when entering this object's ``with`` statement.  Explicitly
        setting this to ``False`` will skip this step, which may improve
        performance slightly.  If it is set to ``True``, then the indices of
        each missing value will still be detected, but operations will proceed
        as if some were found even if this is not the case.
    element_type : BaseType
        Specifies the element type of the series.  Only use this if you know
        in advance what elements are stored in the series.  Providing it does
        not change functionality, but may avoid a call to :func:`detect_type`
        in the context block's ``__enter__`` clause.

    Notes
    -----
    The information that is stripped by this object includes missing values,
    non-unique indices, and sparse/categorical extensions.  When a
    ``SeriesWrapper`` is invoked in a corresponding ``with`` statement, its
    index is replaced with a default ``RangeIndex`` and the old index is
    remembered.  Then, if ``hasnans`` is set to ``True`` or ``None``, missing
    values will be detected by running ``pd.isna()`` on the series.  If any
    are found, they are dropped from the series automatically, leaving the
    index unchanged.

    When the context block is exited, a new series is constructed with the
    same size as the original, but filled with missing values.  The wrapped
    series - along with any transformations that may have been applied to it -
    are then laid into this NA series, aligning on index.  The index is then
    replaced with the original, and if its ``element_type`` is unchanged, any
    sparse/categorical extensions are dynamically reapplied.  The result is a
    series that is identical to the original, accounting for any
    transformations that are applied in the body of the context block.

    One thing to note about this approach is that if a transformation **removes
    values** from the wrapped series while within the context block, then those
    values will be automatically replaced with **missing values** according to
    its ``element_type.na_value`` field.  This is useful if the wrapped logic
    coerces some of the series into missing values, as is the case when using
    the ``errors="coerce"`` argument of the various
    :ref:`conversion functions <conversions>`.  This behavior allows the
    wrapped logic to proceed *without accounting for missing values*, which
    will never be introduced unexpectedly.

    This object is an example of the Gang of Four's
    `Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_,
    which is not to be confused with python language decorators.  The wrapper
    itself "sticky", meaning that any method that produces a ``pandas.Series``
    from this wrapper will be wrapped in turn, allowing users to manipulate
    them as if they were ``pandas.Series`` objects directly without worrying
    about re-wrapping the results whenever a method is applied.

    Lastly, this object implements several convenience methods that automate
    common tasks in wrapped logic.  See each method for details.
    """