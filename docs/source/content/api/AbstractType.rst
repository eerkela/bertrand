.. currentmodule:: pdcast

.. _AbstractType:

pdcast.AbstractType
===================

.. autoclass:: AbstractType

.. raw:: html
    :file: ../../images/types/Types_UML.html

.. _AbstractType.constructors:

Constructors
------------
These are automatically called by :func:`detect_type() <pdcast.detect_type>`
and :func:`resolve_type() <pdcast.resolve_type>` to create instances of the
associated type.

.. autosummary::
    :toctree: ../../generated

    AbstractType.from_string
    AbstractType.from_dtype
    AbstractType.from_scalar
    AbstractType.kwargs
    AbstractType.replace
    AbstractType.__call__

.. _AbstractType.membership:

Membership
----------
These are called by :func:`typecheck` and :func:`@dispatch <dispatch>` to
perform membership tests.

.. autosummary::
    :toctree: ../../generated

    AbstractType.contains
    AbstractType.__contains__

.. _AbstractType.hierarchy:

Hierarchies
-----------
:class:`AbstractTypes <AbstractType>` form the nodes of type hierarchies, which
can be established using the following class decorators.

.. autosummary::
    :toctree: ../../generated

    AbstractType.subtype
    AbstractType.implementation
    AbstractType.default

See the :ref:`tutorial <tutorial>` for more information on creating types and
establishing hierarchies.

.. _AbstractType.config:

Configuration
-------------
:class:`AbstractTypes <AbstractType>` inherit the same interface as their
:class:`ScalarType` equivalents, and can be used interchangeably with them.
They forward any attributes that are not defined on the type itself to their
:meth:`default <pdcast.AbstractType.default>` concretion.

.. autosummary::
    :toctree: ../../generated

    AbstractType.name
    AbstractType.aliases
    AbstractType.type_def
    AbstractType.dtype
    AbstractType.itemsize
    AbstractType.is_numeric
    AbstractType.max
    AbstractType.min
    AbstractType.is_nullable
    AbstractType.na_value
    AbstractType.make_nullable
    AbstractType.__getattr__
    AbstractType.__setattr__

.. _AbstractType.traversal:

Traversal
---------
The :ref:`hierarchies <AbstractType.hierarchy>` that are created by these types
can be traversed using these properties:

.. autosummary::
    :toctree: ../../generated

    AbstractType.is_root
    AbstractType.root
    AbstractType.supertype
    AbstractType.is_generic
    AbstractType.generic
    AbstractType.backends
    AbstractType.subtypes
    AbstractType.is_leaf
    AbstractType.leaves

.. _AbstractType.downcast:

Upcast/Downcast
---------------
Just like :class:`ScalarTypes <pdcast.ScalarType>`,
:class:`AbstractTypes <pdcast.AbstractType>` can be dynamically resized based
on example data.

.. autosummary::
    :toctree: ../../generated

    AbstractType.larger
    AbstractType.smaller
    AbstractType.__lt__
    AbstractType.__gt__

.. _AbstractType.decorators:

Decorators
----------
Lastly, :class:`AbstractTypes <pdcast.AbstractType>` can be wrapped with
:class:`DecoratorTypes <pdcast.DecoratorType>` to adjust their behavior on a
case-by-case basis.

.. autosummary::
    :toctree: ../../generated

    AbstractType.decorators
    AbstractType.unwrap
    AbstractType.make_sparse
    AbstractType.make_categorical

.. _AbstractType.special:

Special Methods
---------------
Similar to :class:`ScalarTypes <pdcast.ScalarType>`,
:class:`AbstractTypes <pdcast.AbstractType>` are hashable, and can be used as
dictionary keys and set elements.

.. autosummary::
    :toctree: ../../generated

    AbstractType.__instancecheck__
    AbstractType.__subclasscheck__
    AbstractType.__hash__
    AbstractType.__eq__
    AbstractType.__str__
