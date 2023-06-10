.. currentmodule:: pdcast

API Reference
=============
``pdcast`` exposes the following interface for public use:

.. raw:: html

    <h2>Type system</h2>

The following data structures define the standard interface for the ``pdcast``
:doc:`type system <../types/types>` and allow users to easily
:doc:`extend <../tutorial>` it to arbitrary data.

.. list-table::

    * - :class:`Type`
      - TODO
    * - :class:`TypeRegistry`
      - TODO
    * - :class:`ScalarType`
      - TODO
    * - :class:`AbstractType`
      - TODO
    * - :class:`DecoratorType`
      - TODO
    * - :class:`CompositeType`
      - TODO
    * - :func:`@register <register>`
      - TODO

.. raw:: html

    <h2>Checks, inference & resolution</h2>

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

.. raw:: html

  <h2>Universal conversions</h2>

.. list-table::

    * - :func:`cast`
      - Cast arbitrary data to the specified type.

.. raw:: html

  <h2>Extension functions</h2>

.. list-table::

    * - :func:`@dispatch <dispatch>`
      - TODO
    * - :func:`@extension_func <extension_func>`
      - TODO
    * - :func:`@attachable <attachable>`
      - TODO

.. raw:: html

    <h2>Pandas integration</h2>

These utilities can be used to attach ``pdcast``-related functionality directly
to Pandas data structures based on their inferred type.

.. list-table::

    * - :func:`attach`
      - TODO
    * - :func:`detach`
      - TODO

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



.. toctree::
    :hidden:
    :maxdepth: 1

    pdcast.Type <Type>
    pdcast.TypeRegistry <TypeRegistry>
    pdcast.ScalarType <ScalarType>
    pdcast.AbstractType <AbstractType>
    pdcast.DecoratorType <DecoratorType>
    pdcast.CompositeType <CompositeType>
    pdcast.register <register>
    pdcast.resolve_type <resolve_type>
    pdcast.detect_type <detect_type>
    pdcast.typecheck <typecheck>
    pdcast.cast <cast>
    pdcast.dispatch <dispatch>
    pdcast.extension_func <extension_func>
    pdcast.attachable <attachable>
    pdcast.attach <attach>
    pdcast.detach <detach>
