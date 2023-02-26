pdcast - flexible type extensions for numpy/pandas
==================================================

``pdcast`` extends the existing numpy/pandas typing infrastructure, adding
support for customizable type hierarchies, inference, schema validation,
automatic method dispatching, and a comprehensive suite of overflow and
precision loss-safe conversions.


Installation
============
Wheels are available for most platforms via the Python Package Index (PyPI).

.. code-block:: console

   (.venv) $ pip install pdcast

``pdcast`` can also be built from source, though doing so requires an
additional ``cython`` dependency.


Advantages
==========
Compared to the existing ``astype()`` infrastructure, ``pdcast`` is:

*  **Powerful**. ``pdcast`` can handle almost any input data, even if they are
   mislabeled, malformed, imprecise, or ambiguous.  They can contain missing
   values, have duplicate indices, and even be of mixed type and still be
   processed intelligibly by ``pdcast`` internals.  This robustness is thanks
   to a powerful dispatch mechanism that allows series methods to be fully
   type-aware, dispatching to a number of different implementations based on
   observed data.  These dispatched methods are exceedingly easy to write, and
   are fully general-purpose.  In principle, they could be extended to
   integrate arbitrary object types into the pandas ecosystem, dynamically
   patching methods on an as-needed basis to replicate existing functionality
   or add new logic specific to that type.
*  **Flexible**.  Every aspect of ``pdcast``'s functionality can be modified or
   extended to meet one's needs, no matter how complex.  New types can be
   declared in as little as 10 lines of code, can describe arbitrary objects,
   and are not restricted to the existing numpy/pandas ``dtype``
   infrastructure.  In fact, ``pdcast`` makes using these types completely
   optional, more a matter of performance than anything else.  The package
   internals work equally well for these as for generic python objects
   (``dtype="O"`` or unlabeled lists, tuples, etc.) with no modifications.  As
   such, it doesn't even rely on custom ``pd.ExtensionDtype`` definitions,
   though it will happily integrate them if available.
*  **Comprehensive**.  ``pdcast`` comes prepackaged with high-level support for
   9 categories of commonly-encountered data: boolean, integer, floating point,
   complex, decimal (arbitrary precision), datetime, timedelta, string, and
   quick python objects.  Each of these are organized into separate hierarchy
   trees, with support for multiple backends including numpy, pandas, python,
   and pyarrow where applicable.
*  **Safe**.  Conversions between types are fully reversible, with no precision
   loss in either direction.  Overflow is explicitly forbidden, and tolerances
   can be set to allow fuzzy-matching.  Rounding can be done in any
   direction on any numeric type, not limited to ``np.floor``, ``np.ceil``,
   or ``np.round``.  Arbitrary units, timezones, and epochs are allowed for
   datetime manipulations, with calendar-accurate unit conversions and enhanced
   range above and below the min/max of ``pd.Timestamp`` objects.
   Commonly-encountered problems, such as missing values and non-unique indices
   are abstracted away, and all the normal error-handling rules (``"raise"``,
   ``"coerce"``, ``"ignore"``) function identically to their pandas
   counterparts.  ``pdcast`` will not modify your data unless you explicitly
   tell it to.
*  **Efficient**.  ``pdcast`` is built on top of the existing numpy/pandas
   typing infrastructure, meaning that in most cases, it is as fast/efficient
   as they would be in an equivalent situation, with minor overhead.  Wherever
   custom code is necessary, it is written in cython, a superset of Python that
   can achieve C-like performance through static typing and transpilation.
   Additionally, the package includes functionality to make existing pandas
   structures more efficient by enhancing support for lossless downcasting and
   sparse data structures.  Together, these can reduce the size in memory of
   naive pandas series/dataframes by a factor of ~10 in some cases while
   simultaneously accelerating downstream analysis.


Demonstration
=============
``pdcast`` can be used to easily verify the types that are present within
pandas data structures:

.. doctest::

   >>> import pandas as pd
   >>> import pdcast.attach
   >>> df = pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["a", "b"]})
   >>> df.check_types({"a": "int", "b": "float", "c": "string")
   True

It also comes prepackaged with a robust suite of type conversions.  Here is a
short walk around the various type categories that are recognized by
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

It can also be used to repair broken pandas methods:

.. doctest::

   >>> import pandas as pd
   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round()
   >>> import pdcast.attach
   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round()
   >>> # original functionality can be easily recovered
   >>> pd.Series([1.1, -2.5, 3.7], dtype="O").round.original()


Documentation
=============
Detailed documentation is hosted on readthedocs.
