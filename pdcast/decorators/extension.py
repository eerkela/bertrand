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


# TODO: add a method to allow an ExtensionSignature to inherit values from
# main thread.
# -> this has to be a standalone function, since namespace is precious
# on ExtensionFunc.


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
            # TODO: update_wrapper(self, _func)?

            # store attributes from main thread
            if threading.current_thread() == threading.main_thread():
                main_thread.append({
                    "class": type(self),
                    "_signature": self._signature,
                })

            # copy attributes from main thread
            else:
                main = main_thread[0]
                self._signature.copy_from(main["_signature"])

                for k in main["_validators"]:
                    setattr(type(self), k, getattr(main["class"], k))
                self._vals = main["_vals"].copy()
                self._defaults = main["_defaults"].copy()
                self._validators = main["_validators"].copy()

    return _ExtensionFunc(func)


#######################
####    PRIVATE    ####
#######################


class ExtensionFunc(FunctionDecorator, threading.local):
    """A wrapper for the decorated callable that can be dynamically extended
    with custom arguments.

    Parameters
    ----------
    _func : Callable
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
    def default_values(self) -> MappingProxyType:
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
        return MappingProxyType({
            **self._signature.defaults,
            **self._signature.settings
        })

    def argument(
        self,
        _func: Callable = None,
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

        def argument(validator: Callable) -> Callable:
            """Attach a validation function to the ExtensionFunc as a managed
            property.
            """
            # Ensure validator is valid
            _name = self._signature.check_validator(validator, name)

            # wrap validator to accept argument context
            @wraps(validator)
            def validate_context(val, context=self.default_values, **kwargs):
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

            # add @property to ExtensionFunc
            prop = self._signature.generate_property(
                validator=validate_context,
                name=_name,
                doc=validator.__doc__
            )
            setattr(type(self), _name, prop)

            # remember validator
            self._signature.validators[_name] = validate_context
            return validate_context

        if _func is None:
            return argument
        return argument(_func)

    def remove_arg(self, *args: str) -> None:
        """Remove a registered argument from an
        :class:`ExtensionFunc <pdcast.ExtensionFunc>` instance.

        Parameters
        ----------
        *args
            The names of one or more arguments that are being actively managed
            by this object.

        Raises
        ------
        AttributeError
            If any of the referenced arguments are not being actively managed
            by this :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

        Examples
        --------
        .. doctest::

            >>> @extension_func
            ... def foo(bar, baz, **kwargs):
            ...     return bar, baz

            >>> @foo.argument(default=1)
            ... def bar(val: int, state: dict) -> int:
            ...     return int(val)

            >>> foo.bar
            1
            >>> foo.remove_arg("bar")
            >>> foo.bar
            Traceback (most recent call last):
                ...
            AttributeError: 'ExtensionFunc' object has no attribute 'bar'.
        """
        # ensure each argument is being actively managed
        for name in args:
            if name not in self._signature:
                raise AttributeError(f"'{name}' is not a managed argument")

        # commit to deletion
        for name in args:
            delattr(type(self), name)
            self._signature.settings.pop(name, None)
            self._signature.defaults.pop(name, None)
            self._signature.validators.pop(name)

    def reset_defaults(self, *args: str) -> None:
        """Reset one or more arguments to their default values.

        Parameters
        ----------
        *args
            The names of one or more arguments that are being actively managed
            by this object.

        Raises
        ------
        AttributeError
            If any of the referenced arguments are not being actively managed
            by this :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

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
            >>> foo.reset_defaults("bar", "baz")
            >>> foo.bar, foo.baz
            (1, 2)

        If no arguments are supplied to this method, then all arguments will
        be reset to their internal defaults.

        .. doctest::

            >>> foo.bar, foo.baz = -86, 17
            >>> foo.bar, foo.baz
            (-86, 17)
            >>> foo.reset_defaults()
            >>> foo.bar, foo.baz
            (1, 2)
        """
        if not args:
            # reset all
            for name in self:
                delattr(self, name)

        else:
            # reset some
            for name in args:  # ensure all args are being actively managed
                if name not in self:
                    raise AttributeError(f"'{name}' is not a managed argument")

            # commit to reset
            for name in args:
                delattr(self, name)

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

    def __contains__(self, item: str) -> bool:
        """Check if the named argument is being managed by this ExtensionFunc.
        """
        return item in self._signature.validators

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
        for par in self.signature.parameters.values():
            if par.kind == par.VAR_KEYWORD:
                self.kwargs_name = par.name
                break

        # init validators, default values
        self.validators = {}  # TODO: make this a WeakValueDictionary?
        self.defaults = {}
        self.settings = {}

    @property
    def parameters(self) -> MappingProxyType:
        """Get the hardcoded parameters of the decorated function."""
        return self.signature.parameters

    def check_validator(self, validator: Callable, name: str = None) -> str:
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

        # check name is not duplicate
        if name in self.validators:
            raise KeyError(f"default argument '{name}' already exists.")

        # check name exists in signature or **kwargs
        pars = self.signature.parameters
        if self.kwargs_name is None and name not in pars:
            raise TypeError(
                f"'{self.__qualname__}()' has no argument '{name}'"
            )

        # return final argument name
        return name

    def generate_property(
        self,
        validator: Callable,
        name: str,
        doc: str = None
    ) -> property:
        """Generate a property with appropriate getter, setter, and deleter
        methods for a managed argument.
        """
        def getter(self) -> Any:
            """Get the value of a managed argument."""
            # use current setting
            if name in self.settings:
                return self.settings[name]

            # fall back to default
            if name in self.defaults:
                return self.defaults[name]

            raise AttributeError(f"'{name}' has no default value")

        def setter(self, val: Any) -> None:
            """Set the value of a managed argument."""
            self.settings[name] = validator(val)

        def deleter(self) -> None:
            """Replace the value of a managed argument with its default."""
            # pass default through validator
            if name in self.defaults:
                validator(self.defaults[name])

            # remove setting if present
            self.settings.pop(name, None)

        # combine into @property descriptor
        return property(getter, setter, deleter, doc=doc)

    def __call__(self, *args, **kwargs) -> ExtensionArguments:
        """Create an `ExtensionArguments` from *args, **kwargs"""
        bound = self.signature.bind_partial(*args, **kwargs)
        parameters = tuple(self.signature.parameters)

        return ExtensionArguments(
            arguments=bound.arguments,
            parameters=parameters,
            positional=parameters[:len(args)],
            kwargs_name=self.kwargs_name,
            validators=self.validators,
            settings=self.settings
        )


class ExtensionArguments:
    """A wrapper around an `inspect.BoundArguments` object that exposes
    additional operations for validating arguments and applying dynamic
    defaults.
    """

    def __init__(
        self,
        arguments: dict[str, Any],
        parameters: tuple[str],
        positional: tuple[str],
        kwargs_name: str,
        validators: dict[str, Callable],
        settings: dict[str, Any]
    ):
        self.arguments = arguments
        self.parameters = parameters
        self.positional = positional
        self.kwargs_name = kwargs_name
        self.validators = validators
        self.settings = settings

    def flatten_kwargs(self) -> None:
        """Flatten the `**kwargs` argument into the main argument list if it is
        present.
        """
        if self.kwargs_name in self.arguments:
            self.arguments.update(self.arguments[self.kwargs_name])
            del self.arguments[self.kwargs_name]

    def validate(self) -> None:
        """Pass each argument through its associated validator."""
        for name, value in self.arguments.items():
            if name in self.validators:
                self.arguments[name] = self.validators[name](value)

    def apply_settings(self) -> None:
        """Apply dynamic defaults."""
        self.arguments |= {
            k: v for k, v in self.settings.items() if k not in self.arguments
        }

    def reconstruct(self) -> str:
        """Reconstruct the original function's signature in string form using
        the bound arguments.
        """
        # apply current settings
        self.apply_settings()

        # flatten extension arguments
        self.flatten_kwargs()

        # stringify args according to original signature
        strings = []
        for par in self.parameters:

            # display managed value
            if par in self.arguments:
                strings.append(f"{par}={repr(self.arguments[par])}")

            # display extension arguments
            elif par == self.kwargs_name:
                kwargs = {
                    k: v for k, v in self.arguments.items()
                    if k not in self.parameters
                }
                strings.extend(f"{k}={repr(v)}" for k, v in kwargs.items())
                strings.append(f"**{self.kwargs_name}")

            # display original
            else:
                strings.append(par)

        return strings

    @property
    def args(self) -> tuple[Any, ...]:
        """Positional arguments to be supplied to the wrapped function."""
        return tuple(self.arguments[p] for p in self.positional)

    @property
    def kwargs(self) -> dict[str, Any]:
        """Keyword arguments to be supplied to the wrapped function."""
        return {
            k: v for k, v in self.arguments.items() if k not in self.positional
        }
