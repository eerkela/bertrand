.. currentmodule:: pdcast

Motivation
==========
``pdcast`` is meant to be a flexible framework for writing extensions to
pandas.


resolving certain
idiosyncrasies in pandas functionality, particularly as it relates to data
cleaning, preprocessing, and `type safety <https://en.wikipedia.org/wiki/Type_safety>`_.
It allows users to easily detect and manipulate data in virtually any
representation, and to safely convert between them without losing information
in the process.

To explain this further, we need to do an in-depth examination of what these
idiosyncrasies are, and how they can adversely affect your data science
pipelines.

Type Safety
-----------






Python is a `dynamically-typed <https://realpython.com/python-type-checking/>`_
language.  This comes with a number of noteworthy benefits, many of which have
spurred the growth of Python as an easy to use, general purpose programming
language.  Simultaneously, it is also the basis for many of the complaints
against Python as just such a language.  Python is too slow?  Blame dynamic
typing.  Python uses too much memory?  Blame dynamic typing (and reference
counting).  Python is buggy?  *Blame dynamic typing.*

In order to avoid these problems, production code is often lifted out of Python
entirely, implemented in some other statically-typed language (usually C), and
then reintroduced to Python by way of the CPython interface or a compatibility
layer such as `Cython <https://cython.org/>`_, `Jython <https://www.jython.org/>`_,
`Numba <https://numba.pydata.org/>`_, or `RustPython <https://rustpython.github.io/>`_.
This is all well and good, but in so doing, one must make certain assumptions
about the data they are working with.  C integers, for instance, can be
`platform-specific <https://en.wikipedia.org/wiki/C_data_types>`_ and may not
fit arbitrary data without overflowing, like `Python integers <https://peps.python.org/pep-0237/>`_
can. Similarly, they are `unable to hold missing values <https://en.wikipedia.org/wiki/NaN#Integer_NaN>`_,
which are often encountered in real-world data.  Nevertheless, as long as one
is aware of these limitations going in, the benefits can be significant, and so
it is done regardless.

This, however, presents an entirely new problem: one of translation.  Given the
fact that there is no direct C equivalent for the built-in Python integer type,
how can we be sure that our dynamic inputs will fit within the limits of our
statically-typed variables?  In fact, how can we be certain that we're dealing
with integers at all?  What if our data is given as `datetimes <https://docs.python.org/3/library/datetime.html>`_,
for which there is no direct `C analogue <https://en.wikipedia.org/wiki/C_date_and_time_functions>`_?
Dynamic typing forces us to answer these questions at **runtime.**  We cannot
rely on a static compiler to keep things consistent for us.  If we want to be
certain, then we must insert manual checks to guarantee that the translation
occurs without error.

If we were working with scalar values, we could do this by inserting one or
more ``isinstance()`` and/or range checks, but this adds overhead and
counteracts the performance benefits we can expect to achieve.  It also fails
to address the case where our data has no analogue, for which we'd need to
implement our own custom encoding/decoding logic.  Additionally, if our data is
vectorized, then we'd need to repeat the check at every index, which might be
slow and inefficient, particularly in Python.  Of course we could just move
forward and hope we don't encounter any problems, but what if we do?  What if
our assumptions are wrong, or what if we don't know ahead of time what kind of
data we will encounter?

These are common problems in data science, where data cleaning and
preprocessing take up a significant fraction of one's time.  In this process,
missing and malformed values are the rule rather than the exception, and care
must be taken to treat them appropriately.  Most often, this involves a whole
pipeline of formatting, interpolation, cuts, biases, normalization,
conversions, and anything else a data scientist might keep in their toolkit for
just such an occassion.

This is also the exact case where reliability and performance matter most,
especially in the era of big data.  As such, one should be looking to use
statically-typed acceleration wherever possible, and indeed that's exactly
what the two most common data analysis packages (numpy and pandas) do under the
hood.  It is important to state, however, that they do not eliminate the
problems that arise when converting between `type systems <https://en.wikipedia.org/wiki/Type_system>`_;
they merely bury them beneath an extra layer of abstraction.  Occasionally,
they still rear their ugly heads.

Limitations of Numpy/Pandas
---------------------------
Consider a pandas series containing the integers 1 through 3:

