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
associated type or one of its
:meth:`implementations <pdcast.AbstractType.implementation>`.

.. autosummary::
    :toctree: ../../generated

    AbstractType.from_string
    AbstractType.from_dtype
    AbstractType.from_scalar
    AbstractType.kwargs
    AbstractType.replace
    AbstractType.__call__
