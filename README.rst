.. NOTE: whenever a change is made to this file, make sure to update the
.. start and end lines of index.rst to allow doctests to run.

pdcast - flexible type extensions for numpy/pandas
==================================================
``pdcast`` modifies the existing numpy/pandas typing infrastructure, making it
easier to work with tabular data in a variety of formats.

Features
--------
``pdcast`` adds support for:

*  **Extendable type hierarchies** for numpy/pandas ``dtype`` objects.  These
   form the backbone of the package and can be used to add arbitrary object
   types to the pandas ecosystem in as little as 10 lines of code.  Integration
   is seamless and automatic, and every aspect of a type's behavior can be
   customized as needed.
*  **A generalized mini-language** for building and resolving types, with
   user-definable aliases and semantics.  This allows types to be unambiguously
   specified while maintaining fine control over their construction and
   behavior.
*  Tools for **easy inference** and **schema validation**.  Types can be
   readily detected from example data, even if those data are non-homogenous
   or not supported by existing numpy/pandas functionality.  This can be
   customized in the same way as the type specification mini-language, and is
   more robust than simply checking an array's ``dtype`` field.  Users can even
   work with ``dtype=object`` arrays without losing confidence in their
   results.
*  **A suite of conversions** covering 9 of the most commonly-encountered data
   types: *boolean*, *integer*, *floating point*, *complex*, *decimal*
   (arbitrary precision), *datetime*, *timedelta*, *string*, *raw python
   objects*, or any combination of the above.  Each conversion is fully
   reversible, protected against overflow and precision loss, and can be
   customized on a per-type basis similar to the resolution and inferencing
   tools outlined above.
*  **Automatic method dispatching** based on observed data.  This functions in
   a manner similar to ``@functools.singledispatch``, allowing series methods
   to dispatch to multiple implementations based on the type of their
   underlying data.  This mechanism is fully general-purpose, and can be used
   to dynamically repair existing numpy/pandas functionality in cases where it
   is broken, or to extend it arbitrarily without interference.  In either
   case, ``pdcast`` handles all the necessary type checks and inferencing,
   dispatching to the appropriate implementation if one exists and falling back
   to pandas if it does not.  Original functionality can be easily recovered if
   necessary, and dispatched methods can be hidden behind temporary namespaces
   to avoid conflicts, similar to ``pd.Series.dt``, ``pd.Series.str``, etc.

Advantages over Pandas
----------------------
Compared to the existing ``astype()`` framework, ``pdcast`` is:

*  **Robust**. ``pdcast`` can handle almost any input data, even if they are
   mislabeled, malformed, imprecise, or ambiguous.  They can even be of mixed
   type and still be processed intelligibly.
*  **Flexible**.  Every aspect of ``pdcast``'s functionality can be modified or
   extended to meet one's needs, no matter how complex.
*  **Comprehensive**.  ``pdcast`` comes prepackaged with support for several
   different backends for each data type, including numpy, pandas, python, and
   pyarrow where applicable.
*  **Intuitive**.  ``pdcast`` avoids many common gotchas and edge cases that
   can crop up during type manipulations, including (but not limited to):
   missing values, precision loss, overflow, timezones, string parsing, and
   sparse/categorical data.
*  **Efficient**.  By giving users more control over the types that are present
   within their series and dataframe objects, substantial memory savings and
   performance improvements can be achieved.  Sparse data structures and
   lossless downcasting make it possible to shrink data by up to a factor of
   10 in some cases, increasing access to big data.

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

If you want to run the test suite, install the package using the optional
``pdcast[dev]`` dependencies.

.. note::
   
   Tests are still incomplete at this stage and are constantly being updated.

Demonstration
-------------
``pdcast`` can be used to easily verify the types that are present within
a pandas object:

.. doctest:: typecheck

   >>> import pandas as pd
   >>> import pdcast.attach

   >>> df = pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["a", "b"]})
   >>> df.check_type({"a": "int", "b": "float", "c": "string"})
   True

It can also be used to convert data from one representation to another.  Here
is a short walk around the various type categories that are recognized by
``pdcast`` (Note: _ refers to previous output).

.. doctest:: conversion

   >>> import numpy as np
   >>> import pdcast
   >>> import pdcast.attach

   >>> class CustomObj:
   ...     def __init__(self, x): self.x = x
   ...     def __str__(self): return f"CustomObj({self.x})"

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
   0   1971-01-01
   1   1970-01-01
   2          NaT
   dtype: datetime64[ns]
   >>> _.cast("timedelta[python]", since="Jan 1st, 2000 at 12:00 PM")
   0    365 days, 0:00:00
   1              0:00:00
   2                  NaT
   dtype: object
   >>> _.cast(CustomObj)
   0    CustomObj(365 days, 0:00:00)
   1              CustomObj(0:00:00)
   2                            <NA>
   dtype: object
   >>> _.cast("categorical[str[pyarrow]]")
   0    CustomObj(365 days, 0:00:00)
   1              CustomObj(0:00:00)
   2                            <NA>
   dtype: category
   Categories (2, string): [CustomObj(0:00:00), CustomObj(365 days, 0:00:00)]
   >>> _.cast(bool, true="*", false="CustomObj(0:00:00)")  # our original data
   0     True
   1    False
   2     <NA>
   dtype: boolean

And finally, dispatching allows users to modify series behavior on a per-type
basis.

.. NOTE: BREAK HERE IN INDEX.RST

.. doctest:: dispatch

   >>> import pandas as pd

   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round()
   Traceback (most recent call last):
      ...
   TypeError: loop of ufunc does not support argument 0 of type float which has no callable rint method

   >>> import pdcast.attach

   # pdcast defines a round() function that is type-agnostic
   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round()
   0    1.0
   1   -2.0
   2    4.0
   dtype: float64

   # original functionality can be easily recovered
   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round.original()
   Traceback (most recent call last):
      ...
   TypeError: loop of ufunc does not support argument 0 of type float which has no callable rint method

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
