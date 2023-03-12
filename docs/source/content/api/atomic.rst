.. currentmodule:: pdcast

.. _atomic_type:

AtomicType
==========
AtomicTypes hold all the necessary implementation logic for dispatched methods,
conversions, and type-related functionality.  They can be linked together into
tree structures to represent subtypes, and can be marked as generic to hold
different implementations.  If you're looking to extend ``pdcast`` in some way,
it will most likely boil down to writing a new AtomicType.  Luckily, this is
:ref:`easy to do <tutorial>`.

Constructors
------------

.. autosummary::
    :toctree: ../../generated/

    AtomicType.instance
    AtomicType.detect
    AtomicType.resolve
    AtomicType.replace
    AtomicType.slugify

Aliases
-------

.. autosummary::
    :toctree: ../../generated/

    AtomicType.register_alias <ScalarType.register_alias>
    AtomicType.remove_alias <ScalarType.remove_alias>
    AtomicType.clear_aliases <ScalarType.clear_aliases>

Subtypes/Supertypes
-------------------

.. autosummary::
    :toctree: ../../generated/

    AtomicType.subtypes
    AtomicType.supertype
    AtomicType.generic
    AtomicType.root
    AtomicType.contains
    AtomicType.is_subtype

Adapters
--------

.. autosummary::
    :toctree: ../../generated/

    AtomicType.adapters
    AtomicType.strip
    AtomicType.make_sparse
    AtomicType.make_categorical

Conversions
-----------

.. autosummary::
    :toctree: ../../generated/

    AtomicType.to_boolean
    AtomicType.to_integer
    AtomicType.to_float
    AtomicType.to_complex
    AtomicType.to_decimal
    AtomicType.to_string
    AtomicType.to_object

Misc
----

.. autosummary::
    :toctree: ../../generated/

    AtomicType.make_nullable
    AtomicType.upcast
    AtomicType.larger

Special Methods
---------------

.. autosummary::
    :toctree: ../../generated/

    AtomicType.__contains__
    AtomicType.__eq__
    AtomicType.__hash__
    AtomicType.__init_subclass__
    AtomicType.__str__
    AtomicType.__repr__
