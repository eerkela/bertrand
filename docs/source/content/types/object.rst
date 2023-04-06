.. currentmodule:: pdcast

Object
======

.. _object_type:

.. autoclass:: ObjectType


``ObjectType``\s use the type specification mini-language to specify python
types to dynamically wrap.  These are pulled directly from the calling
environment via the ``inspect`` module, which can resolve them directly by
name.

.. doctest:: type_resolution

    >>> class CustomObj:
    ...     pass

    >>> pdcast.resolve_type("object[int]")
    ObjectType(type_def=<class 'int'>)
    >>> pdcast.resolve_type("object[CustomObj]")
    ObjectType(type_def=<class 'CustomObj'>)
