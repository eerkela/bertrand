import inspect
from functools import wraps
from typing import Any, Callable, Iterable

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util.type_hints import type_specifier

from .adapter import AdapterType
from .atomic import ScalarType, AtomicType
from .composite import CompositeType


# TODO: dispatch should have an option to allow length changes?


# TODO: move this alongside detect, resolve, check


######################
####    PUBLIC    ####
######################


def dispatch(
    _method=None,
    *,
    namespace: str = None,
    types: type_specifier | Iterable[type_specifier] = None,
    include_subtypes: bool = True
):
    """Dispatch a method to :class:`pandas.Series` objects of the associated
    type.

    Parameters
    ----------
    namespace : str, default None
        Hide the dispatched method behind a virtual namespace, similar to
        :attr:`pandas.Series.dt`, :attr:`pandas.Series.str`, etc.  The
        attribute will only be accessible from this extended namespace.  This
        is useful for preventing conflicts with existing pandas functionality.
    types : type specifier | Iterable[type specifier], default None
        The existing types to broadcast this method to.  These can be given
        in any format recognized by :func:`resolve_type`, and any number of
        types can be specified.

        .. note::

            If a type accepts parametrized arguments, the method will be
            attached to its *class definition*.  This means that it will be
            accessible regardless of the specified parameter settings. If the
            method only applies when certain settings are selected, then it
            must do the filtering itself.

    include_subtypes : bool, default True
        Specifies whether to add the dispatched method to subtypes of the
        given types.  If this is set to ``False``, then the method will
        only be added to the types themselves (along with each of their
        implementations if any are :func:`generic`).

    Raises
    ------
    ValueError
        If ``types`` is given and could not be resolved, or if it contains
        types that are wrapped in adapters (e.g. ``"sparse[int]"``, etc.).
        If you'd like to dispatch method to an :class:`AdapterType` globally,
        then it should be naked when it is provided to this function (e.g.
        ``"sparse"`` rather than ``"sparse[int]"``).
    TypeError
        If the decorated method does not conform to the dispatch criteria.

    Warnings
    --------
    If a dispatched method's name conflicts with a core pandas method, then it
    will be called in any internals that utilize that method.  Care should be
    taken not to break existing functionality in this case.  Using the
    ``namespace`` argument helps to prevent this.

    See Also
    --------
    pandas.Series.round : Customizable rounding for numeric series.
    pandas.Series.dt.tz_localize : Abstract timezone localization for datetime series.
    pandas.Series.dt.tz_convert : Abstract timezone conversion for datetime series.

    Notes
    -----
    This function can be used in one of two ways.  The first is to decorate a
    method of a type definition in-place:

    .. code::

        @pdcast.register
        class CustomType(pdcast.AtomicType):
            ...

            @dispatch
            def method(
                self,
                series: pdcast.SeriesWrapper,
                ...
            ) -> pdcast.SeriesWrapper:
                ...

        ...

    The second is to define a function at the module level and use the
    ``types`` argument to broadcast it to a selection of types.

    .. code::

        @dispatch(types="CustomType1, CustomType2, ...")
        def method(series: pdcast.SeriesWrapper, ...) -> pdcast.SeriesWrapper:
            ...

    The first option is preferred if you control the type definition.  The
    second can be used to add new dispatch methods to existing types.

    Regardless of which style is chosen, the method must meet certain criteria:

        #.  It must accept a :class:`SeriesWrapper` as its first non-self
            argument.
        #.  It must return a :class:`SeriesWrapper`.

    Both of these conditions are validated by the decorator when it is applied.

    On an implementation level, this function works by intercepting attribute
    access to :class:`pandas.Series` objects.  When an attribute is requested,
    its name is first compared against a table of all the methods that have
    been decorated by this function.  If a match is found, then the type of the
    series is inferred, and if the inferred type implements the named method,
    then it will be chosen instead of the default implementation.  Otherwise,
    the attribute access is treated normally.
    """
    types = CompositeType() if types is None else resolve.resolve_type([types])

    def dispatch_decorator(method):
        has_self = validate_dispatch_signature(method)

        @wraps(method)
        def dispatch_wrapper(self, *args, **kwargs):
            if has_self:
                return method(self, *args, **kwargs)
            return method(*args, **kwargs)

        # mark as dispatch method
        dispatch_wrapper._dispatch = True
        dispatch_wrapper._namespace = namespace
        broadcast_to_types(dispatch_wrapper, types, include_subtypes)
        return dispatch_wrapper

    if _method is None:
        return dispatch_decorator
    return dispatch_decorator(_method)


