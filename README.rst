pdcast - flexible type extensions for numpy/pandas
==================================================

``pdcast`` modifies the existing numpy/pandas typing infrastructure, making it
easier to work with tabular data in a wide variety of representations.

Features
--------
``pdcast`` adds support for:

*  :ref:`Extendable type hierarchies <type_index>` for numpy/pandas ``dtype``
   objects.  These form the backbone of the package and can be used to add
   arbitrary object types to the pandas ecosystem in as little as 10 lines of
   code.  Integration is seamless and automatic, and every aspect of a type's
   behavior can be customized as needed.
*  :ref:`A generalized mini-language <type_specification>` for building and
   resolving types, with user-definable aliases and semantics.  This allows
   types to be unambiguously specified while maintaining fine control over
   their construction and behavior.
*  Tools for **easy inference** and **schema validation**.  Types can be
   readily detected from example data, even if those data are non-homogenous
   or not supported by existing numpy/pandas functionality.  This can be
   customized in the same way as the type specification mini-language, and is
   more robust than simply checking an array's ``dtype`` field.  Users can even
   work with ``dtype=object`` arrays without losing confidence in their
   results.
*  **A suite of conversions** covering 9 of the most commonly-encountered data
   types: *boolean*, *integer*, *floating point*, *complex*, *decimal*
   (arbitrary precision), *datetime*, *timedelta*, *string*, and *raw python
   objects*.  Each conversion is fully reversible, protected against overflow
   and precision loss, and can be customized on a per-type basis similar to the
   resolution and inferencing tools outlined above.
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
   10 in some cases while simultaneously accelerating downstream analysis and
   increasing access to big data.

Installation
------------
Wheels are available for most platforms via the Python Package Index (PyPI).

.. TODO: add hyperlink to PyPI page.

.. code-block:: console

   (.venv) $ pip install pdcast

If a wheel is not available for your system, ``pdcast`` also provides an sdist
to allow pip to install from source, although doing so requires an additional
``cython`` dependency.

If you want to run the built-in test suite, install the package using the
optional ``pdcast[dev]`` dependencies.

.. note::
   
   Tests are still incomplete at this stage and are constantly being updated.

Demonstration
-------------
``pdcast`` can be used to easily verify the types that are present within
a pandas object:

.. doctest::

   >>> import pandas as pd
   >>> import pdcast.attach

   >>> df = pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["a", "b"]})
   >>> df.check_type({"a": "int", "b": "float", "c": "string"})
   True

It can also be used to convert data from one representation to another.  Here
is a short walk around the various type categories that are recognized by
``pdcast``.

.. doctest::

   >>> import numpy as np
   >>> import pdcast
   >>> import pdcast.attach

   >>> data = pdcast.to_boolean([1+0j, "False", None])  # non-homogenous
   >>> data
   0     True
   1    False
   2     <NA>
   dtype: boolean
   >>> data = data.cast(np.dtype(np.int8))
   >>> data
   0       1
   1       0
   2    <NA>
   dtype: Int8
   >>> data = data.cast("double")
   >>> data
   0    1.0
   1    0.0
   2    NaN
   dtype: float64
   >>> data = data.cast(np.complex128, downcast=True)
   >>> data
   0    1.0+0.0j
   1    0.0+0.0j
   2   N000a000N
   dtype: complex64
   >>> data = data.cast("sparse[decimal, 1]")
   >>> data
   0      1
   1      0
   2    NaN
   dtype: Sparse[object, Decimal('1')]
   >>> data = data.cast("datetime", unit="Y", since="utc")
   >>> data
   0   1971-01-01
   1   1970-01-01
   2          NaT
   dtype: datetime64[ns]
   >>> data = data.cast("timedelta[python]", since="utc")
   >>> data
   0    365 days, 0:00:00
   1              0:00:00
   2                  NaT
   dtype: object
   >>> class CustomObj:
   ...     def __init__(self, x):  self.x = x
   ...     def __str__(self):  return f"CustomObj({self.x})"
   >>> data = data.cast(CustomObj)
   >>> data
   0    CustomObj(365 days, 0:00:00)
   1              CustomObj(0:00:00)
   2                            <NA>
   dtype: object
   >>> data = data.cast("categorical[str[pyarrow]]")
   >>> data
   0    CustomObj(365 days, 0:00:00)
   1              CustomObj(0:00:00)
   2                            <NA>
   dtype: category
   Categories (2, string): [CustomObj(0:00:00), CustomObj(365 days, 0:00:00)]
   >>> data = data.cast(bool, true="*", false="CustomObj(0:00:00)")
   >>> data  # our original data
   0     True
   1    False
   2     <NA>
   dtype: boolean

And finally, dispatch methods allows users to add or modify series behavior on
a per-type basis.

.. doctest:: dispatch

   >>> import pandas as pd

   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round()
   Traceback (most recent call last):
      ...
   TypeError: loop of ufunc does not support argument 0 of type float which has no callable rint method

   # `pdcast` defines a round() function that is type-agnostic
   >>> import pdcast.attach
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

Contact
-------
The package maintainer can be contacted via the
`GitHub issue tracker <https://github.com/eerkela/pdcast/issues>`_, or directly
at eerkela42@gmail.com.

License
-------
``pdcast`` is available under an
`MIT license <https://github.com/eerkela/pdcast/blob/main/LICENSE>`_.
