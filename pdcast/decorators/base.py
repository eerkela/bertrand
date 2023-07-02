"""This module describes a base class for decorators that allow cooperative
attribute access with nested objects.
"""
from __future__ import annotations
from functools import update_wrapper, WRAPPER_ASSIGNMENTS
import inspect
from types import MappingProxyType
from typing import Any, Callable, Mapping


# shortcut for inspect.Parameter.empty
EMPTY = inspect.Parameter.empty


# shortcut for inspect.Parameter.kind
KINDS = {
    "POSITIONAL_ONLY": inspect.Parameter.POSITIONAL_ONLY,
    "POSITIONAL_OR_KEYWORD": inspect.Parameter.POSITIONAL_OR_KEYWORD,
    "VAR_POSITIONAL": inspect.Parameter.VAR_POSITIONAL,
    "KEYWORD_ONLY": inspect.Parameter.KEYWORD_ONLY,
    "VAR_KEYWORD": inspect.Parameter.VAR_KEYWORD,
}


class FunctionDecorator:
    """Base class for cooperative decorators.

    These decorators implement the Gang of Four's `Decorator pattern
    <https://en.wikipedia.org/wiki/Decorator_pattern>`_ to  cooperatively pass
    attribute access down a (possibly nested) decorator stack.

    Parameters
    ----------
    func : Callable
        The function (or other callable) to decorate.  Any attributes that are
        not found on this decorator will be dynamically passed down to this
        object.
    **kwargs : dict
        Keyword arguments to use for multiple inheritance.

    Notes
    -----
    This expects all subclasses to define the following attribute at the class
    level:

        *   ``_reserved``: a set of strings describing attribute names that are
            reserved for this decorator.

    The special string ``"__wrapped__"`` is always added to this set,
    representing the decorated callable itself.  This is automatically assigned
    by :func:`update_wrapper <python:functools.update_wrapper>` during
    ``__init__``.
    """

    _reserved = set(WRAPPER_ASSIGNMENTS) | {"__wrapped__"}

    def __init__(self, func: Callable, **kwargs):
        super().__init__(**kwargs)  # allow multiple inheritance
        update_wrapper(self, func)

    ################################
    ####    ATTRIBUTE ACCESS    ####
    ################################

    def __getattr__(self, name: str) -> Any:
        """Delegate getters to wrapped object."""
        return getattr(self.__wrapped__, name)

    def __setattr__(self, name: str, value: Any) -> None:
        """Delegate setters to wrapped object."""
        if name in self._reserved or hasattr(type(self), name):
            super().__setattr__(name, value)
        else:
            setattr(self.__wrapped__, name, value)

    def __delattr__(self, name: str) -> None:
        """Delegate deleters to wrapped object."""
        # NOTE: hasattr() doesn't respect nested decorators
        try:
            object.__getattribute__(self, name)
            super().__delattr__(name)
        except AttributeError:
            delattr(self.__wrapped__, name)

    ################################
    ####     SPECIAL METHODS    ####
    ################################

    def __call__(self, *args, **kwargs):
        """Invoke the wrapped object's ``__call__()`` method."""
        return self.__wrapped__(*args, **kwargs)

    def __dir__(self) -> list:
        """Include attributes of wrapped object."""
        result = dir(type(self))
        result += [k for k in self.__dict__ if k not in result]
        result += [k for k in dir(self.__wrapped__) if k not in result]
        return result

    def __str__(self) -> str:
        """Pass ``str()`` calls to wrapped object."""
        return str(self.__wrapped__)

    def __repr__(self) -> str:
        """Pass ``repr()`` calls to wrapped object."""
        return repr(self.__wrapped__)

    def __next__(self) -> Any:
        """Pass ``next()`` calls to wrapped object."""
        return next(self.__wrapped__)

    def __iter__(self):
        """Pass ``iter()`` calls to wrapped object."""
        return iter(self.__wrapped__)

    def __len__(self) -> int:
        """Pass ``len()`` calls to wrapped object."""
        return len(self.__wrapped__)

    def __getitem__(self, key) -> Any:
        """Pass indexing to wrapped object."""
        return self.__wrapped__.__getitem__(key)

    def __setitem__(self, key, value) -> None:
        """Pass index assignment to wrapped object."""
        return self.__wrapped__.__setitem__(key, value)

    def __delitem__(self, key) -> None:
        return self.__wrapped__.__delitem__(key)

    def __contains__(self, item: Any) -> bool:
        """Pass ``in`` keyword to wrapped object."""
        return item in self.__wrapped__

    # TODO: math, comp operators, etc.


