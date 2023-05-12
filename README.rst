.. NOTE: whenever a change is made to this file, make sure to update the
.. start and end lines of index.rst to allow doctests to run.

pdcast - flexible type extensions for pandas
============================================
``pdcast`` expands and enhances the existing numpy/pandas typing
infrastructure, making it easier to work with tabular data in a wide variety of
formats.

Features
--------
``pdcast`` adds support for:

*  **Abstract hierarchies** for numpy/pandas ``dtype`` objects.  These are
   lightweight, efficient, and highly extensible, with new types added in as
   little as :ref:`10 lines of code <tutorial>`.  They can use existing
   ``dtype``\ /\ ``ExtensionDtype`` definitions or *automatically generate*
   their own via the `pandas extension API
   <https://pandas.pydata.org/pandas-docs/stable/development/extending.html>`_.
   This allows users to quickly integrate arbitrary data types into the pandas
   ecosystem, with customizable behavior for each one.
*  A configurable, **domain-specific language** for resolving types.  This
   represents a superset of the existing numpy/pandas keywords and syntax, with
   support for arbitrary parametrization, configurable aliases, and
   user-definable semantics.
*  Robust **type detection** from vectorized example data.  This works
   regardless of an example's ``.dtype`` attribute, allowing ``pdcast`` to
   describe arbitrary Python iterables, including lists, tuples, generators,
   and ``dtype: object`` arrays.  In each case, inference is fast,
   customizable, and works even when the examples are of mixed type or are not
   supported by existing numpy/pandas alternatives.
*  **Efficient type checks** for arbitrary data.  This combines the above tools
   to perform ``isinstance()``-like hierarchical checks for any node in the
   ``pdcast`` type system.  If the provided data are properly labeled, then
   this is done with *O(1)* complexity, allowing users to sprinkle checks
   throughout their code without worrying about performance implications.
*  **Multiple dispatch** based on the observed types of a function's inputs.
   This works like ``@functools.singledispatch``, but can dispatch on any
   combination of positional and/or keyword arguments, each of which can be
   independently vectorized.  It can even dispatch to multiple implementations
   at once in the case of mixed data, which are processed using a
   split-apply-combine strategy.
*  **Direct integration with pandas**.  ``pdcast`` supports a functional
   approach to extending pandas with small, fully encapsulated functions
   performing special operations based on the types of their arguments.  These
   can be combined to create powerful, dynamic patches for its rich feature
   set, which can be deployed directly to pandas data structures on a global
   basis.  This allows users to surgically overload virtually any aspect of the
   pandas machinery in cases where it is broken, or to add entirely new
   behavior specific to one or more types.  The original implementations of
   these attributes can be easily recovered if necessary, and just like the
   existing pandas framework, they can be hidden behind virtual namespaces to
   avoid conflicts, similar to ``Series.dt``, ``Series.str``, etc.

.. TODO: uncomment this once the package is pushed to PyPI

   Installation
   ------------
   Wheels are built using `cibuildwheel <https://cibuildwheel.readthedocs.io/en/stable/>`_
   and are available for most platforms via the Python Package Index (PyPI).

   .. TODO: add hyperlink to PyPI page when it goes live

   .. code:: console

      (.venv) $ pip install pdcast

   If a wheel is not available for your system, ``pdcast`` also provides an sdist
   to allow pip to build from source, although doing so requires an additional
   ``cython`` dependency.

Usage
-----
In its basic usage, ``pdcast`` can easily verify the types that are present
within pandas data structures and other iterables:

.. doctest::

   >>> import pandas as pd
   >>> import pdcast; pdcast.attach()

   >>> df = pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["a", "b"]})
   >>> df.typecheck({"a": "int", "b": "float", "c": "string"})
   True
   >>> df["a"].typecheck("int")
   True

Using its more advanced features, ``pdcast`` implements its own universal
:func:`cast() <pdcast.cast>` function, which can perform arbitrary data
conversions within its expanded type system.  Here's a short walk around the
various categories that are supported out of the box (Note: ``_`` refers to the
previous output).

