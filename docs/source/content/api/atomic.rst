.. currentmodule:: pdcast

.. _atomic_type:

AtomicType
==========

.. autoclass:: AtomicType

Constructors
------------
These methods should always be preferred over direct instantiation to allow for
the `flyweight <https://python-patterns.guide/gang-of-four/flyweight/>`_
pattern.

.. autosummary::
    :toctree: ../../generated/

    AtomicType.instance
    AtomicType.resolve
    AtomicType.detect
    AtomicType.from_dtype
    AtomicType.replace
    AtomicType.slugify

Aliases
-------
See the :ref:`implementation docs <atomic_type_aliases>` for more information
on how aliases are used.

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

    AtomicType.subtypes
    AtomicType.supertype
    AtomicType.root
    AtomicType.contains
    AtomicType.is_subtype

Generic
-------
See the :func:`@generic <generic>` decorator for more information on how to
leverage generic types and register individual backends.

.. autosummary::
    :toctree: ../../generated/

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
