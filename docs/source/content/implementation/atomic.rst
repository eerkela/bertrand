.. currentmodule:: pdcast

.. _implementation.atomic_type:

AtomicType
==========
:class:`AtomicTypes <AtomicType>` are the most fundamental unit of the
``pdcast`` type system.  They are used to describe scalar values of a
particular type (i.e. ``int``, ``numpy.float32``, etc.), and are responsible
for defining all the necessary implementation logic for dispatched methods,
conversions, and type-related functionality at this level.  If you're looking
to extend ``pdcast``, it will most likely come down to writing a new
:class:`AtomicType`.  Luckily, this is :ref:`easy to do <tutorial>`.

There are, however, a few caveats to this process, as described below.

Inheritance
-----------
The base :class:`AtomicType` definition is a `metaclass <https://peps.python.org/pep-0487/>`_
that tracks its subtypes.  Whenever a class inherits from it,
:class:`AtomicType` introspects various attributes of that class, bringing it
into a safe state, ready for integration with the rest of the package.  This
allows users to define new :class:`AtomicTypes <AtomicType>` on the fly simply
by inheriting from it.

One important caveat in this process is that :class:`AtomicTypes <AtomicType>`
are limited to **first-order inheritance**.  This means that objects must
inherit from :class:`AtomicType` *directly*, and no other objects can inherit
from those objects in turn.  For example:

.. code:: python

    class Type1(pdcast.AtomicType):
        ...

    class Type2(Type1):
        ...

would be invalid, since ``Type2`` inherits from ``Type1``, which inherits
from :class:`AtomicType`.  In contrast,

.. code:: python

    class Type1(pdcast.AtomicType):
        ...

    class Type2(pdcast.AtomicType):
        ...

is perfectly acceptable, since both types inherit from :class:`AtomicType`, and
do not inherit from each other.  The reasoning for this is to prevent
unintentional collisions between AtomicType attributes, which may differ
between parent and child classes.  Instead, users are encouraged to link their
dependent types using the :func:`@subtype() <subtype>` decorator like so:

.. code:: python

    @pdcast.subtype(Type1)
    class Type2(pdcast.AtomicType):
        ...

Which adds ``Type2`` to ``Type1.subtypes``.  If you'd like to share
functionality between ``Type1`` and ``Type2``, this can be done using
`Mixin classes <https://dev.to/bikramjeetsingh/write-composable-reusable-python-classes-using-mixins-6lj>`_,
like so:

.. code:: python

    class Mixin:
        # shared attributes/methods go here
        ...

    class Type1(Mixin, pdcast.AtomicType):
        ...

    @pdcast.subtype(Type1)
    class Type2(Mixin, pdcast.AtomicType):
        ...

.. note::

    Note that ``Mixin`` comes **before** ``AtomicType`` in each inheritance
    signature.  This ensures correct `Method Resolution Order (MRO) <https://en.wikipedia.org/wiki/C3_linearization>`_.

.. _flyweight:

Memory Allocation
-----------------
Another caveat is that :class:`AtomicType` instances are
`flyweights <https://python-patterns.guide/gang-of-four/flyweight/>`_.  This
means that they are only constructed once, and are **automatically cached**
for the duration of the program.  If the same type is requested again, it will
simply return a **reference** to the original instance, ensuring that only one
version of each type will exist at any given time.  This allows
:class:`AtomicTypes <AtomicType>` to be extremely memory-efficient, especially
when stored in arrays.  The downside of this is that each type must be
completely immutable, since any changes introduced at runtime would corrupt the
flyweight cache and lead to collisions.  As a result, all AtomicTypes are
strictly **read-only** after they are constructed.

.. testsetup:: allocation

    import pdcast

.. doctest:: allocation

    >>> pdcast.resolve_type("int") is pdcast.resolve_type("int")
    True
    >>> pdcast.resolve_type("int").new_attribute = 2
    Traceback (most recent call last):
        ...
    AttributeError: AtomicType objects are read-only

Some types might be parameterized with continuous or unpredictable inputs,
which could cause `memory leaks <https://en.wikipedia.org/wiki/Memory_leak>`_
in their flyweight cache if they are requested in sufficient numbers.  For
these cases, users can specify a `Least Recently Used (LRU) <https://en.wikipedia.org/wiki/Cache_replacement_policies#Least_recently_used_(LRU)>`_
caching strategy by passing an appropriate ``cache_size`` parameter in the
type's inheritance signature, like so:

.. code:: python

    class CustomType(pdcast.AtomicType, cache_size=128):
        ...

This causes only the 128 most-recently used types to be cached as flyweights.
If a type falls out of this range, it will be completely reconstructed the next
time it is requested.

.. note::

    Setting ``cache_size`` to 0 effectively eliminates flyweight caching for
    the type in question, though this is not recommended.

Required Attributes
-------------------
Lastly, every :class:`AtomicType` must define certain **required fields**
before they can be integrated with the rest of the package.  These are as
follows:

