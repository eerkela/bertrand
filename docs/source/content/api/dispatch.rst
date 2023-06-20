.. currentmodule:: pdcast

.. _dispatch:

pdcast.dispatch
===============

.. autodecorator:: dispatch

.. raw:: html
    :file: ../../images/decorators/Decorators_UML.html

.. _dispatch.basic:

Dispatch functions
------------------
The :func:`@dispatch <pdcast.dispatch>` decorator transforms a function into a
:class:`DispatchFunc` object, which maintains a collection of virtual
implementations that it can dispatch to based on the observed type of its
arguments.  These have the following interface, in addition to that of the
function itself.

.. autosummary::
    :toctree: ../../generated/

    DispatchFunc
    DispatchFunc.dispatched
    DispatchFunc.overload
    DispatchFunc.fallback
    DispatchFunc.__call__
    DispatchFunc.__getitem__
    DispatchFunc.__delitem__

The decorated function itself is used as a default implementation, similar to
:func:`@functools.singledispatch <python:functools.singledispatch>`.  It will
be invoked if no other implementation can be found for a set of inputs.  Since
we haven't added any implementations yet, this will always be the case.

.. doctest::
    
    >>> @dispatch("x", "y")
    ... def add(x, y):
    ...     raise NotImplementedError(
    ...         f"no implementation found for {detect_type(x)} + {detect_type(y)}"
    ...     )

    >>> add(2, 2)
    Traceback (most recent call last):
        ...
    NotImplementedError: no implementation found for int[python] + int[python]

In general, this is a convenient place to raise an error indicating that
unexpected types were encountered.

.. note::

    If we wanted, we could also add our own logic to the base ``add()``
    function.  This would be invoked whenever we fail to find a more specific
    implementation, and might include some sort of automatic fallback behavior.

    In fact, this is exactly what :func:`cast` does under the hood; in its case
    to automatically unwrap :class:`DecoratorTypes <DecoratorType>` and retry
    conversions on their contents.

.. _dispatch.virtual:

Virtual implementations
-----------------------
Once we've created a :class:`DispatchFunc` object, we can add virtual
implementations using the :meth:`@DispatchFunc.overload <DispatchFunc.overload>`
decorator, like so:

.. doctest::

    >>> @add.overload("int[python]", "int[python]")
    ... def add_pyint_to_pyint(x, y):
    ...     return x + y

    >>> add(2, 2)
    4

Here we cover the same case we failed earlier, adding an implementation
specifically for Python integers.  It will be chosen if and only if both inputs
are of type :class:`int <python:int>`, and not in any other case.

.. doctest::

    >>> add(2, "2")
    Traceback (most recent call last):
        ...
    NotImplementedError: no implementation found for int[python] + string[python]

We can go ahead and add as many implementations as we'd like this way, mapping
each combination of input types to its own specialized function.

.. doctest::

    >>> @add.overload("int[python]", "string[python]")
    ... def add_pyint_to_string(x, y):
    ...     return str(x) + y

    >>> @add.overload("string[python]", "int[python]")
    ... def add_string_to_pyint(x, y):
    ...     return x + str(y)

    >>> add(2, "2")
    '22'
    >>> add("2", 2)
    '22'

    # etc.

But this could be tedious and error-prone, especially if several types share
logic between them.  Numpy integers, for instance, come in 8 different
varieties, each of which should be treated identically to their Python
counterparts.  We *could* add an independent implementation for each type, but
that would result in lots of duplicated code and would not be resilient to
future changes.

Instead, we can use ``pdcast``'s hierarchical :ref:`type system <types>` to
cover these cases with a simple :ref:`membership <Type.membership>` check.

.. doctest::

    >>> @add.overload("int", "int")
    ... def add_int_to_int(x, y):
    ...     return x + y

    >>> add(np.int64(2), np.int64(2))
    4
    >>> add(np.int64(2), 2)
    4
    >>> add(2, np.int32(2))
    4
    >>> add(np.int32(2), np.uint8(2))
    4

This adds a single implementation for all integer types, including both numpy
and Python integers.

