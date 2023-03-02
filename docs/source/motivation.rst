Motivation
==========

Static vs Dynamic Typing
------------------------
Python is a dynamically-typed language.  This design comes with a number of
noteworthy benefits, many of which have spurred the growth of Python as an
easy to use, general purpose programming language.  Simultaneously, it is also
the basis for most of the major complaints against Python as just such a
language.  Python is too slow?  Blame dynamic typing.  Python uses too much
memory?  Blame dynamic typing (and reference counting).  Python is buggy?
*Blame dynamic typing.*

In order to avoid these problems, performance-critical code is often lifted out
of python entirely, implemented in some other statically-typed language
(usually C), and then reintroduced to python by way of an interface layer, such
as Cython, Jython, Numba, RustPython, or some other tool.  Now, this is all
well and good, but in so doing, one must make certain assumptions about the
data they are working with.  C integers, for instance, are platform-specific
and may not fit arbitrary data without overflowing, like python integers can.
Similarly, they are unable to hold missing values, which are often encountered
in real-world data.  Nevertheless, as long as one is aware of these limitations
going in, the benefits can be significant, and so it is done regardless.

However, this presents an entirely new problem: one of translation.  Given the
fact that there is no direct C equivalent for the built-in python integer type,
how can I be sure that my inputs will fit within the limits of my
statically-typed functions?  If I'm working with scalar values, I could insert
one or more isinstance() and/or range checks to work it out manually, but this
adds overhead to every function call I make and counteracts the performance
benefits I can expect to achieve. Of course I could just move forward with the
function call and hope I don't encounter any problems, and 64 bits is generally
enough for most applications, but what if it's not?  What if I can't trust the
person who is handing me the data?

This is a common problem in data science, where data cleaning and preprocessing
take up a significant fraction of one's time.  In this process, missing and
malformed values are the rule rather than the exception, and care must be taken
to treat them appropriately.  Most often, this involves a whole pipeline of
data visualizations, normalization, cuts, biases, conversions, projections,
smoothing, and anything else a data scientist might keep in their toolkit for
just such an occassion.

Paradoxically, this is also the exact case where performance matters most,
especially in the era of big data.  As such, one should be looking to use
statically-typed acceleration wherever possible, and indeed this is exactly
what the two most common data analysis packages (numpy and pandas) do by
default.  It is important to state, however, that they do not eliminate the
problems that arise when converting from dynamic to static typing; they merely
bury them beneath an extra layer of abstraction.  Occasionally, they still rear
their ugly heads.

Limitations of Numpy/Pandas
---------------------------
Consider a pandas Series containing the integers 1 through 3:

.. testsetup::

    import pandas as pd

.. doctest::

    >>> pd.Series([1, 2, 3])

By default, this is automatically converted to a 64-bit integer data type, as
represented by its corresponding ``dtype`` field, like so:

.. doctest::

    >>> pd.Series([1, 2, 3]).dtype

If I request a value at a specific index of the series, it will be returned
as an ``int64`` object, as expected:

.. doctest::

    >>> val = pd.Series([1, 2, 3])[1]
    >>> type(val), val
    <class <'numpy.int64'>> 1

So far, so good.  But what if I add a missing value to the series?

.. doctest::

    >>> pd.Series([1, 2, 3, None])

It changes to ``float64``.  This happens because ``np.int64`` objects cannot
contain missing values.  There is no particular bit pattern in their binary
representation that can be reserved to hold special values like ``inf`` or
``NaN``.  This is not the case for floating point values, which restrict a
particular exponent specifically for such purposes.  Because of this
discrepancy, pandas silently converts our integer series into a float series to
accomodate the missing value.

Pandas does expose an ``Int64Dtype()`` object which bypasses this restriction,
but it must be set manually:

.. doctest::

    >>> pd.Series([1, 2, 3, None], dtype=pd.Int64Dtype())

This means that unless you are aware of it ahead of time, your data could very
well be converted to a floating point representation without your knowledge!
Why is this a problem?  Well, let's see what happens when our integers are very
large:

.. doctest::

    >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1])

These integers are very large indeed.  In fact, they are almost overflowing
their 64-bit buffer.  If we add 1 to this series, we might expect to
receive some kind of overflow error informing us of our potential mistake.  Do
we get such an error?

.. doctest::

    >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1]) + 1

No, the data type stays 64-bits wide and we simply wrap around to the
negative side of the number line.  Again, if you aren't aware of this behavior,
you might have just introduced an outlier to your data set unexpectedly.

It gets even worse when you introduce missing values:

.. doctest::

    >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1, None])

As before, this converts our data into a floating point format.  What happens
if we add 1 to this series?

.. doctest::

    >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1, None]) + 1

This time we don't wrap around like before.  This is because in floating point
format, we have plenty of extra numbers to work with above the normal 64-bit
limit.  However, if we look at the values at each index, what integers are we
actually storing?

.. doctest::

    >>> series = pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1, None]) + 1
    >>> for val in series[:3]:
    ...     print(int(val))

They're all the same!  This is an example of floating point rounding errors
in action.  Each of these integers is above the integral range of ``float64``
objects, which is defined by the number of bits in their significand (53 in the
case of ``float64`` objects).  Only integers within this range can be exactly
represented with exponent 1, meaning that any integer outside the range
``(-2**53, 2**53)`` must increment the exponent and lose integer precision.  In
this case it's even worse, since our values are ~10 factors of 2 outside that
range, meaning that exponent portion of our floating point numbers must be
>= 10.  This leaves approximately 2**10 = 1024 values that we are aliasing with
the above data.  We can confirm this by doing the following:

.. doctest::

    >>> val = np.float64(2**63 - 1)
    >>> i, j = 0, 0
    >>> while val + i == val:  # count up
    ...     i += 1
    >>> while val - j == val:  # count down
    ...     j += 1
    >>> print(i + j)

So it turns out we have over 1500 different values within error of the observed
result.  The discrepancy with our predicted value of 1024 comes from the fact
that we are at the top end of what is allowable with exponent 10.  Once we
reach 2**63, we must expand our exponent to 11, giving us twice as many values
above 2**63 as below it.

Once more, if we weren't aware of this going in to our analysis, we
may have just unwittingly introduced systematic error by accident.  This is
not ideal!

``pdcast``: a safer alternative
-------------------------------








Suppose for a moment you are programming a object-oriented data science
package.  Your objects take in data frames and provide a standard interface for
manipulating them.  Perhaps you've added some fancy machine learning 

You've written all your complicated implementation
code 