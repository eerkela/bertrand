.. currentmodule:: pdcast

API Reference
=============
``pdcast`` exposes the following interface for public use:

.. raw:: html

    <h2>Basic usage</h2>

The functions below are used to navigate the ``pdcast``
:doc:`type system <../types/types>` and convert data from one representation to
another.

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

.. toctree::
    :hidden:
    :maxdepth: 1

    pdcast.resolve_type <resolve_type>
    pdcast.detect_type <detect_type>
    pdcast.typecheck <typecheck>
    pdcast.cast <cast>

.. raw:: html

    <h2>Type objects</h2>

The following data structures define the standard interface for the ``pdcast``
:doc:`type system <../types/types>` and allow users to easily
:doc:`extend <../tutorial>` it to arbitrary data.

.. list-table::

    * - :class:`AtomicType`
      - TODO
    * - :class:`AdapterType`
      - TODO
    * - :class:`CompositeType`
      - TODO
    * - :class:`TypeRegistry`
      - TODO
    * - :func:`@register <register>`
      - TODO
    * - :func:`@subtype <subtype>`
      - TODO
    * - :func:`@generic <generic>`
      - TODO

.. toctree::
    :hidden:
    :maxdepth: 1

    pdcast.AtomicType <AtomicType>
    pdcast.AdapterType <AdapterType>
    pdcast.CompositeType <CompositeType>
    pdcast.TypeRegistry <TypeRegistry>
    pdcast.register <register>
    pdcast.subtype <subtype>
    pdcast.generic <generic>

.. raw:: html

    <h2>Pandas integration</h2>

These utilities can be used to attach ``pdcast``-related functionality directly
to Pandas data structures based on their inferred type.

.. list-table::

    * - :func:`attach`
      - TODO
    * - :func:`detach`
      - TODO
    * - :func:`@attachable <attachable>`
      - TODO
    * - :func:`@extension_func <extension_func>`
      - TODO
    * - :func:`@dispatch <dispatch>`
      - TODO
    * - :class:`SeriesWrapper`
      - TODO

.. toctree::
    :hidden:
    :maxdepth: 1

    pdcast.attach <attach>
    pdcast.detach <detach>
    pdcast.attachable <attachable>
    pdcast.extension_func <extension_func>
    pdcast.dispatch <dispatch>
    pdcast.SeriesWrapper <SeriesWrapper>

``pdcast`` uses these tools to add the following methods to Pandas data
structures when :func:`attach` is invoked.

.. list-table::

    * - :meth:`pandas.Series.cast`
      - TODO
    * - :meth:`pandas.Series.typecheck`
      - TODO
    * - :attr:`pandas.Series.element_type`
      - TODO
    * - :meth:`pandas.Series.round`
      - TODO
    * - :meth:`pandas.Series.dt.tz_localize`
      - TODO
    * - :meth:`pandas.Series.dt.tz_convert`
      - TODO



