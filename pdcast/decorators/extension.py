"""This module describes an ``@extension_func`` decorator that transforms an
ordinary Python function into one that can accept dynamic, managed arguments
with custom validators and default values.
"""
from __future__ import annotations
from functools import wraps
import inspect
import threading
from types import MappingProxyType
from typing import Any, Callable

from .base import FunctionDecorator, no_default


######################
####    PUBLIC    ####
######################


def extension_func(func: Callable) -> Callable:
    """A decorator that allows a function to accept managed arguments with
    dynamic values.

    Parameters
    ----------
    func : Callable
        A Python function or other callable to be decorated.  If this accepts a
        ``**kwargs`` dict or similar variable-length keyword argument, then it
        will be allowed to take on
        :ref:`extension arguments <extension_func.extension>` defined at run
        time.

    Returns
    -------
    ExtensionFunc
        A :ref:`cooperative <attachable.nested>` decorator that allows
        transparent access to the decorated function.  These objects manage
        default values and argument validators for the decorated callable.

    Notes
    -----
    The returned :class:`ExtensionFunc <pdcast.ExtensionFunc>` inherits from
    :class:`threading.local <python:threading.local>`, which isolates its
    default values to the current thread.  Each instance inherits the default
    values of the main thread during at the time it is constructed.
    """
    main_thread = []  # using a one-element list bypasses UnboundLocalError

    class _ExtensionFunc(ExtensionFunc):
        """A subclass of :class:`ExtensionFunc <pdcast.ExtensionFunc>` that
        supports dynamic assignment of ``@properties`` without affecting other
        instances.
        """

        def __init__(self, _func: Callable):
            super().__init__(_func)

            # store attributes from main thread
            if threading.current_thread() == threading.main_thread():
                main_thread.append({
                    "class": type(self),
                    "signature": self._signature,
                })

            # load attributes from main thread
            else:
                main = main_thread[0]
                self._signature.copy_settings(main["signature"])
                for k in main["signature"].validators:
                    setattr(type(self), k, getattr(main["class"], k))

    return _ExtensionFunc(func)


#######################
####    PRIVATE    ####
#######################


