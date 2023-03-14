import inspect
from functools import wraps
from typing import Any, Iterable

cimport pdcast.convert as convert
import pdcast.convert as convert

from pdcast.util.type_hints import type_specifier

from .atomic import ScalarType, AtomicType


# TODO: implement the types argument
# -> If types is not None, check to make sure that the wrapped object is not
# bound.  If it is, throw a ValueError.  Else, if it does not have a self
# argument, insert one before broadcasting to each type definition. This
# requires an extra decorator.


######################
####    PUBLIC    ####
######################


def dispatch(
    _method=None,
    *,
    namespace: str = None,
    types: type_specifier | Iterable[type_specifier] = None
):
    """Dispatch a method to ``pandas.Series`` objects of the associated type.

    Parameters
    ----------
    namespace : str, default None
        Hide the dispatched method behind a virtual namespace, similar to
        ``pandas.Series.dt``, ``pandas.Series.str``, etc.  The attribute will
        only be accessible from this extended namespace.  This is useful for
        preventing conflicts with existing pandas functionality.
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

    Raises
    ------
    ValueError
        If ``types`` is given and could not be resolved.
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
    access to ``pandas.Series`` objects.  When an attribute is requested, its
    name is first compared against a table of all the methods that have been
    decorated by this function.  If a match is found, then the type of the
    series is inferred, and if the inferred type implements the named method,
    then it will be chosen instead of the default implementation.  Otherwise,
    the attribute access is treated normally.
    """
    has_options = _method is not None
    if has_options:
        validate_dispatch_signature(_method)

    def dispatch_decorator(method):
        if not has_options:
            validate_dispatch_signature(method)

        @wraps(method)
        def dispatch_wrapper(self, *args, **kwargs):
            return method(self, *args, **kwargs)

        dispatch_wrapper._dispatch = True
        dispatch_wrapper._namespace = namespace
        return dispatch_wrapper

    if has_options:
        return dispatch_decorator(_method)
    return dispatch_decorator


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
    cdef dict orig = {
        k: getattr(_class, k) for k in (
            "_generate_subtypes", "instance", "resolve"
        )
    }

    def _generate_subtypes(self, types: set) -> frozenset:
        result = orig["_generate_subtypes"](self, types)
        for k, v in self.backends.items():
            if k is not None and v in self.registry:
                result |= v.instance().subtypes.atomic_types
        return result

    @classmethod
    def instance(cls, backend: str = None, *args, **kwargs) -> AtomicType:
        if backend is None:
            return orig["instance"](*args, **kwargs)
        extension = cls.backends.get(backend, None)
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
        specific = cls.backends.get(backend, None)
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
            if backend in cls.backends:
                raise TypeError(
                    f"`backend` must be unique, not one of {set(cls.backends)}"
                )

            # ensure backend is self-consistent
            if specific.backend is None:
                specific.backend = backend
            elif backend != specific.backend:
                raise TypeError(
                    f"backends must match ({repr(backend)} != "
                    f"{repr(specific.backend)})"
                )

            # inherit generic attributes
            specific.is_generic = False
            specific.conversion_func = cls.conversion_func
            specific.name = cls.name
            specific._generic = cls
            cls.backends[backend] = specific
            specific._is_boolean = specific._is_boolean or cls._is_boolean
            specific._is_numeric = specific._is_numeric or cls._is_numeric
            specific.model_type = cls.model_type
            specific.registry.flush()
            return specific

        return decorator

    # overwrite class attributes
    _class.is_generic = True
    _class.backend = None
    _class.backends = {None: _class}

    # patch in new methods
    loc = locals()
    for k in orig:
        setattr(_class, k, loc[k])
    _class.register_backend = register_backend
    return _class


def register(_class=None, *, ignore=False):
    """Validate an AtomicType definition and add it to the registry.

    Note: Any decorators above this one will be ignored during validation.
    """
    def register_decorator(_class_):
        if not issubclass(_class_, ScalarType):
            raise TypeError(
                f"`@register` can only be applied to AtomicType and "
                f"AdapterType definitions"
            )
        if not ignore:
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
        class_def.conversion_func = supertype.conversion_func

        # overwrite class attributes
        class_def.is_root = False
        class_def._parent = supertype
        supertype._children.add(class_def)
        class_def._is_boolean = class_def._is_boolean or supertype._is_boolean
        class_def._is_numeric = class_def._is_numeric or supertype._is_numeric
        class_def.model_type = supertype.model_type
        AtomicType.registry.flush()
        return class_def

    return decorator


#######################
####    PRIVATE    ####
#######################


cdef int validate_dispatch_signature(object call) except -1:
    """Inspect the signature of an AtomicType method decorated with @dispatch,
    ensuring that it accepts and returns SeriesWrapper objects.
    """
    cdef object sig = inspect.signature(call)
    cdef object first_type = list(sig.parameters.values())[1].annotation
    cdef object return_type = sig.return_annotation
    cdef set valid_annotations = {"SeriesWrapper", convert.SeriesWrapper}

    # NOTE: methods defined in .pyx files will store their SeriesWrapper
    # annotations as strings, while those defined in .py files store them as
    # direct references.

    if first_type not in valid_annotations:
        raise TypeError(
            f"@dispatch method {call.__qualname__}() must accept a "
            f"SeriesWrapper as its first argument after self, not {first_type}"
        )
    if return_type not in valid_annotations:
        raise TypeError(
            f"@dispatch method {call.__qualname__}() must return a "
            f"SeriesWrapper object, not {return_type}"
        )
