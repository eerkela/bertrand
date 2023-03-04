Implementation
==============

Below is a rough overview of the internal data structures ``pdcast`` uses to
process data, with condensed descriptions for their important functionality.
This is intended to provide increased context for those seeking to extend
``pdcast`` in some way, or those simply interested in learning how it works.
If a question is not answered by this document, check the API docs for more
information.

AtomicType
----------
AtomicTypes hold all the necessary implementation logic for dispatched methods,
conversions, and type-related functionality.  They can be linked together into
tree structures to represent subtypes, and can be marked as generic to hold
different implementations.  If you're looking to extend ``pdcast``, it will
most likely boil down to writing a new AtomicType.  Luckily, this is
:ref:`easy to do <tutorial>`.

The base ``AtomicType`` definition itself is a metaclass with substantial
default behavior.  Whenever a class inherits from it, AtomicType can introspect
various aspects of that class to bring it to a safe state, ready for
integration with the rest of the package.  This allows ``AtomicType`` to be
aware of its own subclasses, and allows users to define new types on the fly
simply by inheriting from ``pdcast.AtomicType``.

There are a few caveats to this process.  First is that ``AtomicType`` is
limited to first-order inheritance.  This means that if an object inherits
from ``AtomicType``, it must do so directly, and no other objects can inherit
from it in turn.  The reasoning for this is to prevent unintentional collisions
between AtomicType attributes, which may differ between parent and child
classes.  Instead, users are encouraged to link their dependent types via the
``@subtype()`` decorator like so:

.. code:: python

    @subtype(ParentType)
    class CustomType(AtomicType):
        ...

Secondly, AtomicType instances are
`flyweights <https://python-patterns.guide/gang-of-four/flyweight/>`_ that are
automatically cached upon construction.  If the same type is requested again,
it will simply return a pointer to the original instance, ensuring that only
one version of each type will exist at any given time.  This allows them to be
extremely memory-efficient, especially when stored in arrays, which is relevant
during inferencing.  The downside of this is that each type must be completely
immutable, since any changes introduced at runtime would corrupt the flyweight
cache and lead to collisions.  As a result, all AtomicTypes are strictly
read-only after they are constructed.

.. doctest::

    >>> import pdcast
    >>> pdcast.resolve_type("int") is pdcast.resolve_type("int")
    True
    >>> pdcast.resolve_type("int").new_attribute = 2
    Traceback (most recent call last):
        ...
    AttributeError: AtomicType objects are read-only

Some types might have continuous or unpredictable inputs, which could overwhelm
the shared flyweight cache if requested in sufficient numbers.  In these cases,
types can specify a Least Recently Used (LRU) caching strategy by passing an
appropriate ``cache_size`` parameter to ``AtomicType.__init_subclass__()``,
like so:

.. code:: python

    class CustomType(AtomicType, cache_size=128):
        ...

Setting this to 0 effectively eliminates caching for that type, though this is
not recommended.

Lastly, every AtomicType must define certain required fields before they can be
integrated into the rest of the package.  These are as follows:

*   ``name: str``.  This must be unique, and is used when generating string
    representations of the associated type.  It can also be inherited from
    generic types via ``generic.register_backend()`` if it is not specified
    manually.
*   ``aliases: set``.  This contains all the various strings, types, and
    numpy/pandas dtype objects that are recognized by ``resolve_type()`` and
    ``detect_type()``.  Each of these must be unique across the space of all
    aliases, and the set always includes the AtomicType itself.
*   ``type: type``, default ``None``.  The scalar class definition for objects
    of this type.
*   ``dtype: np.dtype | pd.api.extensions.ExtensionDtype``, default
    ``np.dtype("O")``.  The numpy/pandas ``dtype`` object that corresponds to
    this AtomicType.
*   ``itemsize: int``, default ``None``.  The size (in bytes) for objects of
    this type.  ``None`` is interpreted as being unlimited.
*   ``na_value: Any``, default ``pd.NA``.  The representation to use for
    missing values of this type.  This must pass a ``pd.isna()`` check.
*   ``slugify(cls, ...)``.  This is a classmethod that describes how to
    generate string labels for the associated type.  It must have the same
    argument signature as the type's ``__init__()`` method, and its output
    determines how flyweights are identified, so care must be taken to ensure
    that each output is unique.  If a type does not implement a custom
    ``__init__()`` method, this can be safely omitted.

AtomicTypes can also be marked as being generic, allowing them to serve as
containers for individual backends.  This can be done by appending an
``@generic`` decorator to its class definition, like so:

.. code:: python

    @generic
    class CustomType(AtomicType):
        ...

In order to qualify as a generic type, an AtomicType must not implement a
custom ``__init__()`` method.  Once marked, backends can be added to a generic
type by calling its ``@register_backend()`` decorator, as shown:

.. code:: python

    @CustomType.register_backend("<backend name>")
    class BackendType(AtomicType):
        ...

This allows ``BackendType`` to be resolved from ``CustomType`` by passing the
specified backend string during ``resolve_type()`` calls.  It also adds
``BackendType`` to ``CustomType.subtypes``, automatically including it in
membership checks.  Note that the backend string that is provided to
``@CustomType.register_backend()`` must be unique.