.. note::

    We can check what implementation will be used for a given set of inputs by
    indexing the function with their types.

    .. doctest::

        >>> add[int, int]
        <function add_pyint_to_pyint at ...>
        >>> add[np.int64, np.int64]
        <function add_int_to_int at ...>

    As we can see, the generic implementation does not replace the
    Python-specific one we defined earlier.  This is because
    :class:`DispatchFuncs <DispatchFunc>` sort their implementations
    `topologically <https://en.wikipedia.org/wiki/Topological_sorting>`_,
    always preferring **more specific implementations** over those that are
    more general.  We can thus narrowly scope our implementations only to the
    specific cases we care about while still providing a default fallback for
    everything else.

    In our case, both implementations are identical, so let's just remove the
    Python-specific one.

    .. doctest::

        >>> del add[int, int]
        >>> add[int, int]
        <function add_int_to_int at ...>

    Now all integer types are handled by the same implementation, which should
    make it easier to maintain and troubleshoot.  We can always add more
    specific implementations later if we need to;
    :class:`DispatchFuncs <DispatchFunc>` don't care about the order in which
    their implementations are added.

Now let's add a few more implementations for other types.

.. doctest::

    >>> @add.overload("float", "float")
    ... def add_float_to_float(x, y):
    ...     return x + y

    >>> @add.overload("int", "float")
    ... def add_int_to_float(x, y):
    ...     return x + y

    >>> @add.overload("float", "int")
    ... def add_float_to_int(x, y):
    ...     return x + y

    >>> add(2.0, 2.0)
    4.0
    >>> add(2, 2.0)
    4.0
    >>> add(2.0, 2)
    4.0

Each of these implementations have the same behavior as the generic integer
case we defined eariler, but for different combinations of types.  Again, this
could result in lots of code duplication if we were to continue on like this.
However, we can simplify things even further by using
:class:`composite <CompositeType>` specifiers in our
:meth:`@overload <DispatchFunc.overload>` signature.

.. doctest::

    >>> @add.overload("int, float", "int, float")
    ... def add_number_to_number(x, y):
    ...     return x + y

    >>> add(2.0, 2.0)
    4.0
    >>> add(2, 2.0)
    4.0
    >>> add(2.0, 2)
    4.0

This applies the implementation to the `Cartesian product
<https://en.wikipedia.org/wiki/Cartesian_product>`_ of the specified types,
allowing us to add and remove implementations without worrying about
combinatoric explosions.

.. doctest::

    >>> number = "bool, int, float, complex, decimal"

    >>> @add.overload(number, number)
    ... def add_number_to_number(x, y):
    ...     return x + y

    >>> add(2, 2)
    4
    >>> add(2.0, np.int64(2))
    4.0
    >>> add(True, 3+0j)
    (4+0j)

.. note::

    We still retain the same fine-grained control we used at the beginning of
    this section, allowing us to overload specific combinations of types if we
    need to.

    .. doctest::

        >>> @add.overload("bool", "bool")
        ... def add_bool_to_bool(x, y):
        ...     if x and y:
        ...         raise TypeError(f"result is no longer a bool: {x} + {y}")
        ...     return bool(x + y)

        >>> add(False, False)
        False
        >>> add(True, False)
        True
        >>> add(False, True)
        True
        >>> add(True, True)
        Traceback (most recent call last):
            ...
        TypeError: result is no longer a bool: True + True

.. _dispatch.vectorized:

Vectorized inputs
-----------------
Whenever a :class:`DispatchFunc` is called, the specific implementation it
chooses is based on a call to :func:`detect_type` for each of its dispatched
arguments.  Thus, since :func:`detect_type` is naturally vectorized, so too is
the dispatch process.

.. doctest::

    >>> add([1, 2, 3], [4, 5, 6])
    0    5
    1    7
    2    9
    dtype: int[python]

Each vector is normalized according to the following procedure:

    #.  If it is not already a :class:`pandas.Series` object, it is coerced
        into one and labeled with the appropriate :ref:`type <types>`.
    #.  If it has a custom :attr:`index <pandas.Series.index>`, it is replaced
        with a normalized :class:`RangeIndex <pandas.RangeIndex>`.
    #.  If it contains any missing values, they are automatically dropped.

.. TODO: vectors are bound to a dataframe before filtering missing values.

The normalized vectors are then passed to the appropriate implementation, which
is free to manipulate them however it sees fit.  If the result is another
:class:`pandas.Series` or :class:`pandas.DataFrame` object, then a similar set
of steps are performed in reverse to normalize the output:

    #.  Missing values are filled with the appropriate
        :attr:`na_value <ScalarType.na_value>` according to the inferred output
        type.
    #.  The index is restored to its original state.
    #.  The output is coerced to the appropriate :ref:`type <types>` if it is
        not already valid.