.. doctest:: limitations

    >>> import pandas as pd
    >>> pd.Series([1, 2, 3])
    0    1
    1    2
    2    3
    dtype: int64

By default, this is automatically converted to a 64-bit integer data type, as
represented by its ``dtype`` attribute.  If we request a value at a specific
index of the series, it will be returned as a ``numpy.int64`` object:

.. doctest:: limitations

    >>> val = pd.Series([1, 2, 3])[0]
    >>> print(type(val), val)
    <class 'numpy.int64'> 1

So far, so good.  But what if we add a missing value to the series?

.. doctest:: limitations

    >>> pd.Series([1, 2, 3, None])
    0    1.0
    1    2.0
    2    3.0
    3    NaN
    dtype: float64

It changes to ``float64``!  This happens because ``numpy.int64`` objects
cannot contain missing values.  There is no particular bit pattern in their
binary representation that can be reserved to hold `special values <https://en.wikipedia.org/wiki/IEEE_754#Special_values>`_
like ``inf`` or ``NaN``.  This is not the case for floating points, which
`restrict a particular exponent <https://en.wikipedia.org/wiki/Double-precision_floating-point_format#Exponent_encoding>`_
specifically for such purposes.  Because of this discrepancy, pandas silently
converts our integer series into a float series to accomodate the missing
value.

Pandas does expose an ``Int64Dtype()`` that bypasses this restriction, but it
must be set manually:

.. doctest:: limitations

    >>> pd.Series([1, 2, 3, None], dtype=pd.Int64Dtype())
    0       1
    1       2
    2       3
    3    <NA>
    dtype: Int64

This means that unless you are aware of it ahead of time, your data could very
well be converted to floats *without your knowledge!* Why is this a problem?
Well, let's see what happens when our integers are very large:

.. doctest:: limitations

    >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1])
    0    9223372036854775805
    1    9223372036854775806
    2    9223372036854775807
    dtype: int64

These integers are very large indeed.  In fact, they are almost overflowing
their 64-bit buffers.  If we add 1 to this series, we might expect to
receive some kind of overflow error informing us of our potential mistake.  Do
we get such an error?

.. doctest:: limitations

    >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1]) + 1
    0    9223372036854775806
    1    9223372036854775807
    2   -9223372036854775808
    dtype: int64

No, the data type stays 64-bits wide and we simply wrap around to the
negative side of the number line.  Again, if you aren't aware of this behavior,
you might have just introduced an outlier to your data set unexpectedly.

It gets even worse when you combine large integers with missing values:

.. doctest:: limitations

    >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1, None])
    0    9.223372e+18
    1    9.223372e+18
    2    9.223372e+18
    3             NaN
    dtype: float64

As before, this converts our data into a floating point format.  What happens
if we add 1 to this series?

.. _floating_point_rounding_error:

.. doctest:: limitations

    >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1, None]) + 1
    0    9.223372e+18
    1    9.223372e+18
    2    9.223372e+18
    3             NaN
    dtype: float64

This time we don't wrap around the number line like before.  This is because in
`floating point arithmetic <https://en.wikipedia.org/wiki/Floating-point_arithmetic>`_,
we have plenty of extra numbers to work with above the normal 64-bit limit.
However, if we look at the values at each index, what integers are we actually
storing?

.. doctest:: limitations

    >>> series = pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1, None])
    >>> for val in series[:3]:
    ...     print(int(val))
    9223372036854775808
    9223372036854775808
    9223372036854775808

They're all the same!  This is an example of a
`floating point rounding error <https://en.wikipedia.org/wiki/Round-off_error>`_
in action.  Each of our integers is above the integral range of ``float64``
objects, which is defined by the number of bits in their significand 
(`53 <https://en.wikipedia.org/wiki/Double-precision_floating-point_format#IEEE_754_double-precision_binary_floating-point_format:_binary64>`_
in the case of ``float64`` objects).  Only integers within this range can be
exactly represented with exponent 1, meaning that any integer outside the range
``(-2**53, 2**53)`` must increment the exponent and therefore lose exact
integer precision.  In this case it's even worse, since our values are ~10
factors of 2 outside that range, meaning that the exponent portion of our
floats must be >= 10.  This leaves approximately ``2**10 = 1024`` unique values
that we are masking with the above data.  We can confirm this by doing the
following:

