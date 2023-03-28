.. TODO: move this entirely into API docs

Implementation
==============

Below is a rough overview of the internal data structures ``pdcast`` uses to
process data, with condensed descriptions for their important functionality.
This is intended to provide increased context for those seeking to extend
``pdcast`` in some way, or those simply interested in learning how it works.
If a question is not answered by this document, check the API docs for more
information.


.. toctree::
    :maxdepth: 1

    AtomicType <atomic>
    AdapterType <adapter>
    CompositeType <composite>
    SeriesWrapper <series>
    Type Specification Mini-Language <mini_language>
    Conversions <cast>
    Dispatch <dispatch>