.. note::

    We can observe this process by writing a new implementation for our ``add``
    function.

    .. doctest::

        >>> import pandas as pd

        >>> @add.overload("float", "float")
        ... def add_float_series_to_float_series(x, y):
        ...     print(x)
        ...     print("      +")
        ...     print(y)
        ...     print("      =")
        ...     return x + y

        >>> x = pd.Series([1.0, 2.0, np.nan, 4.0], index=["a", "b", "c", "d"], dtype="O")
        >>> x
        a    1.0
        b    2.0
        c    NaN
        d    4.0
        dtype: object

        >>> y = pd.Series([5.0, 6.0, 7.0, 8.0], index=["a", "b", "c", "d"])
        >>> y
        a    5.0
        b    6.0
        c    7.0
        d    8.0
        dtype: float64

        >>> add(x, y)
        0    1.0
        1    2.0
        3    4.0
        dtype: float64
              +
        0    5.0
        1    6.0
        3    8.0
        dtype: float64
              =
        a     6.0
        b     8.0
        c     NaN
        d    12.0
        dtype: float64

.. warning::

    Since our ``add()`` function just adds numeric series according to the
    ``+`` operator, it is automatically vectorized.  In general, however, we
    need to be careful to ensure that our implementations are properly
    vectorized if we want to use this feature.
    
    In essence, this boils down to using proper numpy/pandas functions for our
    dispatched implementations, which we should be already be doing wherever
    possible.


.. _dispatch.mixed:

Mixed data
----------
:func:`@dispatch <dispatch>` can also


.. TODO: talk about split-apply-combine and how vectors are bound into
    dataframes.

.. _dispatch.method:

Dispatch methods
----------------
We can combine the :func:`@dispatch <dispatch>` decorator with
:func:`@attachable <attachable>` to create dispatch methods.



.. _dispatch.diagram:

Activity Diagram
----------------

.. raw:: html
    :file: ../../images/decorators/Dispatch_Control.html







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
    element_type : Type
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


.. From original SeriesWrapper docs

    Notes
    -----
    ``SeriesWrapper``\s are `Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_
    wrappers around ``pd.Series`` objects.  They provide a standardized format for
    dispatch and conversion methods, and are aware of their type via
    ``pdcast.detect_type()``.  They are guaranteed to have certain qualities, which
    help to abstract away many common problems that can arise with dispatched
    logic.  These include:

    *   **No missing values**.  These are filtered out before passing on to the
        dispatched method, and are reintroduced dynamically after it is executed.
        This means that **any values that are dropped from a** ``SeriesWrapper``
        **instance will be implicitly replaced with missing values**.  This is
        intended behavior, allowing for easy coercion to NAs while maintaining the
        *no missing values* status of SeriesWrapper objects.
    *   **A unique RangeIndex** with no duplicates.  The missing value filtering
        step above requires ``SeriesWrapper``\s to be aligned on index, meaning they
        are not allowed to have duplicate values.  This is handled internally, and
        the original index is automatically replaced after missing values are
        reintroduced, leaving it unchanged.  This also allows non-homogenous series
        data to be processed, with each group being successively joined into the
        final result based on index.
    *   **No sparse/categorical input**.  If a sparse or categorical extension
        series is provided to ``SeriesWrapper``, it will be densified and stripped
        of categorical labels before being passed to dispatch logic.  This
        increases coverage for edge cases where these representations may present
        inconsistent behavior.  If a dispatched method is invoked on one of these
        objects and it does not change the element type of the series, then the
        wrappers will be reapplied programmatically, in the same way as an
        equivalent ``cast()`` operation.  Again, this is done automatically in the
        background whenever a dispatched method is called.

    In addition, ``SeriesWrapper``\s provide some utilities to make writing
    conversions and dispatch methods easier.  These include:

    *   **Caching for extreme values**, allowing ``SeriesWrapper``\s to skip
        ``min()`` and ``max()`` calculations to evaluate range.
    *   **Boundschecks** for bounded data types, which include standard
        error-handling rules.
    *   **Integer coercion** for converting real numbers to integer format, again
        with built-in error-handling rules.
    *   **Loops for applying custom logic** over series values while accounting
        for errors.

    In every other respect, they are identical to normal ``pd.Series`` objects,
    and can be used as such.