class ExtensionFunc(FunctionDecorator, threading.local):
    """A wrapper for the decorated callable that can be dynamically extended
    with custom arguments.

    Parameters
    ----------
    func : Callable
        The decorated function or other callable.

    Notes
    -----
    Whenever an argument is :meth:`registered <ExtensionFunc.argument>` with
    this function, it is added as a managed :class:`property <python:property>`
    with appropriate getter, setter, and deleter methods.  These are
    automatically derived from the validation function itself, and can be used
    to manage its default value externally, without touching any hard code.

    Additionally, this object inherits from :class:`threading.local`, which
    means that each instance is isolated to the current thread.  If the
    function is referenced from a child thread, a new instance will be created
    with the arguments and settings of the main thread.  This instance can then
    be modified without affecting the behavior of any other threads.

    Examples
    --------
    See the :ref:`API docs <extension_func>` for example usage.
    """

    _reserved = FunctionDecorator._reserved | {"_signature"}

    def __init__(self, func: Callable):
        super().__init__(func=func)
        self._signature = ExtensionSignature(func)

    ####################
    ####    BASE    ####
    ####################

    # NOTE: @properties will be added to this class by the @argument decorator

    @property
    def arguments(self) -> MappingProxyType:
        """A mapping of all managed arguments to their respective validators.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping argument names to their associated
            validation functions.

        Examples
        --------
        .. doctest::

            >>> @extension_func
            ... def foo(bar, baz, **kwargs):
            ...     return bar, baz

            >>> @foo.argument
            ... def bar(val, context: dict) -> int:
            ...     return int(val)

            >>> foo.arguments   # doctest: +SKIP
            mappingproxy({'bar': <function bar at 0x7ff5ad9c6e60>})
        """
        return MappingProxyType(self._signature.validators)

    @property
    def settings(self) -> MappingProxyType:
        """A mapping of all managed arguments to their current settings.

        Returns
        -------
        MappingProxyType
            A read-only dictionary suitable for use as the ``**kwargs`` input
            to the decorated function.

        Notes
        -----
        If no default value is associated with an argument, then it will be
        excluded from this dictionary.
        """
        return self._signature.settings

    def argument(
        self,
        func: Callable = None,
        *,
        name: str | None = None,
        default: Any = no_default
    ) -> Callable:
        """A decorator that transforms a validation function into a managed
        argument for this :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

        Parameters
        ----------
        name : str | None, default None
            The name of the argument that the validator validates.  If this is
            left as :data:`None <python:None>`, then the name of the validator
            will be used instead.
        default : Any | None, default no_default
            The default value to use for this argument.  This is implicitly
            passed to the validator itself, so any custom logic that is
            implemented there will also be applied to this value.  If this
            argument is omitted and the decorated function defines a default
            value in its call signature, then that value will be used instead.

        Returns
        -------
        Callable
            A decorated version of the validation function that automatically
            fills out its second positional argument (typically a ``context``
            dict) with a table containing the current value of each argument
            at the time the function is invoked.  This validator will
            implicitly be called whenever the
            :class:`ExtensionFunc <pdcast.ExtensionFunc>` is executed with the
            named argument as input.

        Raises
        ------
        TypeError
            If the decorated object is not callable or its ``name`` conflicts
            with a built-in attribute of the
            :class:`ExtensionFunc <pdcast.ExtensionFunc>` or one of its
            derivatives.  This can also be raised if the function does not
            implement a ``**kwargs`` argument and ``name`` is absent from its
            argument list.
        KeyError
            If a managed property of the same name already exists.

        Notes
        -----
        Validation function must accept at least two positional arguments as
        shown:

        .. code:: python

            def validator(val, context: dict):
                ...

        Where ``val`` is an arbitrary input to the argument and ``context`` is
        a dictionary containing the current values for each argument at the
        time the :class:`ExtensionFunc <pdcast.ExtensionFunc>` was invoked.  If
        the validator interacts with other arguments (to enforce mutual
        exclusivity, for example), then they can be obtained from the
        ``context`` dictionary.

        .. note::

            Race conditions may be introduced when arguments access each other
            in their validators.  This can be mitigated somewhat by using safe
            access methods like :meth:`dict.get() <python:dict.get>` rather
            than direct indexing, as well as by avoiding modifications to the
            ``context`` dictionary at runtime.

            Additionally, the values that are obtained from the ``context``
            dictionary are not guaranteed to have been validated prior to
            accessing them.  If you need to ensure that a value is valid before
            using it, then you can do so by manually applying the same
            coercions as in the referenced argument's validation function.

        Examples
        --------
        See the :ref:`API docs <extension_func.arguments>` for example usage.
        """

        def decorator(validator: Callable) -> Callable:
            """Attach a validation function to the ExtensionFunc as a managed
            property.
            """
            # Ensure validator is valid
            _name = self._signature.check_validator(validator, name)

            # check name is not overwriting a reserved attribute
            if _name in dir(self):
                raise TypeError(f"'{_name}' is a reserved attribute")

            # wrap validator to accept argument context
            @wraps(validator)
            def validate_context(val, context=self.settings, **kwargs):
                """Automatically populate `context` argument of validator."""
                return validator(val, context, **kwargs)

            # get default value and pass through validator
            defaults = self._signature.defaults
            if default is not no_default:
                defaults[_name] = validate_context(default)
            else:
                pars = self._signature.parameters
                empty = inspect.Parameter.empty
                if _name in pars and pars[_name].default is not empty:
                    defaults[_name] = validate_context(pars[_name].default)

            # generate getter, setter, and deleter for @property
            def getter(self) -> Any:
                """Get the value of a managed argument."""
                overrides = self._signature.overrides
                defaults = self._signature.defaults
                if _name in overrides:
                    return overrides[_name]
                if _name in defaults:
                    return defaults[_name]
                raise TypeError(f"'{_name}' has no default value")

            def setter(self, val: Any) -> None:
                """Set the value of a managed argument."""
                self._signature.overrides[_name] = validate_context(val)

            def deleter(self) -> None:
                """Replace the value of a managed argument with its default."""
                defaults = self._signature.defaults
                if _name in defaults:
                    validate_context(defaults[_name])
                self._signature.overrides.pop(_name, None)

            # attach property to class
            prop = property(getter, setter, deleter, doc=validator.__doc__)
            setattr(type(self), _name, prop)

            # remember validator
            self._signature.validators[_name] = validate_context
            return validate_context

        if func is None:
            return decorator
        return decorator(func)

    def remove_arg(self, name: str) -> None:
        """Remove a managed argument from an
        :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

        Parameters
        ----------
        name : str
            The name of the argument to remove.  This must be contained in the
            :attr:`arguments <pdcast.ExtensionFunc.arguments>` attribute of
            the :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

        Raises
        ------
        KeyError
            If the argument is not being actively managed by this
            :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

        Examples
        --------
        .. doctest::

            >>> @extension_func
            ... def foo(bar, baz, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> @foo.argument(default=1)
            ... def bar(val, context: dict) -> int:
            ...     return int(val)

            >>> foo.bar
            1
            >>> foo.remove_arg("bar")
            >>> foo.bar
            Traceback (most recent call last):
                ...
            AttributeError: 'function' object has no attribute 'bar'.
        """
        # ensure argument is being actively managed
        if name not in self._signature.validators:
            raise KeyError(f"'{name}' is not a managed argument")

        # commit to deletion
        delattr(type(self), name)
        self._signature.validators.pop(name)
        self._signature.defaults.pop(name, None)
        self._signature.overrides.pop(name, None)

    def reset_defaults(self) -> None:
        """Reset all arguments to their hardcoded defaults.

        Examples
        --------
        .. doctest::

            >>> @extension_func
            ... def foo(bar, baz, **kwargs):
            ...     return bar, baz

            >>> @foo.argument(default=1)
            ... def bar(val, context: dict) -> int:
            ...     return int(val)

            >>> @foo.argument(default=2)
            ... def baz(val, context: dict) -> int:
            ...     return int(val)

            >>> foo.bar, foo.baz
            (1, 2)
            >>> foo.bar, foo.baz = 18, -34
            >>> foo.bar, foo.baz
            (12, -34)
            >>> foo.reset_defaults()
            >>> foo.bar, foo.baz
            (1, 2)
        """
        self._signature.overrides.clear()

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __call__(self, *args, **kwargs) -> Any:
        """Execute the decorated function with the managed arguments.

        Parameters
        ----------
        *args, **kwargs
            Arbitrary positional and keyword arguments that were passed to this
            function.  These will always be redirected through their respective
            validation function before being passed on to the decorated
            callable.

        Returns
        -------
        Any
            The return value of the decorated callable with the validated
            arguments.

        Notes
        -----
        Validators are only called for the arguments that are explicitly passed
        to this method.  If an argument is not passed, then its default value
        will be used instead, and the validator will not be invoked.

        Examples
        --------
        See the :ref:`API docs <extension_func.arguments>` for example usage.
        """
        # generate bound arguments
        bound = self._signature(*args, **kwargs)

        # flatten extension arguments
        bound.flatten_kwargs()

        # pass through validator
        bound.validate()

        # fill in pre-validated settings
        bound.apply_settings()

        # invoke wrapped function
        return self.__wrapped__(*bound.args, **bound.kwargs)

    def __repr__(self) -> str:
        """Reconstruct the function's effective signature using the current
        value of each argument.

        Returns
        -------
        str
            A string describing the state of the function's arguments,
            including their current settings and any
            :ref:`extensions <extension_func.extension>` that have been added.

        Examples
        --------
        .. doctest::

            >>> @extension_func
            ... def foo(bar, baz, **kwargs):
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> @foo.argument(default=1)
            ... def bar(val, context: dict) -> int:
            ...     return int(val)

            >>> @foo.argument(default=2)
            ... def baz(val, context: dict) -> int:
            ...     return int(val)

            >>> @foo.argument(default=3)
            ... def qux(val, context: dict) -> int:
            ...     return int(val)

            >>> foo
            foo(bar=1, baz=2, qux=3, **kwargs)
            >>> repr(foo)
            'foo(bar=1, baz=2, qux=3, **kwargs)'
        """
        return self._signature.reconstruct()