Once an appropriate AtomicType definition has been created, it can be
automatically integrated with the rest of the package by finishing its
decorator stack with ``@register``, which verifies its configuration and
appends it to the registry.  Once registered, the type should be recognized by
``resolve_type()`` at all of its given aliases and detectable from example
data of the appropriate type(s).  Any dispatched methods it defines will also
be dynamically attached to ``pd.Series`` objects of that type.

All in all, a typical AtomicType definition could look something like this:

.. code:: python

    @register
    @subtype(ParentType)
    @CustomType.register_backend("backend name")  # inherits name
    class BackendType(AtomicType, cache_size=128):

        aliases = {"foo", "bar", "baz", np.dtype(np.int64), int, ...}
        type = int
        dtype = np.dtype(np.int64)
        itemsize = 8
        na_value = pd.NA

        def __init__(self, x, y):
            # custom arg processing goes here
            super().__init__(x=x, y=y)

        @classmethod
        def slugify(cls, x, y) -> str:
            return f"cls.name[{str(x)}, {str(y)}]"

        # additional customizations/dispatch methods as needed

``AtomicType`` provides significant out of the box functionality for its
subclasses, giving users a robust framework to work from.  If you'd like to
write your own type definition, feel free to peruse the base type and
prepackaged AtomicTypes for examples.

AdapterType
-----------
AdapterTypes are types that modify other types.  They can dynamically wrap
``AtomicType`` objects to change their behavior on a programmatic basis and
are not cached as flyweights like ``AtomicType``\s are.

``AdapterType``\s are examples of the Gang of Four `Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_
(not to be confused with the built-in python ``@decorator`` syntax).  This
leverages `composition over inheritance <https://en.wikipedia.org/wiki/Composition_over_inheritance>`_,
to prevent subclass explosions for type objects.  They can be nested to form
a singly-linked list that sits on top of the base ``AtomicType`` object,
successively wrapping it to modify its behavior.

Common use cases for ``AdapterType``\s include sparse and categorical data.
These are handled by ``pdcast.SparseType`` and ``pdcast.CategoricalType``
respectively, which can be applied to any ``AtomicType`` object to convert it
into a sparse/categorical representation.  They are designed to be as
minimally-intrusive as possible, and can be constructed in the same way as
``AtomicTypes`` via the type-specification mini-language, as shown:

.. doctest::

    >>> import pdcast
    >>> pdcast.resolve_type("sparse[float32]")
    SparseType(wrapped=Float32Type(), fill_value=nan)
    >>> pdcast.resolve_type("categorical[string]")
    CategoricalType(wrapped=StringType(), levels=None)
    >>> pdcast.resolve_type("sparse[categorical[bool]]")
    SparseType(wrapped=CategoricalType(wrapped=BooleanType(), levels=None), fill_value=<NA>)

If you'd like to write your own ``AdapterType``, the best place to start would
be to examine these types to see how they work.

CompositeType
-------------
``CompositeType``\s are set-like containers for ``AtomicType`` and
``AdapterType`` objects.  They implement a standard set interface, with all the
same methods as the built in ``set`` type.  They can be constructed by
providing multiple type specifiers to ``pdcast.resolve_type()``, either in
comma-separated string form or by providing an iterable.

.. doctest::

    >>> import numpy as np
    >>> import pdcast
    >>> pdcast.resolve_type("bool, int, float")   # doctest: +SKIP
    CompositeType({bool, int, float})
    >>> pdcast.resolve_type([np.dtype("f2"), "sparse[datetime]", str])   # doctest: +SKIP
    CompositeType({float16[numpy], sparse[datetime, NaT], string})

``CompositeType`` objects implicitly include subtypes for each of their
elements, and automatically resolve comparison targets.  This allows for easy
membership tests that account for subtypes in the same way as
``AtomicType.contains()``.

.. doctest::

    >>> import pdcast
    >>> pdcast.resolve_type("int, float").contains("int8[pandas]")
    True
    >>> pdcast.resolve_type("int, float") - "float64"   # doctest: +SKIP
    CompositeType({int, float16, float32, float80})

They can also be constructed by providing non-homogenous example data to
``pdcast.detect_type()``, as shown:

.. doctest::

    >>> import pdcast
    >>> pdcast.detect_type([False, 1, 2.3, 4+5j])   # doctest: +SKIP
    CompositeType({bool, float, complex, int})

In this case, the resulting ``CompositeType`` also includes a special
``.index`` attribute, which keeps track of the type's position in the original
data.

.. doctest::

    >>> import pdcast
    >>> pdcast.detect_type([False, 1, 2.3, 4+5j]).index
    array([BooleanType(), IntegerType(), FloatType(), ComplexType()],
          dtype=object)

This can be used during ``pd.Series.groupby()`` operations to apply functions
by type, rather than all at once.

.. _SeriesWrapper_description:

SeriesWrapper
-------------
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

.. _type_specification:

Type specification mini-language
--------------------------------

