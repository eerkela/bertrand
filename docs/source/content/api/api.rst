.. currentmodule:: pdcast

API Reference
=============
This page gives an overview of the top-level public attributes exposed by
``pdcast``.  These rely on the internal objects listed alongside them in the
sidebar.

.. list-table::

    * - :func:`resolve_type`
      - Interpret types from manual
        :ref:`type specifiers <resolve_type.type_specifiers>`.
    * - :func:`detect_type`
      - Infer types from example data.
    * - :func:`typecheck`
      - Check whether example data contains elements of a specified type.
    * - :func:`cast`
      - Cast arbitrary data to the specified type.
    * - :func:`attach`
      - TODO
    * - :func:`detach`
      - TODO
    * - :func:`@dispatch <dispatch>`
      - TODO
    * - :func:`@extension_func <extension_func>`
      - TODO
    * - :func:`@register <register>`
      - TODO
    * - :func:`@subtype <subtype>`
      - TODO
    * - :func:`@generic <generic>`
      - TODO
    * - :class:`AtomicType`
      - TODO
    * - :class:`AdapterType`
      - TODO
    * - :class:`CompositeType`
      - TODO
    * - :class:`TypeRegistry`
      - TODO
    * - :class:`SeriesWrapper`
      - TODO

.. toctree::
    :hidden:
    :maxdepth: 1

    pdcast.resolve_type <resolve_type>
    pdcast.detect_type <detect_type>
    pdcast.typecheck <typecheck>
    pdcast.cast <cast>
    pdcast.extension_func <extension_func>
    pdcast.attach <attach>
    pdcast.detach <detach>
    pdcast.dispatch <dispatch>
    pdcast.register <register>
    pdcast.subtype <subtype>
    pdcast.generic <generic>
    pdcast.AtomicType <AtomicType>
    pdcast.AdapterType <AdapterType>
    pdcast.CompositeType <CompositeType>
    pdcast.TypeRegistry <TypeRegistry>
    pdcast.SeriesWrapper <SeriesWrapper>
