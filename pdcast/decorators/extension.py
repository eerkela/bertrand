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
    """A decorator that allows a function to accept managed arguments and
    default values.

    Parameters
    ----------
    func : Callable
        A Python function or other callable to be decorated.  If this accepts a
        ``**kwargs`` dict or similar variable-length keyword argument, then it
        will be allowed to take on
        :ref:`dynamic arguments <extension_func.extension>` defined at run
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
    values of the main thread at the time it is constructed.
    """
    main_thread = []  # using a list bypasses UnboundLocalError

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
    Whenever an argument is :meth:`registered <ExtensionFunc.argument>` to this
    object, it is added as a managed :class:`property <python:property>`.  This
    automatically generates an appropriate getter, setter, and deleter for the
    attribute based on the decorated validation function.

    Additionally, this object inherits from :class:`threading.local`.  If the
    decorated function is referenced in a child thread, a new instance will be
    dynamically created with arguments and defaults from the main thread.  This
    instance can then be modified without affecting the behavior of any other
    threads.

    Examples
    --------
    See the docs for :func:`@extension_func <pdcast.extension_func>` for
    example usage.
    """

    _reserved = FunctionDecorator._reserved | {"_signature"}

    def __init__(self, func: Callable):
        super().__init__(func=func)
        self._signature = ExtensionSignature(func)

    ####################
    ####    BASE    ####
    ####################

    # NOTE: @properties will be added to this class by the @argument decorator

    def argument(
        self,
        func: Callable = None,
        *,
        name: str | None = None,
        default: Any = no_default
    ) -> Callable:
        """A decorator that transforms a naked validation function into a
        managed argument for this
        :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

        Parameters
        ----------
        name : str | None
            The name of the argument that the validator validates.  If this is
            left as :data:`None <python:None>` (the default), then the name of
            the validator will be used instead.
        default : Any | None
            The default value to use for this argument.  This is implicitly
            passed to the validator itself, so any custom logic that is
            implemented there will also be applied to this value.  If this
            argument is omitted and the decorated function defines a default
            value in its call signature, then that value will be used instead.

        Returns
        -------
        Callable
            A decorated version of the validation function that automatically
            fills out its ``values`` argument.  This function will
            implicitly be called whenever the
            :class:`ExtensionFunc <pdcast.ExtensionFunc>` is executed.

        Raises
        ------
        TypeError
            If the decorator is applied to a non-function argument, or if the
            argument does not appear in the signature of the
            :class:`ExtensionFunc <pdcast.ExtensionFunc>` (or its
            ``**kwargs``).
        KeyError
            If a managed property of the same name already exists.

        Notes
        -----
        A validation function must have the following signature:

        .. code:: python

            def validator(val, state):
                ...

        Where ``val`` is an arbitrary input to the argument and ``state`` is a
        dictionary containing the current values for each argument.  If the
        validator interacts with other arguments (via mutual exclusivity, for
        instance), then they can be obtained from the ``state`` dictionary.

        .. note::

            Race conditions may be introduced when arguments access each other
            in their validators.  This can be mitigated by using
            :meth:`dict.get() <python:dict.get>` with a default value rather
            than relying on direct access, as well as manually applying the
            same coercions as in the referenced argument's validation function.

        Examples
        --------
        See the :func:`extension_func`
        :ref:`API docs <extension_func.validator>` for example usage.
        """

        def decorator(validator: Callable) -> Callable:
            """Attach a validation function to the ExtensionFunc as a managed
            property.
            """
            # Ensure validator is valid
            _name = self._signature.check_validator(name, validator)

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
            ... def bar(val: int, state: dict) -> int:
            ...     return int(val)

            >>> foo.arguments   # doctest: +SKIP
            mappingproxy({'bar': <function bar at 0x7ff5ad9c6e60>})
        """
        return MappingProxyType(self._signature.validators)

    @property
    def settings(self) -> MappingProxyType:
        """A mapping of all managed arguments to their default values.

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

    def remove_arg(self, name: str) -> None:
        """Remove a managed argument from an
        :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

        Parameters
        ----------
        name : str
            The name of the argument to remove.

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
            ... def bar(val: int, state: dict) -> int:
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
        """Reset all arguments to their default value.

        Examples
        --------
        .. doctest::

            >>> @extension_func
            ... def foo(bar, baz, **kwargs):
            ...     return bar, baz

            >>> @foo.argument(default=1)
            ... def bar(val: int, state: dict) -> int:
            ...     return int(val)

            >>> @foo.argument(default=2)
            ... def baz(val: int, state: dict) -> int:
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

    def __call__(self, *args, **kwargs):
        """Execute the decorated function with the default arguments stored in
        this ExtensionFunc.

        This also validates the input to each argument using the attached
        validators.
        """
        # generate bound arguments
        bound = self._signature(*args, **kwargs)

        # flatten extension arguments
        bound.flatten_kwargs()

        # pass through validator
        bound.validate()

        # apply current settings (pre-validated)
        bound.apply_settings()

        # invoke wrapped function
        return self.__wrapped__(*bound.args, **bound.kwargs)

    def __contains__(self, item: str | Callable) -> bool:
        """Check if the named argument is being managed by this ExtensionFunc.

        This can accept either the name of the argument or the validator
        itself.
        """
        return (
            item in self._signature.validators or
            item in self._signature.validators.values()
        )

    def __iter__(self):
        """Iterate through the arguments that are being managed by this
        ExtensionFunc.
        """
        return iter(self._signature.validators)

    def __len__(self) -> int:
        """Return the total number of arguments that are being managed by this
        ExtensionFunc
        """
        return len(self._signature.validators)

    def __repr__(self) -> str:
        """Return a string representation of the decorated function with a
        reconstructed signature incorporating the default values stored in this
        ExtensionFunc.
        """
        args = self._signature().reconstruct()
        return f"{self.__wrapped__.__qualname__}({', '.join(args)})"


class ExtensionSignature:
    """A wrapper around an `inspect.Signature` object that creates
    `ExtensionArguments`.
    """

    def __init__(self, func: Callable):
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
        """Get the hardcoded parameters of the decorated function."""
        return self.signature.parameters

    @property
    def settings(self) -> MappingProxyType:
        """A mapping of all managed arguments to their default values.

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
        return MappingProxyType({**self.defaults, **self.overrides})

    def check_validator(self, name: str, validator: Callable) -> str:
        """Ensure that an argument validator is of the expected form.

        Parameters
        ----------
        validator : Callable
            A function decorated with :meth:`ExtensionFunc.argument`.
        name : str, optional
            The name of the argument that ``validator`` validates.  If this is
            not given, then the name of ``validator`` will be used directly.

        Returns
        -------
        str
            The final name of the argument that ``validator`` validates.

        Raises
        ------
        TypeError
            If ``validator`` is not callable with at least 2 arguments, or if
            ``name`` is not a string contained within the parameters of this
            :class:`ExtensionSignature`.
        KeyError
            If ``name`` is already in use by another validator.

        Notes
        -----
        The following conditions must be met for this method to return
        normally:

            *   ``validator`` must be callable with at least 2 arguments.  The
                first will be the value of the argument being validated, and
                the second will be a dictionary of all the other arguments
                passed to :meth:`ExtensionFunc.__call__`.
            *   ``name`` must be the name of a unique argument in the original
                function's signature or its ``**kwargs``.
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
        """Copy default values and validators from another `ExtensionSignature`.

        This is used to copy settings from the main thread's `ExtensionFunc`
        to its children when a new thread is spawned.
        """
        self.validators = other.validators.copy()
        self.defaults = other.defaults.copy()
        self.overrides = other.overrides.copy()

    def __call__(self, *args, **kwargs) -> ExtensionArguments:
        """Create an `ExtensionArguments` from *args, **kwargs"""
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
        """Flatten the `**kwargs` argument into the main argument table if it
        is present.
        """
        if self.kwargs_name in self.arguments:
            self.arguments.update(self.arguments[self.kwargs_name])
            del self.arguments[self.kwargs_name]

    def validate(self) -> None:
        """Pass each argument through its associated validator.

        `flatten_kwargs` should always be called before this method to ensure
        that all arguments are caught by the validators.
        """
        for name, value in self.arguments.items():
            if name in self.validators:
                self.arguments[name] = self.validators[name](value)

    def apply_settings(self) -> None:
        """Fill in the current settings for any arguments that are missing from
        the main argument table.

        These settings are pre-validated, so this method should always be
        called after `validate`.
        """
        self.arguments = {**self.settings, **self.arguments}

    def reconstruct(self) -> list[str]:
        """Reconstruct a function's effective signature using the current
        settings and extension arguments.

        This is used to display the function's signature in `repr()` calls.

        Returns
        -------
        list
            A list of strings that can be joined with commas to produce the
            arguments portion of an `ExtensionFunc`'s signature string.
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
        """Positional arguments to be supplied to the wrapped function."""
        return tuple(self.arguments[p.name] for p in self.positional)

    @property
    def kwargs(self) -> dict[str, Any]:
        """Keyword arguments to be supplied to the wrapped function."""
        pos = {p.name for p in self.positional}
        return {k: v for k, v in self.arguments.items() if k not in pos}