def generic(_class: type):
    """Class decorator to mark generic AtomicType definitions.

    Generic types are backend-agnostic and act as wildcard containers for
    more specialized subtypes.  For instance, the generic "int" can contain
    the backend-specific "int[numpy]", "int[pandas]", and "int[python]"
    subtypes, which can be resolved as shown. 
    """
    # NOTE: something like this would normally be handled using a decorating
    # class rather than a function.  Doing it this way (patching) has the
    # advantage of preserving the original type for issubclass() checks
    if not issubclass(_class, AtomicType):
        raise TypeError(f"`@generic` can only be applied to AtomicTypes")

    # verify init is empty.  NOTE: cython __init__ is not introspectable.
    if (
        _class.__init__ != AtomicType.__init__ and
        inspect.signature(_class).parameters
    ):
        raise TypeError(
            f"To be generic, {_class.__name__}.__init__() cannot "
            f"have arguments other than self"
        )

    # remember original equivalents
    cdef dict orig = {k: getattr(_class, k) for k in ("instance", "resolve")}

    @classmethod
    def instance(cls, backend: str = None, *args, **kwargs) -> AtomicType:
        if backend is None:
            return orig["instance"](*args, **kwargs)
        extension = cls._backends.get(backend, None)
        if extension is None:
            raise TypeError(
                f"{cls.name} backend not recognized: {repr(backend)}"
            )
        return extension.instance(*args, **kwargs)

    @classmethod
    def resolve(cls, backend: str = None, *args: str) -> AtomicType:
        if backend is None:
            return orig["resolve"](*args)

        # if a specific backend is given, resolve from its perspective
        specific = cls._backends.get(backend, None)
        if specific is not None and specific not in cls.registry:
            specific = None
        if specific is None:
            raise TypeError(
                f"{cls.name} backend not recognized: {repr(backend)}"
            )
        return specific.resolve(*args)

    @classmethod
    def register_backend(cls, backend: str):
        # NOTE: in this context, cls is an alias for _class
        def decorator(specific: type):
            if not issubclass(specific, AtomicType):
                raise TypeError(
                    f"`generic.register_backend()` can only be applied to "
                    f"AtomicType definitions"
                )

            # ensure backend is unique
            if backend in cls._backends:
                raise TypeError(
                    f"`backend` must be unique, not one of "
                    f"{set(cls._backends)}"
                )

            # ensure backend is self-consistent
            if specific._backend is None:
                specific._backend = backend
            elif backend != specific._backend:
                raise TypeError(
                    f"backends must match ({repr(backend)} != "
                    f"{repr(specific._backend)})"
                )

            # inherit generic attributes
            specific._family = cls._family  # TODO: check if this is already given?
            specific._is_generic = False
            specific.name = cls.name
            specific._generic = cls
            cls._backends[backend] = specific
            specific._is_boolean = specific._is_boolean or cls._is_boolean
            specific._is_numeric = specific._is_numeric or cls._is_numeric
            specific.registry.flush()
            return specific

        return decorator

    # overwrite class attributes
    _class._is_generic = True
    _class._backend = None

    # patch in new methods
    loc = locals()
    for k in orig:
        setattr(_class, k, loc[k])
    _class.register_backend = register_backend
    return _class


def register(_class=None, *, cond=True):
    """Validate an AtomicType definition and add it to the registry.

    Note: Any decorators above this one will be ignored during validation.
    """
    def register_decorator(_class_):
        if not issubclass(_class_, ScalarType):
            raise TypeError(
                f"`@register` can only be applied to AtomicType and "
                f"AdapterType definitions"
            )
        if cond:
            AtomicType.registry.add(_class_)
        return _class_

    if _class is None:
        return register_decorator
    return register_decorator(_class)


def subtype(supertype: type):
    """Class decorator to establish type hierarchies."""
    if not issubclass(supertype, AtomicType):
        raise TypeError(f"`supertype` must be a subclass of AtomicType")

    def decorator(class_def: type):
        if not issubclass(class_def, AtomicType):
            raise TypeError(
                f"`@subtype()` can only be applied to AtomicType definitions"
            )

        # break circular references
        ref = supertype
        while ref is not None:
            if ref is class_def:
                raise TypeError(
                    "Type hierarchy cannot contain circular references"
                )
            ref = ref._parent

        # check type is not already registered
        if class_def._parent:
            raise TypeError(
                f"AtomicTypes can only be registered to one supertype at a "
                f"time (`{class_def.__name__}` is currently registered to "
                f"`{class_def._parent.__name__}`)"
            )

        # inherit supertype attributes
        class_def._family = supertype._family  # TODO: check if this is already given?

        # overwrite class attributes
        class_def._parent = supertype
        supertype._children.add(class_def)
        class_def._is_boolean = class_def._is_boolean or supertype._is_boolean
        class_def._is_numeric = class_def._is_numeric or supertype._is_numeric
        AtomicType.registry.flush()
        return class_def

    return decorator


#######################
####    PRIVATE    ####
#######################


# NOTE: cython stores type hints as strings; python stores them as references.
cdef set valid_series_annotations = {"SeriesWrapper", convert.SeriesWrapper}


def validate_dispatch_signature(call: Callable) -> bool:
    """Given a callable decorated with @dispatch, ensure that its signature is
    compatible.

    This function returns a boolean, which indicates whether the callable
    includes a ``self`` argument.
    """
    # check for compatible signature
    sig = inspect.signature(call)
    pars = list(sig.parameters.items())
    has_self = False
    if not pars:
        raise TypeError(
            f"@dispatch method {repr(call.__qualname__)}() signature "
            f"must not be empty"
        )
    if pars[0][0] == "self":
        has_self = True
        pars = pars[1:]
    if not pars or pars[0][1].annotation not in valid_series_annotations:
        raise TypeError(
            f"@dispatch method {call.__qualname__}() must accept a "
            f"'SeriesWrapper' as its first non-self argument"
        )
    if sig.return_annotation not in valid_series_annotations:
        raise TypeError(
            f"@dispatch method {call.__qualname__}() must return a "
            f"'SeriesWrapper' object"
        )

    return has_self


def broadcast_to_types(
    call: Callable,
    types: CompositeType,
    include_subtypes: bool
) -> None:
    """Add the given callable to each of the specified types."""
    name = call.__name__

    # ensure all types are dispatchable
    for typ in types:
        if isinstance(typ, AdapterType) and typ.wrapped is not None:
            raise ValueError(
                f"adapters are ignored during @dispatch: {typ}"
            )

    # attach to selected types
    for typ in types:
        # adapters don't have backends/subtypes
        if isinstance(typ, AdapterType):
            setattr(type(typ), name, call)

        else:
            for back in typ.backends.values():  # add to all backends
                if include_subtypes:
                    for t in back.subtypes:
                        setattr(type(t), name, call)
                else:
                    setattr(type(back), name, call)

    # force rebuild of dispatch_map
    AtomicType.registry.flush()