.. list-table::
    :header-rows: 1
    :align: center

    * - Attribute
      - Type
      - Default
    * - ``.name``
      - ``str``
      - N/A
    * - ``.aliases``
      - ``set``
      - N/A
    * - ``.type_def``
      - ``type``
      - ``None``
    * - ``.dtype``
      - ``np.dtype``, ``ExtensionDtype``
      - ``NotImplemented``
    * - ``.itemsize``
      - ``int``
      - ``None``
    * - ``.na_value``
      - ``Any``
      - ``pd.NA``
    * - ``.slugify``
      - ``@classmethod``
      - :meth:`AtomicType.slugify`

name
^^^^
This must be a unique string for each type.  It is used to generate string
representations of the associated type, which are then used to search for
existing flyweights.  The process for generating these strings can be
customized in :meth:`slugify <AtomicType.slugify>`.

Names can also be inherited from generic types via
:meth:`@AtomicType.register_backend() <AtomicType.register_backend>` if they
are not specified manually.

.. _atomic_type_aliases:

aliases
^^^^^^^
An :class:`AtomicType`\'s ``aliases`` field determines how types are detected
and resolved using :func:`detect_type` and :func:`resolve_type` respectively.
Special significance is given to the type of each alias, as follows:

    #.  String aliases are used by the :ref:`type specification mini-language
        <mini_language>` as anchors, triggering
        :meth:`resolution <AtomicType.resolve>` of the associated type and any
        arguments that are supplied to it.
    #.  Numpy ``dtype`` and pandas ``ExtensionDtype`` aliases are used by
        :func:`detect_type` for *O(1)* type inference.  ``ExtensionDtypes``
        are a special case and may be parametrized.  Adding ``type(dtype)`` to
        ``aliases`` negates this by triggering the type's
        :meth:`from_dtype() <AtomicType.from_dtype>` method.
    #.  Raw Python types are used by :func:`detect_type` for scalar or
        unlabeled vector inference.  If the ``type()`` of an element appears in
        ``aliases``, then this type's :meth:`detect() <AtomicType.detect>`
        constructor will be called on it.

Each alias must be unique across the space of all aliases, and the set always
includes the type itself.

type_def
^^^^^^^^
The scalar class for objects of this type.

dtype
^^^^^
The numpy ``dtype`` or pandas ``ExtensionDtype`` that corresponds to arrays of
this type.  This can also be ``NotImplemented``, which signals ``pdcast`` to
automatically generate a new ``ExtensionDtype`` via the `pandas extension api
<https://pandas.pydata.org/pandas-docs/stable/development/extending.html>`_.

.. note::

    Auto-generated ``ExtensionDtypes`` store data internally as a
    ``dtype: object`` array, which may not be the most efficient way of
    storing every type.  If there is a more compact representation for a
    particular data type, users can write their own ``ExtensionDtypes``
    instead.

itemsize
^^^^^^^^
The size (in bytes) for objects of this type.  ``None`` is interpreted as
being resizable/unlimited.

na_value
^^^^^^^^
The representation to use for missing values of this type.  This must pass a
``pd.isna()`` check.

slugify
^^^^^^^
This is a class method that describes how to generate string labels for the
associated type.  It must have the same arguments as the type's ``__init__()``
method.  Its output determines how flyweights are identified, so care must be
taken to ensure that each result is unique.  If a type is not parameterized and
does not implement a custom ``__init__()`` method, then this can be safely
omitted.

Hierarchies
-----------
:class:`AtomicTypes <AtomicType>` can be linked together to form trees using
the :func:`@subtype() <subtype>` decorator.  This adds the decorated type to
its parent's :attr:`subtypes <AtomicType.subtypes>` collection, including it
in :func:`typecheck` operations.

:class:`AtomicTypes <AtomicType>` can also be marked as
:func:`@generic <generic>`, meaning they can serve as containers for more
specific implementations.  In order to add an implementation to a generic type,
use its :meth:`register_backend() <AtomicType.register_backend>` decorator,
like so:

.. code:: python

    @pdcast.generic
    class GenericType(pdcast.AtomicType):
        ...

    @GenericType.register_backend("backend_name")
    class ImplementationType(pdcast.AtomicType):
        ...

This makes ``ImplementationType`` available from ``GenericType`` via the
backend ``"backend_name"``.

.. _atomic_type_registration:

Registration
------------
Once an appropriate :class:`AtomicType` definition has been created, it can be
integrated with the rest of the package by finishing its decorator stack with
:func:`@register <register>`, which verifies its configuration and appends it
to the :class:`registry <TypeRegistry>`.  Once registered, the type should be
recognized by :func:`detect_type` and :func:`resolve_type` using all of its
given aliases.  Any dispatched methods that are attached to it can then be
dispatched to pandas by calling :func:`attach`.

All in all, a typical :class:`AtomicType` definition could look something like
this:

.. code:: python

    @register
    @subtype(ParentType)
    @CustomType.register_backend("backend name")  # inherits .name
    class BackendType(AtomicType, cache_size=128):

        aliases = {"foo", "bar", "baz", np.dtype(np.int64), int, ...}
        type = int
        dtype = np.dtype(np.int64)
        itemsize = 8
        na_value = pd.NA

        def __init__(self, x, y):
            # custom arg parsing goes here, along with any new attributes
            super().__init__(x=x, y=y)  # no new attributes after this point

        @classmethod
        def slugify(cls, x, y) -> str:
            return f"cls.name[{str(x)}, {str(y)}]"

        # additional customizations/dispatch methods as needed
