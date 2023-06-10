.. currentmodule:: pdcast

.. _Type:

pdcast.Type
===========

.. autoclass:: Type

.. raw:: html
    :file: ../../images/types/Types_UML.html

.. _Type.aliases:

Aliases
-------
Every type within the ``pdcast`` type system supports the addition of dynamic
aliases, which are used to configure the output of :func:`detect_type` and
:func:`resolve_type`.

.. autosummary::
    :toctree: ../../generated

    Type.aliases

..
    HACK - commenting out an autosummary directive like this will still
    generate stubs, which can then be linked inside aliases.

    .. autosummary::
        :toctree: ../../generated

        AliasManager
        AliasManager.add
        AliasManager.remove
        AliasManager.discard
        AliasManager.pop
        AliasManager.clear

Aliases can be defined either statically (by including them in a
:ref:`type definition <tutorial.type_def>`) or dynamically, by
:meth:`adding <pdcast.AliasManager.add>` or
:meth:`removing <pdcast.AliasManager.remove>` them at runtime.

.. _Type.constructors:

Constructors
------------
When a registered alias is encountered in
:func:`detect_type() <pdcast.detect_type>` or
:func:`resolve_type() <pdcast.resolve_type>`, one of the following constructors
will be invoked:

.. autosummary::
    :toctree: ../../generated

    Type.from_string
    Type.from_dtype
    Type.from_scalar

Individual types can override these methods to customize their behavior.

Membership
----------
Types can be arranged into trees to represent
:ref:`hierarchical <AbstractType.hierarchy>` relationships.  The following
methods allow membership tests to be performed on these trees.

.. autosummary::
    :toctree: ../../generated

    Type.contains
    Type.__contains__