.. doctest:: limitations

    >>> import numpy as np
    >>> val = np.float64(2**63 - 1)
    >>> i, j = 0, 0
    >>> while val + i == val:  # count up
    ...     i += 1
    >>> while val - j == val:  # count down
    ...     j += 1
    >>> print(f"up: {i}\ndown: {j}\ntotal: {i + j}")
    up: 1025
    down: 513
    total: 1538

So it turns out we have over 1500 different values within error of the observed
result.  Once more, if we weren't aware of this going in to our analysis, we
may have just unwittingly introduced a form of systematic error by accident.
This is not ideal!

.. note::

    The discrepancy from our predicted value of 1024 comes from the fact
    that ``2**63 - 1`` is on the verge of overflowing past its current
    exponent.  Once we reach ``2**63``, we must increment our exponent to 11,
    giving us twice as many values above ``2**63`` as below it.

pdcast: a safer alternative
-------------------------------
Let's see how ``pdcast`` handles the above example:

.. doctest:: pdcast_intro

    >>> import pdcast
    >>> pdcast.to_integer([1, 2, 3])
    0    1
    1    2
    2    3
    dtype: int64
    >>> pdcast.to_integer([1, 2, 3]).dtype
    dtype('int64')

So far this is exactly the same as before.  However, when we add missing
values, we see how ``pdcast`` diverges from normal pandas:

.. doctest:: pdcast_intro

    >>> pdcast.to_integer([1, 2, 3, None])
    0       1
    1       2
    2       3
    3    <NA>
    dtype: Int64

Instead of coercing integers to floating point, we skip straight to the
``pd.Int64Dtype()`` implementation.  This doesn't just happen for ``int64``\s
either, it also applies for booleans and all other non-nullable data types.

.. doctest:: pdcast_intro

    >>> pdcast.to_boolean([True, False, None])
    0     True
    1    False
    2     <NA>
    dtype: boolean
    >>> pdcast.to_integer([1, 2, 3, None], "uint32")
    0       1
    1       2
    2       3
    3    <NA>
    dtype: UInt32

By avoiding a floating point intermediary, we can ensure that no data is lost
during these conversions, even if the values are very large:

.. doctest:: pdcast_intro

    >>> pdcast.to_integer([2**63 - 3, 2**63 - 2, 2**63 - 1, None])
    0    9223372036854775805
    1    9223372036854775806
    2    9223372036854775807
    3                   <NA>
    dtype: Int64

Conversions
-----------
The problems discussed above are multiplied tenfold when converting from one
representation to another.  This is where ``pdcast`` really shines.

Case study: integers & floats
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Before we dive into the differences, let's see how pandas handles conversions
in cases of precision loss and/or overflow.  We'll start with our large
integers from before:

.. testsetup:: conversions

    import numpy as np
    import pandas as pd
    import pdcast

.. doctest:: conversions

    >>> series = pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1])
    >>> series
    0    9223372036854775805
    1    9223372036854775806
    2    9223372036854775807
    dtype: int64
    >>> series.astype(float)
    0    9.223372e+18
    1    9.223372e+18
    2    9.223372e+18
    dtype: float64

As we can see, pandas doesn't even emit a warning about the precision loss we
demonstrated previously.  If we reverse this conversion, we can see why that
might be a problem:

.. doctest:: conversions

    >>> series.astype(float).astype(int)
    0   -9223372036854775808
    1   -9223372036854775808
    2   -9223372036854775808
    dtype: int64

Note that we don't get our original data back.  In fact we don't even end
up on the same side of the number line, thanks to silent overflow.  Simply by
converting our data, we have implicitly changed its value.  In contrast,
``pdcast`` requires explicit approval to change data in this way.

.. doctest:: conversions

    >>> import pdcast; pdcast.attach()
    >>> series.cast("float")
    Traceback (most recent call last):
        ...
    ValueError: precision loss exceeds tolerance 1e-06 at index [0, 1, 2]
    >>> series.cast("float", errors="coerce")
    0    9.223372e+18
    1    9.223372e+18
    2    9.223372e+18
    dtype: float64

And we can reverse our conversion without overflowing:

.. doctest:: conversions

    >>> series.cast("float", errors="coerce").cast("int")
    0    9223372036854775808
    1    9223372036854775808
    2    9223372036854775808
    dtype: uint64

Which preserves the actual value of the coerced floats.

What if we wanted to convert our series to ``int32`` rather than ``float``? 
Obviously the values won't fit, but what does pandas do in this situation?

.. doctest:: conversions

    >>> series
    0    9223372036854775805
    1    9223372036854775806
    2    9223372036854775807
    dtype: int64
    >>> series.astype(np.int32)
    0   -3
    1   -2
    2   -1
    dtype: int32

At this point, you might be tearing out your hair in frustration.  Not only
does pandas *not emit a warning* in this situation, but it also gives results
that are almost unintelligible and very likely not what we were expecting.

.. note::

    The actual values we observe here are due to the same overflow wrapping
    behavior as above, except that we're doing it with a smaller container
    (2**32 possible values vs 2**64).  This means that our nearly-overflowing
    64-bit values wrap around the number line not just once, but *32 times* to
    arrive at their final result.

In contrast, ``pdcast`` is aware of this and raises an ``OverflowError`` as
you might expect.

.. doctest:: conversions

    >>> series.cast(np.int32)
    Traceback (most recent call last):
        ...
    OverflowError: values exceed int32[numpy] range at index [0, 1, 2]

If we try to coerce the previous operation, then the overflowing values will be
replaced with NAs to avoid biasing the result:

.. doctest:: conversions

    >>> series.cast(np.int32, errors="coerce")
    0    <NA>
    1    <NA>
    2    <NA>
    dtype: Int32

If any of our values *had* fit into the available range for ``int32``, they
would have been preserved.

.. doctest:: conversions

    >>> pd.Series([1, 2, 3, 2**63 - 1]).cast(np.int32, errors="coerce")
    0       1
    1       2
    2       3
    3    <NA>
    dtype: Int32

Note that a nullable dtype is returned even though the original input had no
missing values.  ``pdcast`` knows when a value is being coerced and can adjust
accordingly.

Case study: datetimes
^^^^^^^^^^^^^^^^^^^^^
Now let's look at a different case: converting to and from **datetimes**.  You
could slowly go insane doing this in pandas:

.. figure:: /images/pandas_time_conversions_naive.png
    :align: center

    (And this doesn't even consider timezones)

Or you could let ``pdcast`` work out all the messy details for you:

.. doctest:: conversions

    >>> integers = pd.Series([1, 2, 3])
    >>> integers.cast("datetime", unit="s")
    0   1970-01-01 00:00:01
    1   1970-01-01 00:00:02
    2   1970-01-01 00:00:03
    dtype: datetime64[ns]

With expanded support for different epochs and timezones:

.. doctest:: conversions

    >>> integers.cast("datetime", unit="ns", since="j2000", tz="US/Pacific")
    0   2000-01-01 04:00:00.000000001-08:00
    1   2000-01-01 04:00:00.000000002-08:00
    2   2000-01-01 04:00:00.000000003-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> integers.cast("datetime", unit="h", since="03/27/22", tz="utc")
    0   2022-03-27 01:00:00+00:00
    1   2022-03-27 02:00:00+00:00
    2   2022-03-27 03:00:00+00:00
    dtype: datetime64[ns, UTC]

And calendar-accurate unit conversions:

.. doctest:: conversions

    >>> integers.cast("datetime", unit="Y")  # 1972 was a leap year
    0   1971-01-01
    1   1972-01-01
    2   1973-01-01
    dtype: datetime64[ns]
    >>> integers.cast("datetime", unit="M", since="utc")
    0   1970-02-01
    1   1970-03-01
    2   1970-04-01
    dtype: datetime64[ns]
    >>> integers.cast("datetime", unit="M", since=pd.Timestamp("1972-01-01"))
    0   1972-02-01
    1   1972-03-01
    2   1972-04-01
    dtype: datetime64[ns]

Without data loss:

.. doctest:: conversions

    >>> integers.cast("datetime", unit="h").cast("int", unit="h")
    0    1
    1    2
    2    3
    dtype: int64

To and from any representation:

.. doctest:: conversions

    >>> pd.Series([1.3, -4.8])
    0    1.3
    1   -4.8
    dtype: float64
    >>> _.cast("datetime", unit="D")
    0   1970-01-02 07:12:00.000000000
    1   1969-12-27 04:48:00.000000001
    dtype: datetime64[ns]
    >>> _.cast("float", unit="D")
    0    1.3
    1   -4.8
    dtype: float64

With arbitrary string parsing:

.. doctest:: conversions

    >>> pd.Series(["2022-01-12", "2022-01-30 07:30", "2022-03-27 12:00:00-0800"]).cast("datetime")
    0   2022-01-12 00:00:00
    1   2022-01-30 07:30:00
    2   2022-03-27 20:00:00
    dtype: datetime64[ns]
    >>> pd.Series(["Jan 12 2022", "January 30th, 2022 at 7:30", "27 mar 22"]).cast("datetime")
    0   2022-01-12 00:00:00
    1   2022-01-30 07:30:00
    2   2022-03-27 00:00:00
    dtype: datetime64[ns]

And support for several different datetime representations:

.. doctest:: conversions

    >>> integers.cast("datetime[pandas]", unit="s", since="jan 30 2022 at 7 AM")
    0   2022-01-30 07:00:01
    1   2022-01-30 07:00:02
    2   2022-01-30 07:00:03
    dtype: datetime64[ns]
    >>> integers.cast("datetime[python]", unit="D", since="cocoa", tz="Asia/Hong_Kong")
    0    2001-01-02 08:00:00+08:00
    1    2001-01-03 08:00:00+08:00
    2    2001-01-04 08:00:00+08:00
    dtype: datetime[python, Asia/Hong_Kong]
    >>> integers.cast("datetime[numpy]", unit="Y", since="-4713-11-24 12:00:00")
    0    -4712-11-24T12:00:00.000000
    1    -4711-11-24T12:00:00.000000
    2    -4710-11-24T12:00:00.000000
    dtype: object

.. note::

    ``pdcast`` doesn't just handle homogenous data - it can even process
    inputs that are of mixed type using a split-apply-combine strategy.
    Elements are grouped by their inferred type, converted independently, and
    then stitched together along with missing values to achieve a final
    result.

    .. doctest:: conversions

        >>> from decimal import Decimal
        >>> mixed_data = [2**63, "1979", True, 4+0j, Decimal(18), None]
        >>> pdcast.to_integer(mixed_data)
        0    9223372036854775808
        1                   1979
        2                      1
        3                      4
        4                     18
        5                   <NA>
        dtype: UInt64
        >>> pdcast.to_datetime(mixed_data)
        0       2262-04-11 23:47:16.854775
        1              1979-01-01 00:00:00
        2    1970-01-01 00:00:00.000000001
        3    1970-01-01 00:00:00.000000004
        4    1970-01-01 00:00:00.000000018
        5                             <NA>
        dtype: object

.. TODO: the datetime example from above does not result in a shared data type.
    The first result is a datetime.datetime while all others are pd.Timestamp.

.. testcleanup:: conversions

    # detach pdcast for next section
    pdcast.detach()

Type Checks
-----------
Another area where pandas could be improved is in runtime type checking.
Baseline, it includes a number of `utility functions <https://pandas.pydata.org/pandas-docs/stable/reference/arrays.html#utilities>`_
under :mod:`pandas.api.types` that are meant to do this, but each of them essentially
boils down to a naive ``.dtype`` check.  This leads to questionable (and even
inaccurate) results, such as:

.. testsetup:: validation

    import numpy as np
    import pandas as pd
    import pdcast

.. doctest:: validation

    >>> series = pd.Series([1, 2, 3], dtype="O")
    >>> pd.api.types.is_string_dtype(series)
    True

This happens because pandas stores strings as generic python objects by
default.  We can observe this by creating a basic string series.

.. doctest:: validation

    >>> pd.Series(["foo", "bar", "baz"])
    0    foo
    1    bar
    2    baz
    dtype: object

Note that the series is returned with ``dtype: object``.  This ambiguity means
that ``pd.api.types.is_string_dtype()`` (which implies specificity to strings)
has to include ``dtype: object`` in its comparisons.  Because of this, **any
series with** ``dtype: object`` **will be counted as a string series**, even
if it *does not* contain strings.  This is confusing to say the least, and
makes it practically impossible to distinguish between genuine object arrays
and those containing only strings.  Pandas does have a specialized
``pd.StringDtype()`` just to represent strings, but - like with
``pd.Int64Dtype()`` - it must be set manually, and is often overlooked in
practice.  With this dtype enabled, we can unambiguously check for strings by
doing:

.. doctest:: validation

    >>> series1 = pd.Series(["foo", "bar", "baz"], dtype=pd.StringDtype())
    >>> series2 = pd.Series([1, 2, 3], dtype="O")
    >>> pd.api.types.is_string_dtype(series1) and not pd.api.types.is_object_dtype(series1)
    True
    >>> pd.api.types.is_string_dtype(series2) and not pd.api.types.is_object_dtype(series2)
    False

But this is long and cumbersome, not to mention requiring an extra
preprocessing step to work at all.  ``pdcast`` has a better solution:

.. doctest:: validation

    >>> import pdcast; pdcast.attach()
    >>> series1.typecheck("string")
    True
    >>> series2.typecheck("string")
    False

And it even works on ``dtype: object`` series:

.. doctest:: validation

    >>> series = pd.Series(["foo", "bar", "baz"])
    >>> series
    0    foo
    1    bar
    2    baz
    dtype: object
    >>> series.typecheck("string")
    True

This is accomplished by a combination of :ref:`inference <detect_type>` and
:doc:`validation <../generated/pdcast.AtomicType.contains>`, which can
be customized on a per-type basis.  Since these functions do not rely on a
potentially inaccurate ``.dtype`` field, we can apply this to arbitrary data:

.. doctest:: validation

    >>> from decimal import Decimal
    >>> class CustomObj:
    ...     def __init__(self, x):  self.x = x

    >>> pd.Series([1, 2, 3], dtype="O").typecheck("int")
    True
    >>> pd.Series([Decimal(1), Decimal(2), Decimal(3)]).typecheck("decimal")
    True
    >>> pd.Series([CustomObj("python"), CustomObj("is"), CustomObj("awesome")]).typecheck(CustomObj)
    True

And even to non-homogenous data:

.. doctest:: validation

    >>> series = pd.Series([1, Decimal(2), CustomObj("awesome")])
    >>> series.typecheck("int, decimal, object[CustomObj]")
    True

.. note::

    :func:`typecheck` mimics the built-in ``isinstance()`` function.  If
    multiple types are provided to compare against, it will return ``True`` if
    and only if the inferred types form a **subset** of the comparison type(s).

.. testcleanup:: validation

    pdcast.detach()

Expanded Support
----------------
``pdcast`` also exposes several new types for use in pandas data structures.

Case Study: decimals
^^^^^^^^^^^^^^^^^^^^
Earlier, we saw how :ref:`floating point rounding errors <floating_point_rounding_error>`
can influence common data analysis tasks.  One way to avoid these is to use
Python's built-in `decimal <https://docs.python.org/3/library/decimal.html>`_
library, which provides data types for arbitrary precision arithmetic.  Pandas
does not expose these by default, but ``pdcast`` makes it easy to integrate
them into our typing ecosystem.

.. testsetup:: decimal_support

    import numpy as np
    import pandas as pd

.. doctest:: decimal_support

    >>> import decimal
    >>> import pdcast
    >>> pdcast.to_decimal([1, 2, 3])
    0    1
    1    2
    2    3
    dtype: decimal

Note that we get a ``dtype: decimal`` series in return.  This is because
``pdcast`` has *automatically generated* an appropriate ``ExtensionDtype`` for
this data.  In practice, this is simply a ``dtype: object`` array with some
extra functionality added on top for comparisons, coercion, and arithmetic.

.. doctest:: decimal_support

    >>> series = pdcast.to_decimal([1, 2, 3])
    >>> series.dtype
    ImplementationDtype(decimal)
    >>> series.array
    <ImplementationArray>
    [Decimal('1'), Decimal('2'), Decimal('3')]
    Length: 3, dtype: decimal
    >>> series.array.data
    array([Decimal('1'), Decimal('2'), Decimal('3')], dtype=object)

Most importantly, this label provides an explicit hook for :func:`detect_type`
to interpret.  This turns type inference into an *O(1)* operation.

.. doctest:: decimal_support

    >>> import timeit
    >>> series = pdcast.to_decimal(np.arange(10**6))
    >>> series
    0              0
    1              1
    2              2
    3              3
    4              4
            ...
    999995    999995
    999996    999996
    999997    999997
    999998    999998
    999999    999999
    Length: 1000000, dtype: decimal
    >>> timeit.timeit(lambda: pdcast.detect_type(series), number=10**3)   # doctest: +SKIP
    0.0024710440047783777

If we stored the same values in a ``dtype: object`` series, we would be forced
to iterate over the entire array to find its type.

.. doctest:: decimal_support

    >>> series = series.astype(object)
    >>> series
    0              0
    1              1
    2              2
    3              3
    4              4
            ...
    999995    999995
    999996    999996
    999997    999997
    999998    999998
    999999    999999
    Length: 1000000, dtype: object
    >>> timeit.timeit(lambda: pdcast.detect_type(series), number=1)   # doctest: +SKIP
    0.22230663200025447

This is still fast, but it's nowhere near the constant-time equivalent.  Such
speed allows us to do near-instantaneous type checks on properly-formatted
data, and as a result, we are at liberty to sprinkle these checks throughout
our code without worrying about incurring significant overhead.

.. _decimal_round_demo:

Now, if we want to use our ``decimal`` objects for some practical math, we
might notice that some things are broken.  While the normal operators work as
expected, some of the more specialized methods (like ``round()``) may not work
properly.

.. doctest:: decimal_support

    >>> series = pdcast.to_decimal([-1.8, 0.5, 1.5, 2.4])
    >>> series.round()
    Traceback (most recent call last):
        ...
    TypeError: loop of ufunc does not support argument 0 of type decimal.Decimal which has no callable rint method

This can be fixed by :func:`attaching <attach>` ``pdcast`` to pandas, which
we've been doing throughout this documentation already.  This gives us a new
implementation of the ``round()`` method designed specifically for ``decimal``
objects.

.. doctest:: decimal_support

    >>> pdcast.attach()
    >>> series.round   # doctest: +SKIP
    <pdcast.patch.virtual.DispatchMethod object at 0x7f6ad2ed9990>
    >>> series.round()
    0    -2
    1     0
    2     2
    3     2
    dtype: decimal

.. note::

    This is actually even stronger than the normal ``round()`` method since it
    allows for customizable rules.  For instance:

    .. doctest:: decimal_support

        >>> series.round(rule="down")  # toward zero
        0    -1
        1     0
        2     1
        3     2
        dtype: decimal
        >>> series.round(rule="up")  # away from zero
        0    -2
        1     1
        2     2
        3     3
        dtype: decimal

Case Study: datetimes
^^^^^^^^^^^^^^^^^^^^^
By default, pandas stores datetimes in its own ``pandas.Timestamp`` format,
which is built on top of a ``numpy.datetime64`` object with nanosecond
precision.

.. testsetup:: datetime_support

    import numpy as np
    import pandas as pd

.. doctest:: datetime_support

    >>> pd.Timestamp(1)
    Timestamp('1970-01-01 00:00:00.000000001')
    >>> pd.Timestamp(1).asm8
    numpy.datetime64('1970-01-01T00:00:00.000000001')

In essence, these are just 64-bit integers with extra support for timezones,
formatting, etc.  They simply count the number of nanoseconds from the UTC
epoch (1970-01-01 00:00:00), and as such are subject to overflow errors if the
dates are too extreme:

.. doctest:: datetime_support

    >>> pd.to_datetime(2**63 - 1)
    Timestamp('2262-04-11 23:47:16.854775807')
    >>> pd.to_datetime(2**63)
    Traceback (most recent call last):
        ...
    pandas._libs.tslibs.np_datetime.OutOfBoundsDatetime: Out of bounds nanosecond timestamp

This means that only dates within the range

.. doctest:: datetime_support

    >>> str(pd.Timestamp.min), str(pd.Timestamp.max)
    ('1677-09-21 00:12:43.145224193', '2262-04-11 23:47:16.854775807')

can actually be represented by pandas.  In most practical applications, this is
plenty, but it may not be in all cases.  In historical and astronomical data,
for instance, one may need to represent values above or below this range, and
full nanosecond precision may not be necessary.  In these cases, you'll have to
choose some other format to store your data, and consequentially, you **may not
be able to use pandas** for your analysis.  In normal python, you could
represent your dates as built-in ``datetime.datetime`` objects.

.. doctest:: datetime_support

    >>> import datetime
    >>> datetime.datetime.fromisoformat("1500-01-01")
    datetime.datetime(1500, 1, 1, 0, 0)

And in numpy, you could reduce the precision of ``datetime64`` objects to
expand their range.

.. doctest:: datetime_support

    >>> np.datetime64("1500-01-01", "us")  # with microsecond precision
    numpy.datetime64('1500-01-01T00:00:00.000000')
    >>> np.datetime64("-400-01-01", "s")  # with second precision
    numpy.datetime64('-400-01-01T00:00:00')

But if you try to store these in a ``pandas.Series``, you're left with a
``dtype: object`` series that lacks access to the ``.dt`` namespace.

.. doctest:: datetime_support

    >>> pd.Series([datetime.datetime.fromisoformat("1500-01-01")])
    0    1500-01-01 00:00:00
    dtype: object
    >>> pd.Series([np.datetime64("1500-01-01", "us")])
    0    1500-01-01T00:00:00.000000
    dtype: object
    >>> pd.Series([datetime.datetime.fromisoformat("1500-01-01")]).dt.tz_localize("US/Pacific")
    Traceback (most recent call last):
        ...
    AttributeError: Can only use .dt accessor with datetimelike values. Did you mean: 'at'?

.. TODO:
    .. note::

        Pandas has a habit of automatically converting these into
        ``pandas.Timestamp`` objects, even when it's not appropriate.

        .. doctest:: datetime_support

            >>> pd.Series(np.array(["1500-01-01"], dtype="M8[us]"))
            Traceback (most recent call last):
                ...
            pandas._libs.tslibs.np_datetime.OutOfBoundsDatetime: Out of bounds nanosecond timestamp: 1500-01-01 00:00:00

This makes them somewhat unreliable and hard to manipulate.  It also leaves you
unable to use ``pandas.to_datetime()`` to easily convert your data.  In
contrast, ``pdcast`` handles these objects gracefully:

.. doctest:: datetime_support

    >>> import pdcast
    >>> pdcast.to_datetime(2**63 - 1)
    0   2262-04-11 23:47:16.854775807
    dtype: datetime64[ns]
    >>> pdcast.to_datetime(2**63)
    0    2262-04-11 23:47:16.854775
    dtype: datetime[python]
    >>> _[0]
    datetime.datetime(2262, 4, 11, 23, 47, 16, 854775)
    >>> pdcast.to_datetime("October 12th, 400AD")
    0    0400-10-12 00:00:00
    dtype: datetime[python]
    >>> _[0]
    datetime.datetime(400, 10, 12, 0, 0)
    >>> pdcast.to_datetime("-400-01-01")
    0    -400-01-01T00:00:00.000000
    dtype: object
    >>> _[0]
    numpy.datetime64('-400-01-01T00:00:00.000000')

Which lets users represent dates all the way back to the
`age of the universe <https://en.wikipedia.org/wiki/Age_of_the_universe>`_.

.. doctest:: datetime_support

    >>> pdcast.to_datetime([-13.8e9], unit="Y")
    0    -13799998030-01-01T00:00:00
    dtype: object
    >>> _[0]
    numpy.datetime64('-13799998030-01-01T00:00:00')

And, using the same :ref:`dispatching mechanism <decimal_round_demo>` as
earlier, we can even make use of the ``.dt`` namespace just like we would for
``pandas.Timestamp`` objects.

.. doctest:: datetime_support

    >>> pdcast.attach()
    >>> pdcast.to_datetime("1500-01-01")
    0    1500-01-01 00:00:00
    dtype: datetime[python]
    >>> _.dt.tz_localize("Europe/Berlin")
    0    1500-01-01 00:00:00+00:53
    dtype: datetime[python, Europe/Berlin]
    >>> _[0]
    datetime.datetime(1500, 1, 1, 0, 0, tzinfo=<DstTzInfo 'Europe/Berlin' LMT+0:53:00 STD>)

.. warning::

    Extended datetime support is still experimental and may be subject to
    change without notice.