class ExtensionSignature:
    """A wrapper around an :class:`inspect.Signature <python:inspect.Signature>`
    object that serves as a factory for
    :class:`ExtensionArguments <pdcast.ExtensionArguments>`.

    Parameters
    ----------
    func : Callable
        The function whose signature will be wrapped.

    Notes
    -----
    These capture the arguments that are passed to an
    :class:`ExtensionFunc <pdcast.ExtensionFunc>`'s
    :meth:`__call__() <pdcast.ExtensionFunc.__call__>` method, encapsulating them
    in a dedicated :class:`ExtensionArguments <pdcast.ExtensionArguments>`
    class.

    :class:`ExtensionArguments <pdcast.ExtensionArguments>` provide a
    high-level interface for interacting with and validating arguments, without
    having to worry about the complicated machinery of the
    :mod:`inspect <python:inspect>` module.
    """

    def __init__(self, func: Callable):
        self.func_name = func.__qualname__
        self.signature = inspect.signature(func)

        # get name of **kwargs argument
        self.kwargs_name = None
        for par in self.parameters.values():
            if par.kind == par.VAR_KEYWORD:
                self.kwargs_name = par.name
                break

        # init validators, default values
        self.validators = {}
        self.defaults = {}
        self.overrides = {}

    @property
    def parameters(self) -> MappingProxyType:
        """Get the hardcoded parameters of the decorated function.

        Returns
        -------
        MappingProxyType
            An ordered, read-only mapping of the decorated function's
            parameters, as they were defined in its original signature.

        Notes
        -----
        This is equivalent to the output of
        :attr:`inspect.Signature.parameters <python:inspect.Signature.parameters>`.
        """
        return self.signature.parameters

    @property
    def settings(self) -> MappingProxyType:
        """A mapping of all managed arguments to their current defaults.

        Returns
        -------
        MappingProxyType
            A read-only dictionary suitable for use as the ``**kwargs`` input
            to the decorated function.  If no default value is associated with
            an argument, then it will be excluded from this dictionary.
        """
        return MappingProxyType({**self.defaults, **self.overrides})

    def check_validator(
        self,
        validator: Callable,
        name: str | None = None
    ) -> str:
        """Ensure that an argument validator is of the expected form.

        Parameters
        ----------
        validator : Callable
            A function decorated with
            :meth:`ExtensionFunc.argument() <pdcast.ExtensionFunc.argument>`.
        name : str | None
            The name of the argument that ``validator`` validates.  If this is
            left as :data:`None <python:None>`, then the name of the validator
            itself will be used instead.

        Returns
        -------
        str
            The final name of the validated argument.

        Raises
        ------
        TypeError
            If ``validator`` is not callable with at least 2 arguments, or if
            ``name`` is not a string contained within the parameters of this
            :class:`ExtensionSignature`.
        KeyError
            If ``name`` is already in use by another validator.
        """
        # ensure validator is callable
        if not callable(validator):
            raise TypeError(f"validator must be callable: {validator}")

        # ensure validator accepts at least 2 arguments
        if len(inspect.signature(validator).parameters) < 2:
            raise TypeError(
                f"validator must accept at least 2 arguments: {validator}"
            )

        # use name of validator as argument name if not explicitly given
        if name is None:
            name = validator.__name__
        elif not isinstance(name, str):
            raise TypeError(f"name must be a string, not {type(name)}")

        # ensure name is not already in use
        if name in self.validators:
            raise KeyError(f"default argument '{name}' already exists")

        # check name exists in signature or **kwargs
        if self.kwargs_name is None and name not in self.parameters:
            raise TypeError(
                f"'{self.__qualname__}()' has no argument '{name}'"
            )

        # return final argument name
        return name

    def copy_settings(self, other: ExtensionSignature) -> None:
        """Copy the current settings of another
        :class:`ExtensionSignature <pdcast.ExtensionSignature>`.

        Parameters
        ----------
        ExtensionSignature
            The signature whose settings will be copied.

        Notes
        -----
        This is used to copy settings from the main thread's
        :class:`ExtensionFunc <pdcast.ExtensionFunc>` to its children when a
        new thread is spawned.
        """
        self.validators = other.validators.copy()
        self.defaults = other.defaults.copy()
        self.overrides = other.overrides.copy()

    def reconstruct(self) -> str:
        """Reconstruct the function's signature using the managed arguments.

        Returns
        -------
        str
            A string representation of the function's signature using the
            current values of all its managed arguments.
        """
        return f"{self.func_name}({', '.join(self().reconstruct())})"

    def __call__(self, *args, **kwargs) -> ExtensionArguments:
        """Bind the arguments to form a new
        :class:`ExtensionArguments <pdcast.ExtensionArguments>` object.

        Parameters
        ----------
        *args, **kwargs
            Arbitrary positional and keyword arguments to be bound to this
            signature.

        Returns
        -------
        ExtensionArguments
            A new :class:`ExtensionArguments <pdcast.ExtensionArguments>`
            object that encapsulates the bound arguments.

        Notes
        -----
        This is always called on the input to
        :meth:`ExtensionFunc.__call__() <pdcast.ExtensionFunc.__call__>`.
        """
        bound = self.signature.bind_partial(*args, **kwargs)
        parameters = tuple(self.parameters.values())

        return ExtensionArguments(
            arguments=bound.arguments,
            parameters=parameters,
            positional=parameters[:len(args)],
            kwargs_name=self.kwargs_name,
            validators=self.validators,
            settings=self.settings
        )