class Signature:
    """An extensible wrapper around an
    :class:`inspect.Parameter <python:inspect.Parameter>` object that allows
    easy modification of parameters and annotations.

    Parameters
    ----------
    func : Callable
        A function or other callable to introspect.
    """

    def __init__(self, func: Callable):
        self.func_name = func.__qualname__
        self.signature = inspect.signature(func)
        self.original = self.signature.replace()  # store a copy

    @property
    def parameter_map(self) -> Mapping[str, inspect.Parameter]:
        """A read-only mapping from argument names to their corresponding
        :class:`Parameters <python:inspect.Parameter>`.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig
            <Signature (bar, baz=2, **kwargs)>
            >>> sig.parameter_map
            mappingproxy(OrderedDict([('bar', <Parameter "bar">), ('baz', <Parameter "baz=2">), ('kwargs', <Parameter "**kwargs">)]))
        """
        return self.signature.parameters

    @property
    def parameters(self) -> tuple[inspect.Parameter, ...]:
        """Access the :class:`Parameter <python:inspect.Parameter>` objects
        that are stored in a signature.

        Returns
        -------
        tuple[inspect.Parameter]
            An ordered tuple containing every parameter in the signature.

        Notes
        -----
        This property can be written to in order to allow parameters to be
        replaced, removed, and/or extended at runtime.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig.parameters
            (<Parameter "bar">, <Parameter "baz=2">, <Parameter "**kwargs">)
            >>> sig.parameters = [par.replace(annotation=int) for par in sig.parameters]
            >>> sig.parameters
            (<Parameter "bar: int">, <Parameter "baz: int = 2">, <Parameter "**kwargs: int">)
            >>> del sig.parameters
            >>> sig.parameters
            (<Parameter "bar">, <Parameter "baz=2">, <Parameter "**kwargs">)
        """
        return tuple(self.signature.parameters.values())

    @parameters.setter
    def parameters(self, val: tuple[inspect.Parameter]) -> None:
        self.signature = self.signature.replace(parameters=val)

    @parameters.deleter
    def parameters(self) -> None:
        self.signature = self.original

    @property
    def return_annotation(self) -> Any:
        """Access the return annotation of the signature.

        Returns
        -------
        Any
            The raw return annotation for the signature.

        Notes
        -----
        This property can be written to in order to allow the return annotation
        to be replaced or removed at runtime.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig.return_annotation
            <class 'inspect._empty'>
            >>> sig.return_annotation = dict[str, int]
            >>> sig.return_annotation
            dict[str, int]
            >>> sig
            '(bar, baz= 2, **kwargs) -> dict[str, int]'
            >>> del sig.return_annotation
            >>> sig.return_annotation
            <class 'inspect._empty'>
        """
        return self.signature.return_annotation

    @return_annotation.setter
    def return_annotation(self, val: Any) -> None:
        self.signature = self.signature.replace(return_annotation=val)

    @return_annotation.deleter
    def return_annotation(self) -> None:
        self.signature = self.signature.replace(
            return_annotation=self.original.return_annotation
        )

    def add_parameter(
        self,
        name: str,
        kind: str = "POSITIONAL_OR_KEYWORD",
        default: Any = EMPTY,
        annotation: Any = EMPTY
    ) -> None:
        """Add a parameter to the signature.

        Parameters
        ----------
        name : str
            The name of the parameter to add.
        kind : str, default "POSITIONAL_OR_KEYWORD"
            A string specifying the species of parameter to add.  Must be one
            of the kinds specified in
            :class:`inspect.Parameter.kind <python:inspect.Parameter.kind>`.
        default : Any, default EMPTY
            The default value for the parameter.  If the parameter has no
            default, leave this empty.
        annotation : Any, default EMPTY
            The type annotation for the parameter.  If the parameter has no
            annotation, leave this empty.

        Raises
        ------
        ValueError
            If the named argument is already present in the signature, or if
            ``kind`` is not one of the kinds specified in
            :class:`inspect.Parameter.kind <python:inspect.Parameter.kind>`.

        See Also
        --------
        Signature.set_parameter :
            Modify a parameter in the signature.
        Signature.remove_parameter :
            Remove a parameter from the signature.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig.add_parameter("qux", kind="KEYWORD_ONLY", default=3, annotation=int)
            >>> sig
            <Signature (bar, baz=2, *, qux: int = 3, **kwargs)>
            >>> sig.add_parameter("corge", kind="POSITIONAL_ONLY")
            >>> sig
            <Signature (corge, /, bar, baz=2, *, qux: int = 3, **kwargs)>
            >>> sig.add_parameter("args", kind="VAR_POSITIONAL")
            >>> sig
            <Signature (corge, /, bar, baz=2, *args, qux: int = 3, **kwargs)>
        """
        if name in self.parameter_map:
            raise ValueError(f"Parameter {name} already exists")

        try:
            kind = KINDS[kind]
        except KeyError as err:
            raise ValueError(f"Invalid kind: {repr(kind)}") from err

        # construct Parameter
        parameter = inspect.Parameter(
            name=name,
            kind=kind,
            default=default,
            annotation=annotation
        )

        # respect Python argument order based on kind.
        # NOTE: we count from the right to maintain lexicographic order.
        reverse = reversed(list(enumerate(self.parameters)))
        generator = (idx + 1 for idx, par in reverse if par.kind <= kind)
        index = next(generator, 0)

        # insert into parameters
        self.parameters = (
            self.parameters[:index] + (parameter,) + self.parameters[index:]
        )

    def set_parameter(
        self,
        _name: str,
        kind: str = None,
        **kwargs
    ) -> None:
        """Modify a parameter by name.

        Parameters
        ----------
        name : str
            The name of the parameter to modify.
        **kwargs
            Keyword arguments to pass to
            :meth:`Parameter.replace() <python:inspect.Parameter.replace>`.

        Raises
        ------
        KeyError
            If the named argument is not contained within the signature.

        See Also
        --------
        Signature.add_parameter :
            Add a parameter to the signature.
        Signature.remove_parameter :
            Remove a parameter from the signature.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig.set_parameter("bar", annotation=int)
            >>> sig
            <Signature (bar: int, baz=2, **kwargs)>
            >>> sig.set_parameter("baz", default=3)
            >>> sig
            <Signature (bar: int, baz=3, **kwargs)>
            >>> sig.set_parameter("baz", name="qux")
            >>> sig
            <Signature (bar: int, qux=3, **kwargs)>
            >>> sig.set_parameter("bar", kind="KEYWORD_ONLY")
            >>> sig
            <Signature (qux=3, *, bar: int, **kwargs)>
        """
        if _name not in self.parameter_map:
            raise KeyError(_name)

        # replace parameter in-place
        if kind is None:
            self.parameters = tuple(
                par.replace(**kwargs) if par.name == _name else par
                for par in self.parameters
            )

        # reorder the signature to respect the new kind
        else:
            kind = KINDS[kind]
            param = self.parameter_map[_name].replace(kind=kind, **kwargs)

            # remove parameter
            index = self.parameter_index(_name)
            parameters = (
                self.parameters[:index] + self.parameters[index + 1:]
            )

            # add parameter
            reverse = reversed(list(enumerate(parameters)))
            generator = (idx + 1 for idx, par in reverse if par.kind <= kind)
            index = next(generator, 0)
            self.parameters = (
                parameters[:index] + (param,) + parameters[index:]
            )

    def reset_parameter(self, name: str) -> None:
        """Replace a parameter with its original definition, if one exists.

        Parameters
        ----------
        name : str
            The name of the parameter to replace.

        Raises
        ------
        KeyError
            If the named argument is not a parameter of this signature.
        TypeError
            If the named parameter does not have an original definition.

        See Also
        --------
        Signature.add_parameter :
            Add a parameter to the signature.
        Signature.set_parameter :
            Modify a parameter in the signature.
        Signature.remove_parameter :
            Remove a parameter from the signature.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig.add_parameter("qux", default=3)
            >>> sig.set_parameter("bar", annotation=int, default=3)
            >>> sig
            <Signature (bar: int = 1, baz=2, qux=3, **kwargs)>
            >>> sig.reset_parameter("bar")
            >>> sig
            <Signature (bar, baz=2, qux=3, **kwargs)>
            >>> sig.reset_parameter("qux")
            Traceback (most recent call last):
                ...
            TypeError: 'qux' does not appear in the original signature
        """
        # get index of named parameter
        index = self.parameter_index(name)

        # check if parameter exists in original signature
        if name not in self.original.parameters:
            raise TypeError(
                f"'{name}' does not appear in the original signature"
            )

        # replace parameter
        self.parameters = (
            self.parameters[:index] +
            (self.original.parameters[name], ) +
            self.parameters[index + 1:]
        )

    def remove_parameter(self, name: str) -> None:
        """Remove a parameter from this signature.

        Parameters
        ----------
        name : str
            The name of the parameter to remove.

        Raises
        ------
        KeyError
            If the named argument is not a parameter of this signature.

        See Also
        --------
        Signature.add_parameter :
            Add a parameter to the signature.
        Signature.set_parameter :
            Modify a parameter in the signature.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig.remove_parameter("bar")
            >>> sig
            <Signature (baz=2, **kwargs)>
            >>> sig.remove_parameter("kwargs")
            >>> sig
            <Signature (baz=2)>
            >>> sig.remove_parameter("baz")
            >>> sig
            <Signature ()>
        """
        # get index of named parameter
        index = self.parameter_index(name)

        # drop parameter
        self.parameters = self.parameters[:index] + self.parameters[index + 1:]

    def parameter_index(self, name: str) -> int:
        """Find the index of a parameter by name.

        Parameters
        ----------
        name : str
            The name of the parameter to find.

        Returns
        -------
        int
            The index of the parameter.

        Raises
        ------
        KeyError
            If the named argument is not a parameter of this signature.

        See Also
        --------
        Signature.move_parameter :
            Move a parameter to a new index.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig.parameter_index("baz")
            1
            >>> sig.parameter_index("kwargs")
            2
        """
        param = self.parameter_map[name]
        return self.parameters.index(param)

    def move_parameter(self, name: str, index: int) -> None:
        """Move a parameter to the specified index.

        Parameters
        ----------
        name : str
            The name of the parameter to move.
        index : int
            The index to move the parameter to.  This supports negative
            indexing just like a normal tuple.

        Raises
        ------
        KeyError
            If the named argument is not contained within the signature.
        IndexError
            If the index is out of bounds.
        ValueError
            If the new parameter ordering is invalid.  This can occur if, for
            instance, a keyword-only parameter is moved before a positional
            one.

        See Also
        --------
        Signature.parameter_index :
            Find the index of a parameter by name.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig
            <Signature (bar, baz, **kwargs)>
            >>> sig.move_parameter("baz", 0)
            >>> sig
            <Signature (baz, bar, **kwargs)>
            >>> sig.move_parameter("bar", -1)
            Traceback (most recent call last):
                ...
            ValueError: wrong parameter order: variadic keyword parameter before positional or keyword parameter
        """
        # treat negative indices as positive
        if not -len(self.parameters) <= index < len(self.parameters):
            raise IndexError("tuple index out of range")
        if index < 0:
            index += len(self.parameters)

        # get index of parameter
        curr_index = self.parameter_index(name)
        param = self.parameter_map[name]

        # remove parameter
        parameters = (
            self.parameters[:curr_index] + self.parameters[curr_index + 1:]
        )

        # insert parameter at specified index
        self.parameters = parameters[:index] + (param,) + parameters[index:]

    def reconstruct(
        self,
        defaults: bool = True,
        annotations: bool = True,
        return_annotation: bool = True,
    ) -> str:
        """Return a complete string representation of the signature.

        Parameters
        ----------
        annotations: bool, default True
            Indicates whether to include type annotations for each parameter in
            the resulting string.
        defaults: bool, default True
            Indicates whether to include default values for each parameter in
            the resulting string.
        return_annotation: bool, default True
            Indicates whether to include the return annotation in the resulting
            string.

        Returns
        -------
        str
            A string listing the function name and all its arguments, with or
            without type annotations/default values.

        Examples
        --------
        .. doctest::

            >>> def foo(bar: int, baz: int = 2, **kwargs) -> dict[str, int]:
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig.reconstruct()
            'foo(bar: int, baz: int = 2, **kwargs) -> dict[str, int]'
            >>> sig.reconstruct(annotations=False)
            'foo(bar, baz=2, **kwargs) -> dict[str, int]'
            >>> sig.reconstruct(annotations=False, return_annotation=False)
            'foo(bar, baz=2, **kwargs)'
            >>> sig.reconstruct(defaults=False, annotations=False, return_annotation=False)
            'foo(bar, baz, **kwargs)'
        """
        # configure replacement kwargs
        kwargs = {}
        if not annotations:
            kwargs["annotation"] = EMPTY
        if not defaults:
            kwargs["default"] = EMPTY

        # replace parameters
        parameters = tuple(par.replace(**kwargs) for par in self.parameters)
        sig = self.signature.replace(parameters=parameters)

        # replace return annotation
        if not return_annotation:
            sig = sig.replace(return_annotation=EMPTY)

        # combine into string
        return f"{self.func_name}{sig}"

    def compatible(self, other: Signature) -> bool:
        """Return ``True`` if the other signature implements all of this
        function's arguments.

        Parameters
        ----------
        other : Signature
            The signature to match against.  This is free to define additional
            parameters beyond those of this signature, but it must implement
            at least these parameters for this method to return ``True``.

        Returns
        -------
        bool
            ``True`` if the other signature this signature's interface.
            ``False`` otherwise.

        Notes
        -----
        The following rules are used to determine compatibility:

            *   The other signature must implement every positional argument of
                this :class:`Signature <pdcast.Signature>` in the same order.
                They can have different annotations and/or default values, but
                they must have the same name and kind.  The other signature can
                also define additional positional arguments beyond those of
                this object, which are not compared.
            *   If this signature accepts variadic positional arguments, the
                other signature must also accept variadic positional arguments
                with the same name.
            *   The other signature must implement every keyword-only argument
                of this :class:`Signature <pdcast.Signature>`.  The order does
                not matter, and they can have different annotations and/or
                default values, but they must have the same name and kind.  The
                other signature can also define additional keyword-only
                arguments beyond those of this object, which are not compared.
            *   If this signature accepts variadic keyword arguments, the other
                signature must also accept variadic keyword arguments with the
                same name.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = Signature(foo)
            >>> sig.compatible(Signature(lambda bar, baz, **kwargs: None))
            True
            >>> sig.compatible(Signature(lambda bar, baz, *, qux=3, corge=4, **kwargs: None))
            True
            >>> sig.compatible(Signature(lambda x, y: None))
            False
        """
        positional = {KINDS["POSITIONAL_ONLY"], KINDS["POSITIONAL_OR_KEYWORD"]}
        keyword = {KINDS["KEYWORD_ONLY"], KINDS["VAR_KEYWORD"]}

        def compatible(par, other_par):
            return par.name == other_par.name and par.kind == other_par.kind

        # iterate through this signature's parameters
        idx = 0
        for par in self.parameters:
            # get corresponding parameter from other signature
            try:
                other_par = other.parameters[idx]
            except IndexError:
                return False

            # check all positional arguments match exactly
            if par.kind in positional and not compatible(par, other_par):
                return False

            # if signature accepts *args, skip remaining positional args
            elif par.kind == KINDS["VAR_POSITIONAL"]:
                while other_par.kind in positional:
                    idx += 1
                    try:
                        other_par = other.parameters[idx]
                    except IndexError:
                        return False

                # check next parameter matches exactly
                if not compatible(par, other_par):
                    return False

            # check keyword-only arguments are present (no specific order)
            elif par.kind in keyword:
                equivalent = other.parameter_map.get(par.name, None)
                if equivalent is None or not compatible(par, equivalent):
                    return False

            idx += 1

        return True

    def __call__(self, *args, **kwargs) -> Arguments:
        """Bind the arguments to the :class:`Signature <pdcast.Signature>`,
        returning a corresponding :class:`Arguments <pdcast.Arguments>` object.

        Parameters
        ----------
        *args, **kwargs
            Arbitrary positional and keyword arguments to bind to this
            signature.

        Returns
        -------
        Arguments
            A wrapper around a
            :class:`BoundArguments <python:inspect.BoundArguments>` object with
            additional context for decorator-related functionality.

        Notes
        -----
        The default implementation of this method is equivalent to a
        :meth:`bind_partial <python:inspect.Signature.bind_partial>` call on
        the underlying :class:`inspect.Signature <python:inspect..Signature>`
        object.
        
        Subclasses can override this method to provide additional functionality.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = pdcast.Signature(foo)
            >>> bound = sig(1)
            >>> bound
            <BoundArguments (bar=1)>
            >>> bound.apply_defaults()
            >>> bound
            <BoundArguments (bar=1, baz=2, kwargs={})>
            >>> bound.args, bound.kwargs
            ((1, 2), mappingproxy({}))
        """
        bound = self.signature.bind_partial(*args, **kwargs)
        return Arguments(bound=bound, signature=self)

    def __str__(self) -> str:
        return str(self.signature)

    def __repr__(self) -> str:
        return repr(self.signature)


