.. NOTE: whenever a change is made to this file, make sure to update the
.. start and end lines of index.rst to allow doctests to run.

pdcast - flexible type extensions for pandas
============================================
``pdcast`` enhances the numpy/pandas typing infrastructure, allowing users to
write powerful, modular extensions for arbitrary data.

.. contents::
   :local:

What pdcast does
----------------
``pdcast`` provides a robust toolset for handling custom data types, including:

*  **Automatic creation of ExtensionDtypes**: ``pdcast`` simplifies and
   streamlines the creation of new data types for the pandas ecosystem.
*  **Universal conversions**: ``pdcast`` implements a single, overloadable
   conversion function that can losslessly convert data within its expanded
   type system.
*  **Type inference and schema validation**: ``pdcast`` can efficiently infer
   the types of arbitrary data and compare them against an external schema,
   increasing confidence and reliability in complex data pipelines.
*  **First-class support for missing values and mixed-type data**: ``pdcast``
   implements a separate data type for missing values, and can naturally
   process composite vectors via a split-apply-combine strategy.
*  **Data compression**: ``pdcast`` can losslessly compress data into a more
   efficient representation, reducing memory usage and increasing performance.
*  **Compatibility with third-party libraries**: ``pdcast`` bridges the gap
   between dynamically-typed Python and statically-typed extension libraries,
   allowing users to optimize their code without sacrificing flexibility.

Features
--------
``pdcast`` implements a rich `type system
<https://en.wikipedia.org/wiki/Type_system>`_ for numpy/pandas ``dtype``
objects, adding support for:

*  **Abstract hierarchies** representing different subtypes and
   implementations.  These are lightweight, efficient, and highly extensible,
   with new types added in as little as :ref:`10 lines of code <tutorial>`.

   .. doctest::

      >>> @register
      ... class CustomType(ScalarType):
      ...     name = "custom"
      ...     aliases = {"foo", "bar"}
      ... 
      ...     def __init__(self, x=None):
      ...         super().__init__(x=x)

*  A configurable, **domain-specific mini-language** for resolving types.  This
   represents a superset of the existing numpy/pandas syntax, with customizable
   aliases and semantics.

   .. doctest::

      >>> resolve_type("foo")
      CustomType(x=None)
      >>> resolve_type("foo").aliases.add("baz")
      >>> resolve_type("baz[x]")
      CustomType(x='x')

*  Vectorized **type detection** for example data in any format.  This is
   highly optimized and works regardless of an example's ``.dtype`` attribute,
   allowing ``pdcast`` to infer the types of ambiguous sequences such as lists,
   tuples, generators, and ``dtype: object`` arrays, no matter their contents.

   .. doctest::

      >>> detect_type([1, 2, 3])
      PythonIntegerType()
      >>> detect_type([1, 2.3, 4+5j])   # doctest: +SKIP
      CompositeType({int[python], float64[python], complex128[python]})

*  Efficient **type checks** for vectorized data.  These combine the above
   tools to perform ``isinstance()``-like hierarchical checks for any node in
   the ``pdcast`` type system.  If the data are properly labeled, then this is
   done in constant time, allowing users to add checks wherever they are
   needed.

   .. doctest::

         >>> df = pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["a", "b"]})
         >>> typecheck(df, {"a": "int", "b": "float", "c": "string"})
         True
         >>> typecheck(df["a"], "int")
         True

*  Support for **composite** and **decorator** types.  These can be used to
   represent mixed data and/or add new functionality to an existing type
   without modifying its original implementation (for instance by marking it as
   ``sparse`` or ``categorical``).

   .. doctest::

      >>> resolve_type("int, float, complex")  # doctest: +SKIP
      CompositeType({int, float, complex})
      >>> resolve_type("sparse[int, 23]")
      SparseType(wrapped=IntegerType(), fill_value=23)

*  **Multiple dispatch** based on the inferred type of one or more of a
   function's arguments.  With the ``pdcast`` type system, this can be extended
   to cover vectorized data in any representation, including those containing
   mixed elements.

   .. doctest::

      >>> @dispatch("x", "y")
      ... def add(x, y):
      ...     return x + y

      >>> @add.overload("int", "int")
      ... def add_integer(x, y):
      ...     return x - y

      >>> add([1, 2, 3], 1)
      0    0
      1    1
      2    2
      dtype: int[python]
      >>> add([1, 2, 3], [1, True, 1.0])
      0      0
      1      3
      2    4.0
      dtype: object

