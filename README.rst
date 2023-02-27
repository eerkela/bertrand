pdcast - flexible type extensions for numpy/pandas
==================================================

.. ``pdcast`` extends and enhances the existing numpy/pandas typing
..  infrastructure, making it easier to clean and manipulate tabular data.

``pdcast`` modifies the existing numpy/pandas typing infrastructure, making it
easier to work with tabular data in a wide variety of representations.


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
   missing values, overflow, timezones, epochs, imprecise representations,
   string parsing, and error-handling.
*  **Efficient**.  By giving users more control over the types that are present
   within their series and dataframe objects, substantial memory savings and
   performance improvements can be achieved.  Sparse data structures and
   lossless downcasting make it possible to shrink data by up to a factor of
   10 in some cases while simultaneously accelerating downstream analysis and
   increasing access to big data.


Installation
------------
Wheels are available for most platforms via the Python Package Index (PyPI).

.. code-block:: console

   (.venv) $ pip install pdcast

``pdcast`` can also be built from source, although doing so requires an
additional ``cython`` dependency.

.. NOTE: this is done through pip via the same endpoint.

.. NOTE: if you want to run the test suite, install the package using the
.. optional ``pdcast[dev]`` dependencies.


Demonstration
-------------
``pdcast`` can be used to easily verify the types that are present within
tabular data:

.. doctest::

   >>> import pandas as pd
   >>> import pdcast.attach
   >>> df = pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["a", "b"]})
   >>> df.check_types({"a": "int", "b": "float", "c": "string")
   True

It can also be used to convert data from one representation to another.  Here
is a short walk around the various type categories that are recognized by
``pdcast``.

.. doctest::

   >>> import numpy as np
   >>> import pdcast
   >>> data = [1+0j, "False", None]
   >>> data = pdcast.to_boolean(data)
   >>> data
   >>> data = data.cast(np.int8)
   >>> data
   >>> data = data.cast("float64", downcast=True)
   >>> data
   >>> data = data.cast(complex, sparse=True)
   >>> data
   >>> data = data.cast("decimal[python]")
   >>> data
   >>> data = data.cast("datetime[pandas]", unit="Y", tz="US/Pacific")
   >>> data
   >>> data = data.cast("timedelta[python]" epoch="utc")
   >>> data
   >>> class CustomObj:
   >>>     def __init__(self, x: datetime.timedelta):
   >>>         self.x = x
   >>>     def __str__(self) -> str:
   >>>         return f"CustomObj({self.x})"
   >>> data = data.cast(CustomObj)
   >>> data
   >>> data = data.cast("categorical[str[pyarrow]]")
   >>> data
   >>> data = data.cast(bool, true="*", false="CustomObj(0:00:00)")
   >>> data

And finally, dispatching allows users to add or modify series methods on a
per-type basis.

.. doctest::

   >>> import pandas as pd
   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round()
   >>> import pdcast.attach
   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round()
   >>> # original functionality can be easily recovered
   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round.original()


Documentation
-------------
Detailed documentation is hosted on readthedocs.

.. NOTE: add hyperlink once documentation goes live