class Arguments:
    """An extensible wrapper around a
    :class:`BoundArguments <python:inspect.BoundArguments>` object with extra
    context for decorator-related functionality.

    Parameters
    ----------
    bound : inspect.BoundArguments
        The :class:`BoundArguments <python:inspect.BoundArguments>` to wrap.
    signature : Signature
        A reference to the :class:`Signature <pdcast.Signature>` that spawned
        this object.

    Notes
    -----
    These are created by calling a :class:`Signature <pdcast.Signature>` using
    its :meth:`__call__() <pdcast.Signature.__call__>` method.

    Subclasses can add additional attributes to simplify decorator-specific
    operations.
    """

    def __init__(
        self,
        bound: inspect.BoundArguments,
        signature: Signature
    ):
        self.bound = bound
        self.signature = signature

    @property
    def arguments(self) -> dict[str, Any]:
        """A mutable mapping containing the current value of each argument.

        Returns
        -------
        dict[str, Any]
            An ordinary dictionary listing the bound value of each argument.
            Any changes that are made to this dictionary will be propagated
            to :attr:`args <pdcast.Arguments.args>` and
            :attr:`kwargs <pdcast.Arguments.kwargs>`.

        See Also
        --------
        Arguments.args :
            An ``*args`` tuple that can be used to invoke the original
            function.
        Arguments.kwargs :
            A ``**kwargs`` map that can be used to invoke the original
            function.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = pdcast.Signature(foo)
            >>> bound = sig(1)
            >>> bound.arguments
            {'bar': 1}
            >>> bound.apply_defaults()
            >>> bound.arguments
            {'bar': 1, 'baz': 2, 'kwargs': {}}
            >>> foo(*bound.args, **bound.kwargs)
            {'bar': 1, 'baz': 2}
            >>> bound.arguments["baz"] = 3
            >>> foo(*bound.args, **bound.kwargs)
            {'bar': 1, 'baz': 3}
        """
        return self.bound.arguments

    @property
    def args(self) -> tuple[Any, ...]:
        """Positional arguments that can be supplied to the original function.

        Returns
        -------
        tuple[Any, ...]
            An ``*args`` tuple that can be used to invoke the original
            function.

        See Also
        --------
        Arguments.arguments :
            A mutable dictionary containing the current value of each argument.

        Arguments.kwargs :
            A ``**kwargs`` map that can be used to invoke the original
            function.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = pdcast.Signature(foo)
            >>> bound = sig(1)
            >>> bound.args, bound.kwargs
            ((1,), mappingproxy({}))
            >>> bound.apply_defaults()
            >>> bound.args, bound.kwargs
            ((1, 2), mappingproxy({}))
        """
        return self.bound.args

    @property
    def kwargs(self) -> Mapping[str, Any]:
        """Keyword arguments that can be supplied to the original function.

        Returns
        -------
        Mapping[str, Any]
            A read-only ``**kwargs`` dictionary that can be used to invoke the
            original function.

        See Also
        --------
        Arguments.arguments :
            A mutable dictionary containing the current value of each argument.
        Arguments.args :
            An ``*args`` tuple that can be used to invoked the original
            function.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = pdcast.Signature(foo)
            >>> sig.add_parameter("qux", default=4)
            >>> sig
            <Signature (bar, baz=2, qux=4, **kwargs)>
            >>> bound = sig(1)
            >>> bound.args, bound.kwargs
            ((1,), mappingproxy({}))
            >>> bound.apply_defaults()
            >>> bound.args, bound.kwargs  # TODO: incorrect - probably need to do some custom shenanigans
            ((1, 2), mappingproxy({'qux': 4})
        """
        return MappingProxyType(self.bound.kwargs)

    def apply_defaults(self) -> None:
        """Apply the original signature's default values to the bound
        arguments.

        Examples
        --------
        .. doctest::

            >>> def foo(bar, baz=2, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> sig = pdcast.Signature(foo)
            >>> bound = sig(1)
            >>> bound
            <BoundArguments (bar=1)>
            >>> bound.apply_defaults()
            >>> bound
            <BoundArguments (bar=1, baz=2, kwargs={})>
        """
        return self.bound.apply_defaults()

    def __str__(self) -> str:
        return str(self.bound)

    def __repr__(self) -> str:
        return repr(self.bound)
