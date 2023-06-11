.. currentmodule:: pdcast

pdcast.TypeRegistry
===================

.. autoclass:: TypeRegistry

.. raw:: html
    :file: ../../images/types/Types_UML.html

Registration
------------
Types can be manually added or removed from the registry using the following
methods:

.. autosummary::
    :toctree: ../../generated

    TypeRegistry.add
    TypeRegistry.remove

.. note::

    :meth:`add <pdcast.TypeRegistry.add>` can also be called from the
    :func:`@register <pdcast.register>` class decorator for convenience.

State
-----
Whenever a type is :meth:`added <pdcast.TypeRegistry.add>` or
:meth:`removed <pdcast.TypeRegistry.remove>` from the registry or has one of
its :attr:`aliases <pdcast.Type.aliases>` updated, it will generate a unique
hash to represent the current state.  This hash can then be used to cache
computed values according to the following interface:

.. autosummary::
    :toctree: ../../generated

    CacheValue
    TypeRegistry.hash
    TypeRegistry.flush

These are used internally to store the :class:`TypeRegistry <pdcast.TypeRegistry>`
properties listed below.  They can also be used to synchronize external values
with the global state of the ``pdcast`` type system where applicable.

Accessors
---------
The registry stores a variety of accessors for various parts of the ``pdcast``
type system.

.. autosummary::
    :toctree: ../../generated

    TypeRegistry.roots
    TypeRegistry.leaves
    TypeRegistry.families
    TypeRegistry.decorators
    TypeRegistry.abstract

These can be used to select slices of the registry based on their position
within the hierarchy.

Aliases
-------
The registry also maintains a map of every alias to its associated type, as
well as a set of regular expressions for use in the
:ref:`type specification mini-language <resolve_type.mini_language>`.

.. autosummary::
    :toctree: ../../generated

    TypeRegistry.aliases
    TypeRegistry.regex
    TypeRegistry.resolvable

Relationships
-------------
Additionally, the registry is responsible for maintaining the links between
each type and traversing their respective hierarchies.

.. autosummary::
    :toctree: ../../generated/

    TypeRegistry.get_default
    TypeRegistry.get_supertype
    TypeRegistry.get_subtypes
    TypeRegistry.get_generic
    TypeRegistry.get_implementations

These relationships are established by
:class:`AbstractTypes <pdcast.AbstractType>` through their various
:ref:`hierarchical decorators <AbstractType.hierarchy>`.

Configuration
-------------
Lastly, the registry contains some global configuration values for types within
the ``pdcast`` type system.

.. autosummary::
    :toctree: ../../generated

    TypeRegistry.decorator_priority

..
    HACK - commenting out an autosummary directive like this will still
    generate stubs, which can then be linked inside decorator_priority.

    .. autosummary::
        :toctree: ../../generated

        PriorityList
        PriorityList.index
        PriorityList.move_up
        PriorityList.move_down
        PriorityList.move

Special methods
---------------
These methods are used for syntactic sugar related to registry manipulations.

.. autosummary::
    :toctree: ../../generated

    TypeRegistry.__iter__
    TypeRegistry.__len__
    TypeRegistry.__contains__
    TypeRegistry.__getitem__
