pandas.Series.round
===================

.. method:: pandas.Series.round(decimals: int = 0, rule: str = "half_even") -> pandas.Series:
    :abstractmethod:

    Customizable rounding for numeric series objects.

    :param decimals:
        The number of decimal places to round to.  0 indicates rounding in
        the ones position.  Positive values count to the right of the decimal
        point (if one exists) and negative values count to the left.
    :type decimals: int, default 0
    :param rule:
        The `rounding strategy <https://en.wikipedia.org/wiki/Rounding#Rounding_to_integer>`_
        to use.  Must be one of "floor", "ceiling", "down", "up", "half_floor",
        "half_ceiling", "half_down", "half_up", or "half_even".
    :type rule: str, default "half_even"
    :return:
        A series of the same type as the input, rounded to the given number of
        decimal places.
    :rtype: pandas.Series
    :raise ValueError:
        If ``rule`` does not match one of the available options.

    **Notes**

    This is an abstract method that is dispatched using the
    :func:`@dispatch() <dispatch>` decorator.  It is defined for all
    :ref:`integer <integer_types>`, :ref:`float <float_types>`,
    :ref:`complex <complex_types>`, and :ref:`decimal <decimal_types>` types.
    If a type does not implement this method, it will default back to the
    original pandas implementation.

    The available rounding rules are as follows:

        #.  ``"floor"`` - round toward negative infinity.
        #.  ``"ceiling"``` - round toward positive infinity.
        #.  ``"down"`` - round toward zero.
        #.  ``"up"`` - round away from zero.
        #.  ``"half_floor"`` - round to nearest with halves toward negative
            infinity.
        #.  ``"half_ceiling"`` - round to nearest with halves toward positive
            infinity.
        #.  ``"half_down"`` - round to nearest with halves toward zero.
        #.  ``"half_up"`` - round to nearest with halves away from zero.
        #.  ``"half_even"`` - Round to nearest with halves toward the nearest
            even value.  This is the default, matching the behavior of the
            original ``pandas.Series.round()`` implementation.  Also known as
            banker's rounding.  

    **Examples**

    The standard ``pandas.Series.round()`` implementation fails in some cases:

    .. doctest::

        >>> import pandas as pd
        >>> pd.Series([-1.2, -2.5, 3.5, 4.7], dtype="O").round()
        Traceback (most recent call last):
            ...
        TypeError: loop of ufunc does not support argument 0 of type float which has no callable rint method

    Attaching this method corrects this:

    .. doctest::

        >>> import pdcast; pdcast.attach()
        >>> pd.Series([-1.2, -2.5, 3.5, 4.7], dtype="O").round()
        0   -1.0
        1   -2.0
        2    4.0
        3    5.0
        dtype: float[python]

    Here are some examples of the various rounding rules:

    .. doctest::

        >>> series = pd.Series([-1.2, -2.5, 3.5, 4.7])
        >>> series.round(rule="floor")
        0   -2.0
        1   -3.0
        2    3.0
        3    4.0
        dtype: float64
        >>> series.round(rule="ceiling")
        0   -1.0
        1   -2.0
        2    4.0
        3    5.0
        dtype: float64
        >>> series.round(rule="down")
        0   -1.0
        1   -2.0
        2    3.0
        3    4.0
        dtype: float64
        >>> series.round(rule="up")
        0   -2.0
        1   -3.0
        2    4.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_floor")
        0   -1.0
        1   -3.0
        2    3.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_ceiling")
        0   -1.0
        1   -2.0
        2    4.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_down")
        0   -1.0
        1   -2.0
        2    3.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_up")
        0   -1.0
        1   -3.0
        2    4.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_even")
        0   -1.0
        1   -2.0
        2    4.0
        3    5.0
        dtype: float64

    For integers, the ``decimals`` argument must be negative for this method
    to have any effect.

    .. doctest::

        >>> pd.Series([1, 2, 3]).round(-1, rule="up")
        0    10
        1    10
        2    10
        dtype: int64