.. doctest::

   >>> import numpy as np

   >>> class CustomObj:
   ...     def __init__(self, x):  self.x = x
   ...     def __str__(self):  return f"CustomObj({self.x})"
   ...     def __repr__(self):  return str(self)

   >>> pdcast.to_boolean([1+0j, "False", None])  # non-homogenous
   0     True
   1    False
   2     <NA>
   dtype: boolean
   >>> _.cast(np.dtype(np.int8))
   0       1
   1       0
   2    <NA>
   dtype: Int8
   >>> _.cast("double")
   0    1.0
   1    0.0
   2    NaN
   dtype: float64
   >>> _.cast(np.complex128, downcast=True)
   0    1.0+0.0j
   1    0.0+0.0j
   2   N000a000N
   dtype: complex64
   >>> _.cast("sparse[decimal, 1]")
   0      1
   1      0
   2    NaN
   dtype: Sparse[object, Decimal('1')]
   >>> _.cast("datetime", unit="Y", since="j2000")
   0   2001-01-01 12:00:00
   1   2000-01-01 12:00:00
   2                   NaT
   dtype: datetime64[ns]
   >>> _.cast("timedelta[python]", since="Jan 1st, 2000 at 12:00 PM")
   0    366 days, 0:00:00
   1              0:00:00
   2                  NaT
   dtype: timedelta[python]
   >>> _.cast(CustomObj)
   0    CustomObj(366 days, 0:00:00)
   1              CustomObj(0:00:00)
   2                            <NA>
   dtype: object
   >>> _.cast("categorical[str[pyarrow]]")
   0    CustomObj(366 days, 0:00:00)
   1              CustomObj(0:00:00)
   2                            <NA>
   dtype: category
   Categories (2, string): [CustomObj(0:00:00), CustomObj(366 days, 0:00:00)]
   >>> _.cast("bool", true="*", false="CustomObj(0:00:00)")  # our original data
   0     True
   1    False
   2     <NA>
   dtype: boolean

New implementations for the :func:`cast() <pdcast.cast>` function can be added
dynamically, with customization for both the source and destination types.

.. doctest::

   >>> @pdcast.cast.overload("bool[python]", "int[python]")
   ... def my_custom_conversion(series, dtype, **unused):
   ...     print("calling my custom conversion...")
   ...     return series.apply(int, convert_dtype=False)

   >>> pd.Series([True, False], dtype=object).cast(int)
   calling my custom conversion...
   0    1
   1    0
   dtype: object

Finally, ``pdcast`` offers a selection of powerful tools for extending pandas
with a minimalistic, decorator-focused design.  They can be used to modify
existing behavior:

.. doctest::

   >>> @pdcast.attachable
   ... @pdcast.dispatch("self", "other")
   ... def __add__(self, other):
   ...     return getattr(self.__add__, "original", self.__add__)(other)

   >>> @__add__.overload("int", "int")
   ... def add_integer(self, other):
   ...     return self - other

   >>> __add__.attach_to(pd.Series)
   >>> pd.Series([1, 2, 3]) + 1
   0    0
   1    1
   2    2
   dtype: int64
   >>> pd.Series([1, 2, 3]) + True
   0    2
   1    3
   2    4
   dtype: int64

Or create entirely new attributes and methods above and beyond what's included
in pandas.

.. doctest::

   >>> @pdcast.attachable
   ... @pdcast.dispatch("series")
   ... def bar(series):
   ...     raise NotImplementedError("bar is only defined for floating point values")

   >>> @bar.overload("float")
   ... def float_bar(series):
   ...     print("Hello, World!")
   ...     return series

   >>> bar.attach_to(pd.Series, namespace="foo", pattern="property")
   >>> pd.Series([1.0, 2.0]).foo.bar
   Hello, World!
   0    1.0
   1    2.0
   dtype: float64
   >>> pd.Series([1, 0]).foo.bar
   Traceback (most recent call last):
      ...
   NotImplementedError: bar is only defined for floating point values


.. uncomment this when documentation goes live

   Documentation
   -------------
   Detailed documentation is hosted on readthedocs.

   .. TODO: add hyperlink once documentation goes live

License
-------
``pdcast`` is available under an
`MIT license <https://github.com/eerkela/pdcast/blob/main/LICENSE>`_.

Contact
-------
The package maintainer can be contacted via the
`GitHub issue tracker <https://github.com/eerkela/pdcast/issues>`_, or directly
at eerkela42@gmail.com.