class ExtensionArguments:
    """Captured arguments from a call to `ExtensionFunc(*args, **kwargs)`.

    Parameters
    ----------
    arguments : dict[str, Any]
        A dictionary of all the arguments that were passed to an
        `ExtensionFunc`.  This is equivalent to the `arguments` attribute of an
        `inspect.BoundArguments` object.
    parameters : tuple[str]
        All the parameter names from the function's original signature, in
        order.
    positional : tuple[str]
        The names of all the positional arguments that were passed to the
        `ExtensionFunc`.  This is just the first `n` elements of `parameters`,
        where `n` is the number of `*args` that were supplied.
    kwargs_name : str
        The name of the `**kwargs` argument in the function's original
        signature, or `None` if no such argument exists.
    validators : dict[str, Callable]
        A reference to the `validators` table of the function's
        `ExtensionSignature`.
    settings : MappingProxyType
        A reference to the `settings` table of the function's
        `ExtensionSignature`.  This combines its `defaults` and `overrides`
        into a single read-only dictionary.

    Notes
    -----
    Whenever an `ExtensionFunc` is called, its arguments are intercepted and
    encapsulated into an `ExtensionArguments` object.  These behave similarly
    to `inspect.BoundArguments`, but with additional operations for validating
    argument values and applying dynamic defaults.
    """

    def __init__(
        self,
        arguments: dict[str, Any],
        parameters: tuple[inspect.Parameter],
        positional: tuple[inspect.Parameter],
        kwargs_name: str,
        validators: dict[str, Callable],
        settings: MappingProxyType
    ):
        self.arguments = arguments
        self.parameters = parameters
        self.positional = positional
        self.kwargs_name = kwargs_name
        self.validators = validators
        self.settings = settings

    def flatten_kwargs(self) -> None:
        """Flatten a `**kwargs` argument into the main argument table if it is
        present.
        """
        if self.kwargs_name in self.arguments:
            self.arguments.update(self.arguments[self.kwargs_name])
            del self.arguments[self.kwargs_name]

    def validate(self) -> None:
        """Pass each argument through its associated validator.

        Notes
        -----
        :meth:`flatten_kwargs() <pdcast.ExtensionArguments.flatten_kwargs>`
        should always be called before this method to ensure that all arguments
        are caught by their respective validators.
        """
        for name, value in self.arguments.items():
            if name in self.validators:
                self.arguments[name] = self.validators[name](value)

    def apply_settings(self) -> None:
        """Fill in the current settings for any arguments that are missing from
        the main argument table.

        Notes
        -----
        These settings are pre-validated, so this method should always be
        called after :meth:`validate() <pdcast.ExtensionArguments.validate>`.
        """
        self.arguments = {**self.settings, **self.arguments}

    def reconstruct(self) -> list[str]:
        """Reconstruct a function's effective signature using the current
        settings and extension arguments.

        Returns
        -------
        list
            A list of strings that can be joined with commas to produce the
            arguments portion of an `ExtensionFunc`'s signature string.

        Notes
        -----
        This is used to display the function's signature in
        :func:`repr() <python:repr>` calls.
        """
        # apply current settings
        self.apply_settings()

        # flatten extension arguments
        self.flatten_kwargs()

        # stringify args according to original signature
        strings = []
        for par in self.parameters:

            # display managed value
            if par.name in self.arguments:
                strings.append(f"{par.name}={repr(self.arguments[par.name])}")

            # display extension arguments
            elif par.name == self.kwargs_name:
                hardcoded = {p.name for p in self.parameters}
                kwargs = {
                    k: v for k, v in self.arguments.items()
                    if k not in hardcoded
                }
                strings.extend(f"{k}={repr(v)}" for k, v in kwargs.items())
                strings.append(f"**{self.kwargs_name}")

            # display original
            else:
                if par.default is not inspect.Parameter.empty:
                    strings.append(f"{par.name}={par.default}")
                else:
                    strings.append(par.name)

        return strings

    @property
    def args(self) -> tuple[Any, ...]:
        """Positional arguments to be supplied to the wrapped function.

        Notes
        -----
        The output from this property can be used as an ``*args`` input to the
        decorated function.
        """
        return tuple(self.arguments[p.name] for p in self.positional)

    @property
    def kwargs(self) -> dict[str, Any]:
        """Keyword arguments to be supplied to the wrapped function.

        Notes
        -----
        The output from this property can be used as a ``**kwargs`` input to
        the decorated function.
        """
        pos = {p.name for p in self.positional}
        return {k: v for k, v in self.arguments.items() if k not in pos}
