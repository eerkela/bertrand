.. currentmodule:: pdcast

AtomicType
==========

.. autosummary::
    :toctree: ../../generated

    AtomicType

.. _atomic_type.inheritance:

Inheritance
-----------
:class:`AtomicTypes <AtomicType>` are `metaclasses
<https://peps.python.org/pep-0487/>`_ that are limited to **first-order
inheritance**.  This means that they must inherit from :class:`AtomicType`
*directly*, and cannot have any children of their own.  For example:

.. code:: python

    class Type1(pdcast.AtomicType):   # valid
        ...

    class Type2(Type1):   # invalid
        ...

If you'd like to share functionality between types, this can be done using
`Mixin classes <https://dev.to/bikramjeetsingh/write-composable-reusable-python-classes-using-mixins-6lj>`_,
like so:

.. code:: python

    class Mixin:
        # shared attributes/methods go here
        ...

    class Type1(Mixin, pdcast.AtomicType):
        ...

    class Type2(Mixin, pdcast.AtomicType):
        ...

.. note::

    Note that ``Mixin`` comes **before** :class:`AtomicType` in each
    inheritance signature.  This ensures correct `Method Resolution Order (MRO)
    <https://en.wikipedia.org/wiki/C3_linearization>`_.

.. _atomic_type.allocation:

Memory Allocation
-----------------
Additionally, :class:`AtomicType` instances are `flyweights
<https://python-patterns.guide/gang-of-four/flyweight/>`_ that are identified
by their :meth:`slug <AtomicType.slugify>` attribute.  This allows them to be
extremely memory-efficient (especially when stored in arrays) but also requires
each one to be completely immutable.  As a result, all
:class:`AtomicTypes <AtomicType>` are strictly **read-only** after they are
constructed.

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
which could cause `memory leaks <https://en.wikipedia.org/wiki/Memory_leak>`_.
In these cases, users can specify a `Least Recently Used (LRU)
<https://en.wikipedia.org/wiki/Cache_replacement_policies#Least_recently_used_(LRU)>`_
caching strategy by passing an appropriate ``cache_size`` parameter in the
type's inheritance signature, like so:

.. code:: python

    class CustomType(pdcast.AtomicType, cache_size=128):
        ...

.. note::

    Setting ``cache_size`` to 0 effectively eliminates flyweight caching for
    the type in question, though this is not recommended.

Required Attributes
-------------------
:class:`AtomicTypes <AtomicType>` must implement the following attributes to
be considered valid.

.. autosummary::
    :toctree: ../../generated

    AtomicType.name
    AtomicType.aliases
    AtomicType.type_def
    AtomicType.dtype
    AtomicType.itemsize
    AtomicType.na_value

Constructors
------------
These should always be preferred over direct instantiation to allow for the
`flyweight <https://python-patterns.guide/gang-of-four/flyweight/>`_ pattern.

.. autosummary::
    :toctree: ../../generated/

    AtomicType.instance
    AtomicType.resolve
    AtomicType.detect
    AtomicType.from_dtype
    AtomicType.replace
    AtomicType.slugify
    AtomicType.kwargs

Aliases
-------
See :attr:`AtomicType.aliases` for more information on how aliases are used.

.. autosummary::
    :toctree: ../../generated/

    AtomicType.register_alias <ScalarType.register_alias>
    AtomicType.remove_alias <ScalarType.remove_alias>
    AtomicType.clear_aliases <ScalarType.clear_aliases>

Subtypes/Supertypes
-------------------
See the :func:`@subtype <subtype>` decorator for more information on how to
define subtypes.

.. autosummary::
    :toctree: ../../generated/

    AtomicType.is_root
    AtomicType.root
    AtomicType.supertype
    AtomicType.subtypes
    AtomicType.contains
    AtomicType.is_subtype

Generic Backends
----------------
See the :func:`@generic <generic>` decorator for more information on how to
leverage generic types and register individual backends.

.. autosummary::
    :toctree: ../../generated/

    AtomicType.is_generic
    AtomicType.backend
    AtomicType.generic
    AtomicType.backends
    AtomicType.register_backend

Adapters
--------
See :class:`AdapterType` for more information on how to wrap
:class:`AtomicTypes <AtomicType>` with adapters.

.. autosummary::
    :toctree: ../../generated/

    AtomicType.adapters
    AtomicType.unwrap
    AtomicType.make_sparse
    AtomicType.make_categorical

Conversions
-----------
See the :doc:`conversion docs <cast>` for more information on type conversions.

.. autosummary::
    :toctree: ../../generated/

    AtomicType.to_boolean
    AtomicType.to_integer
    AtomicType.to_float
    AtomicType.to_complex
    AtomicType.to_decimal
    AtomicType.to_datetime
    AtomicType.to_timedelta
    AtomicType.to_string
    AtomicType.to_object

Upcast/Downcast
---------------

.. autosummary::
    :toctree: ../../generated/

    AtomicType.larger
    AtomicType.smaller
    AtomicType.upcast
    AtomicType.downcast

Missing Values
--------------

.. autosummary::
    :toctree: ../../generated/

    AtomicType.is_nullable
    AtomicType.is_na
    AtomicType.make_nullable

Special Methods
---------------

.. autosummary::
    :toctree: ../../generated/

    AtomicType.__contains__
    AtomicType.__eq__
    AtomicType.__hash__
    AtomicType.__str__
    AtomicType.__repr__
