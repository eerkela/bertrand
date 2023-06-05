.. currentmodule:: pdcast

.. _ScalarType:

pdcast.ScalarType
=================

.. autoclass:: ScalarType

.. _ScalarType.inheritance:

Inheritance
-----------
:class:`ScalarTypes <ScalarType>` are `metaclasses
<https://peps.python.org/pep-0487/>`_ that are limited to **first-order
inheritance**.  This means that they must inherit from :class:`ScalarType`
*directly*, and cannot have any children of their own.  For example:

.. code:: python

    class Type1(pdcast.ScalarType):   # valid
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

    class Type1(Mixin, pdcast.ScalarType):
        ...

    class Type2(Mixin, pdcast.ScalarType):
        ...

.. note::

    Note that ``Mixin`` comes **before** :class:`ScalarType` in each
    inheritance signature.  This ensures correct `Method Resolution Order (MRO)
    <https://en.wikipedia.org/wiki/C3_linearization>`_.

.. _ScalarType.allocation:

Memory Allocation
-----------------
Additionally, :class:`ScalarType` instances are `flyweights
<https://python-patterns.guide/gang-of-four/flyweight/>`_ that are identified
by their :meth:`slug <ScalarType.slugify>` attribute.  This allows them to be
extremely memory-efficient (especially when stored in arrays) but also requires
each one to be completely immutable.  As a result, all
:class:`ScalarTypes <ScalarType>` are strictly **read-only** after they are
constructed.

.. testsetup:: allocation

    import pdcast

.. doctest:: allocation

    >>> pdcast.resolve_type("int") is pdcast.resolve_type("int")
    True
    >>> pdcast.resolve_type("int").new_attribute = 2
    Traceback (most recent call last):
        ...
    AttributeError: ScalarType objects are read-only

Some types might be parameterized with continuous or unpredictable inputs,
which could cause `memory leaks <https://en.wikipedia.org/wiki/Memory_leak>`_.
In these cases, users can specify a `Least Recently Used (LRU)
<https://en.wikipedia.org/wiki/Cache_replacement_policies#Least_recently_used_(LRU)>`_
caching strategy by passing an appropriate ``cache_size`` parameter in the
type's inheritance signature, like so:

.. code:: python

    class CustomType(pdcast.ScalarType, cache_size=128):
        ...

.. note::

    Setting ``cache_size`` to 0 effectively eliminates flyweight caching for
    the type in question, though this is not recommended.

.. _ScalarType.required:

Required Attributes
-------------------
:class:`ScalarTypes <ScalarType>` must implement the following attributes to
be considered valid.

.. autosummary::
    :toctree: ../../generated

    ScalarType.name
    ScalarType.aliases
    ScalarType.type_def
    ScalarType.dtype
    ScalarType.itemsize
    ScalarType.na_value

.. _ScalarType.constructors:

Constructors
------------
These should always be preferred over direct instantiation to allow for the
`flyweight <https://python-patterns.guide/gang-of-four/flyweight/>`_ pattern.

.. autosummary::
    :toctree: ../../generated/

    ScalarType.instance
    ScalarType.resolve
    ScalarType.detect
    ScalarType.from_dtype
    ScalarType.replace
    ScalarType.slugify
    ScalarType.kwargs

.. _ScalarType.aliases:

Aliases
-------
See :attr:`ScalarType.aliases` for more information on how aliases are used.

.. autosummary::
    :toctree: ../../generated/

    ScalarType.register_alias <VectorType.register_alias>
    ScalarType.remove_alias <VectorType.remove_alias>
    ScalarType.clear_aliases <VectorType.clear_aliases>

.. _ScalarType.subtypes:

Subtypes/Supertypes
-------------------
See the :func:`@subtype <subtype>` decorator for more information on how to
define subtypes.

.. autosummary::
    :toctree: ../../generated/

    ScalarType.is_root
    ScalarType.root
    ScalarType.supertype
    ScalarType.subtypes
    ScalarType.contains
    ScalarType.is_subtype

.. _ScalarType.generic:

Generic Backends
----------------
See the :func:`@generic <generic>` decorator for more information on how to
leverage generic types and register individual backends.

.. autosummary::
    :toctree: ../../generated/

    ScalarType.is_generic
    ScalarType.backend
    ScalarType.generic
    ScalarType.backends
    ScalarType.register_backend

.. _ScalarType.decorators:

Adapters
--------
See :class:`DecoratorType` for more information on how to wrap
:class:`ScalarTypes <ScalarType>` with adapters.

.. autosummary::
    :toctree: ../../generated/

    ScalarType.decorators
    ScalarType.unwrap
    ScalarType.make_sparse
    ScalarType.make_categorical

.. _ScalarType.downcast:

Upcast/Downcast
---------------

.. autosummary::
    :toctree: ../../generated/

    ScalarType.larger
    ScalarType.smaller

.. _ScalarType.missing:

Missing Values
--------------

.. autosummary::
    :toctree: ../../generated/

    ScalarType.is_nullable
    ScalarType.make_nullable

.. _ScalarType.special:

Special Methods
---------------

.. autosummary::
    :toctree: ../../generated/

    ScalarType.__contains__
    ScalarType.__eq__
    ScalarType.__hash__
    ScalarType.__str__
    ScalarType.__repr__

Class Diagram
-------------

.. image:: /images/types/Types_UML.svg