*  **Metaprogrammable extension functions** with dynamic arguments.  These can
   be used to actively manage the values that are supplied to a function by
   defining validators for one or more arguments, which pass their results into
   the body of the function in-place.  They can also be used to
   programmatically add new arguments at runtime, making them available to any
   virtual implementations that might request them.

   .. doctest::

      >>> @extension_func
      ... def add(x, y, **kwargs):
      ...     return x + y

      >>> @add.argument
      ... def y(val, context: dict) -> int:
      ...     return int(value)

      >>> add(1, "2")
      3
      >>> add.y = 2
      >>> add(1)
      3
      >>> del add.y
      >>> add(1)
      Traceback (most recent call last):
         ...
      TypeError: add() missing 1 required positional argument: 'y'

*  **Attachable functions** with a variety of access patterns.  These can be
   used to export a function to an existing class as a virtual attribute,
   dynamically modifying its interface at runtime.  These attributes can be
   used to mask existing behavior while maintaining access to the original
   implementation or be hidden behind virtual namespaces to avoid conflicts
   altogether, similar to ``Series.str``, ``Series.dt``, etc.

   .. doctest::

      >>> pdcast.attach()
      >>> series = pd.Series([1, 2, 3])
      >>> series.element_type == detect_type(series)
      True
      >>> series.typecheck("int") == typecheck(series, "int")
      True

Together, these features enable a functional approach to extending pandas with
small, fully encapsulated functions that perform special logic based on the
types of their arguments.  Users are thus able to surgically overload virtually
any aspect of the pandas interface or add entirely new behavior specific to
one or more of their own data types - all while maintaining the pandas tools
they know and love.

..
   Installation
   ------------
   Wheels are built using `cibuildwheel
   <https://cibuildwheel.readthedocs.io/en/stable/>`_ and are available for most
   platforms via the Python Package Index (PyPI).

   .. TODO: add hyperlink to PyPI page when it goes live

   .. code:: console

      (.venv) $ pip install pdcast

   If a wheel is not available for your system, ``pdcast`` also provides a
   source distribution to allow pip to build locally, although doing so
   requires a valid `Cython <https://cython.org/>`_ installation, including a C
   compiler such as `gcc <https://gcc.gnu.org/>`_ for Mac/Linux or `MinGW
   <https://sourceforge.net/projects/mingw/>`_ for Windows.

   .. code:: console

      (.venv) $ git clone https://github.com/eerkela/pdcast
      (.venv) $ pip install pdcast/

   This should take around 5 minutes to build.  An editable install can be
   created by running:

   .. code:: console

      (.venv) $ git clone https://github.com/eerkela/pdcast
      (.venv) $ cd pdcast/
      (.venv) $ pip install -e .[dev]
      (.venv) $ make help

   Manual installs may also require Python development headers if they are
   not already present.  These can be installed via your system's package
   manager.

      *  On Ubuntu (or other Debian-based systems), run
         ``sudo apt-get install python3-dev``.
      *  On CentOS, run: ``sudo yum install python3-devel``.
      *  On Fedora, run: ``sudo dnf install python3-devel``.

Usage
-----
``pdcast`` combines its advanced features to implement its own super-charged
:func:`cast() <pdcast.cast>` function, which can perform universal data
conversions within its expanded type system.  Here's a round-trip journey
through each of the core families of the ``pdcast`` type system:

.. doctest::

   >>> import numpy as np

   >>> class CustomObj:
   ...     def __init__(self, x):  self.x = x
   ...     def __str__(self):  return f"CustomObj({self.x})"
   ...     def __repr__(self):  return str(self)

   >>> pdcast.to_boolean([1+0j, "False", None])  # non-homogenous to start
   0     True
   1    False
   2     <NA>
   dtype: boolean
   >>> _.cast(np.dtype(np.int8))  # to integer
   0       1
   1       0
   2    <NA>
   dtype: Int8
   >>> _.cast("double")  # to float
   0    1.0
   1    0.0
   2    NaN
   dtype: float64
   >>> _.cast(np.complex128, downcast=True)  # to complex (minimizing memory usage)
   0    1.0+0.0j
   1    0.0+0.0j
   2   N000a000N
   dtype: complex64
   >>> _.cast("sparse[decimal, 1]")  # to decimal (sparse)
   0      1
   1      0
   2    NaN
   dtype: Sparse[object, Decimal('1')]
   >>> _.cast("datetime", unit="Y", since="j2000")  # to datetime (years since j2000 epoch)
   0   2001-01-01 12:00:00
   1   2000-01-01 12:00:00
   2                   NaT
   dtype: datetime64[ns]
   >>> _.cast("timedelta[python]", since="Jan 1st, 2000 at 12:00 PM")  # to timedelta (µs since j2000)
   0    366 days, 0:00:00
   1              0:00:00
   2                  NaT
   dtype: timedelta[python]
   >>> _.cast(CustomObj)  # to custom Python object
   0    CustomObj(366 days, 0:00:00)
   1              CustomObj(0:00:00)
   2                            <NA>
   dtype: object[<class 'CustomObj'>]
   >>> _.cast("categorical[str[pyarrow]]")  # to string (categorical with PyArrow backend)
   0    CustomObj(366 days, 0:00:00)
   1              CustomObj(0:00:00)
   2                            <NA>
   dtype: category
   Categories (2, string): [CustomObj(0:00:00), CustomObj(366 days, 0:00:00)]
   >>> _.cast("bool", true="*", false="CustomObj(0:00:00)")  # back to our original data
   0     True
   1    False
   2     <NA>
   dtype: boolean

New implementations for :func:`cast() <pdcast.cast>` can be added dynamically,
with customization for both the source and destination types.

.. doctest::

   >>> @cast.overload("bool[python]", "int[python]")
   ... def my_custom_conversion(series, dtype, **unused):
   ...     print("calling my custom conversion...")
   ...     return series.apply(int, convert_dtype=False)

   >>> pd.Series([True, False], dtype=object).cast(int)
   calling my custom conversion...
   0    1
   1    0
   dtype: object

Finally, ``pdcast``'s powerful suite of function decorators allow users to
write their own specialized extensions for existing pandas behavior:

.. doctest::

   >>> @attachable
   ... @dispatch("self", "other")
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
   >>> pd.Series([1, 2, 3]) + [1, True, 1.0]
   0      0
   1      3
   2    4.0
   dtype: object

Or create entirely new attributes and methods above and beyond what pandas
includes by default.

.. doctest::

   >>> @attachable
   ... @dispatch("series")
   ... def bar(series):
   ...     raise NotImplementedError("bar is only defined for floating point values")

   >>> @bar.overload("float")
   ... def float_bar(series):
   ...     print("Hello, World!")
   ...     return series

   >>> bar.attach_to(pd.Series, namespace="foo", pattern="property")
   >>> pd.Series([1.0, 2.0, 3.0]).foo.bar
   Hello, World!
   0    1.0
   1    2.0
   2    3.0
   dtype: float64
   >>> pd.Series([1, 2, 3]).foo.bar
   Traceback (most recent call last):
      ...
   NotImplementedError: bar is only defined for floating point values

.. 
   Documentation
   -------------
   Detailed documentation is hosted on readthedocs.

License
-------
``pdcast`` is available under an `MIT license
<https://github.com/eerkela/pdcast/blob/main/LICENSE>`_.

Contributing
------------
``pdcast`` is open-source and welcomes contributions.  For more information,
please contact the package maintainer or submit a pull request on
`GitHub <https://github.com/eerkela/pdcast>`_.

Contact
-------
The package maintainer can be contacted via the
`GitHub issue tracker <https://github.com/eerkela/pdcast/issues>`_, or directly
at eerkela42@gmail.com.

Related Projects
----------------
*  `pdlearn <https://github.com/eerkela/pdlearn>`_ - AutoML integration for
   pandas DataFrames using the ``pdcast`` type system.
