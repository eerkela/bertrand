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

from .base import BaseDecorator


######################
####    PUBLIC    ####
######################


def extension_func(func: Callable) -> Callable:
    """A decorator that allows Python functions to accept managed arguments
    and default values.

    Parameters
    ----------
    func : Callable
        A Python function or other callable to be decorated.  If this accepts a
        ``**kwargs`` dict or similar variable-length keyword argument, then it
        will be allowed to take on arbitrary
        :ref:`extension arguments <extension_func.extension>` defined at run
        time.

    Returns
    -------
    ExtensionFunc
        A cooperative :class:`ExtensionFunc <pdcast.ExtensionFunc>` decorator,
        which manages default values and argument validators for the decorated
        callable.

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
            # update_wrapper(self, _func)

            # store attributes from main thread
            if threading.current_thread() == threading.main_thread():
                main_thread.append({
                    "class": type(self),
                    "_vals": self._vals,
                    "_defaults": self._defaults,
                    "_validators": self._validators
                })

            # copy attributes from main thread
            else:
                main = main_thread[0]
                for k in main["_validators"]:
                    setattr(type(self), k, getattr(main["class"], k))
                self._vals = main["_vals"].copy()
                self._defaults = main["_defaults"].copy()
                self._validators = main["_validators"].copy()

    return _ExtensionFunc(func)


#######################
####    PRIVATE    ####
#######################


class NoDefault:
    """Signals that an argument does not have an associated default value."""

    def __repr__(self) -> str:
        return "<no default>"


no_default = NoDefault()


class ExtensionFunc(BaseDecorator, threading.local):
    """A wrapper for the decorated callable that can be dynamically extended
    with custom arguments.

    Parameters
    ----------
    _func : Callable
        The decorated function or other callable.

    Notes
    -----
    Whenever an argument is :meth:`registered <ExtensionFunc.register_arg>` to
    this object, it is added as a managed :class:`property <python:property>`.
    This automatically generates an appropriate getter, setter, and deleter for
    the attribute based on the decorated validation function.

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

    _reserved = (
        BaseDecorator._reserved |
        {"_signature", "_vals", "_defaults", "_validators"}
    )

    def __init__(self, func: Callable):
        super().__init__(func=func)

        # check if function accepts **kwargs
        self._signature = inspect.signature(func)
        self._kwargs_name = None
        for par in self._signature.parameters.values():
            if par.kind == par.VAR_KEYWORD:
                self._kwargs_name = par.name
                break

        self._vals = {}
        self._defaults = {}
        self._validators = {}  # TODO: make this a WeakValueDictionary

    ####################
    ####    BASE    ####
    ####################

    @property
    def validators(self) -> MappingProxyType:
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

            >>> @foo.register_arg
            ... def bar(val: int, state: dict) -> int:
            ...     return int(val)

            >>> foo.validators   # doctest: +SKIP
            mappingproxy({'bar': <function bar at 0x7ff5ad9c6e60>})
        """
        return MappingProxyType(self._validators)

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
        return MappingProxyType({**self._defaults, **self._vals})

    def register_arg(
        self,
        _func: Callable = None,
        *,
        default: Any = no_default
    ) -> Callable:
        """A decorator that transforms a naked validation function into a
        managed argument for this
        :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

        Parameters
        ----------
        default : Optional[Any]
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
            # ensure validator is callable
            if not callable(validator):
                raise TypeError(
                    f"decorated function must be callable: {validator}"
                )

            # use name of validation function as argument name
            name = validator.__name__
            if name in self._validators:
                raise KeyError(f"default argument '{name}' already exists.")

            # check name exists in signature or **kwargs
            pars = self._signature.parameters
            if self._kwargs_name is None and name not in pars:
                raise TypeError(
                    f"'{self.__qualname__}()' has no argument '{name}'"
                )

            # wrap validator to accept default arguments
            @wraps(validator)
            def accept_default(val, state=self.default_values):
                return validator(val, state)

            # get default value and validate
            if default is no_default:  # check for annotation
                if name in pars and pars[name].default is not inspect._empty:
                    self._defaults[name] = accept_default(pars[name].default)
            else:
                self._defaults[name] = accept_default(default)

            # generate getter, setter, and deleter attributes for @property
            def getter(self) -> Any:
                if name in self._vals:
                    return self._vals[name]
                if name in self._defaults:
                    return self._defaults[name]
                raise AttributeError(f"'{name}' has no default value")

            def setter(self, val: Any) -> None:
                self._vals[name] = accept_default(val)

            def deleter(self) -> None:
                if name in self._defaults:
                    accept_default(self._defaults[name])
                self._vals.pop(name, None)

            # attach @property to DefaultFunc
            prop = property(
                getter,
                setter,
                deleter,
                doc=validator.__doc__
            )
            setattr(type(self), name, prop)

            # remember validator
            self._validators[name] = accept_default
            return accept_default

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

            >>> @foo.register_arg(default=1)
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
            if name not in self:
                raise AttributeError(f"'{name}' is not a managed argument")

        # commit to deletion
        for name in args:
            delattr(type(self), name)
            self._vals.pop(name, None)
            self._defaults.pop(name, None)
            self._validators.pop(name)

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

            >>> @foo.register_arg(default=1)
            ... def bar(val: int, state: dict) -> int:
            ...     return int(val)

            >>> @foo.register_arg(default=2)
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
        # reset all
        if not args:
            for name in self._validators:
                delattr(self, name)

        # reset some
        else:
            # ensure each argument is being actively managed
            for name in args:
                if name not in self:
                    raise AttributeError(f"'{name}' is not a managed argument")

            # commit
            for name in args:
                delattr(self, name)

    def _reconstruct_signature(self) -> list[str]:
        """Reconstruct the original function's signature in string form,
        incorporating the default values from this ExtensionFunc.
        """
        # bind managed defaults
        defaults = self._signature.bind_partial(**self.default_values)

        # apply defaults from annotations
        defaults.apply_defaults()

        # flatten extension arguments
        defaults = defaults.arguments
        if self._kwargs_name in defaults:
            defaults.update(defaults[self._kwargs_name])
            del defaults[self._kwargs_name]

        # insert args
        signature = []
        parameters = self._signature.parameters
        for par in parameters.values():
            # display default value
            if par.name in defaults:
                signature.append(f"{par.name}={defaults[par.name]}")

            # display extension arguments if they have default values
            elif par.name == self._kwargs_name:
                kwargs = {
                    k: v for k, v in defaults.items() if k not in parameters
                }
                signature.extend(f"{k}={v}" for k, v in kwargs.items())
                signature.append(f"**{self._kwargs_name}")

            # arg has no default
            else:
                signature.append(par.name)

        return signature

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __call__(self, *args, **kwargs):
        """Execute the decorated function with the default arguments stored in
        this ExtensionFunc.

        This also validates the input to each argument using the attached
        validators.
        """
        # bind *args to **kwargs
        kwargs = self._signature.bind_partial(*args, **kwargs).arguments

        # flatten **kwargs
        if self._kwargs_name in kwargs:
            kwargs.update(kwargs[self._kwargs_name])
            del kwargs[self._kwargs_name]

        # apply validator for each argument
        for name, value in kwargs.items():
            if name in self._validators:
                kwargs[name] = self._validators[name](value, kwargs)

        # merge with pre-validated defaults
        kwargs = {**self.default_values, **kwargs}

        # recover *args
        pars = self._signature.parameters
        args = [kwargs.pop(a) for a in list(pars)[:len(args)]]

        # pass to wrapped
        return self.__wrapped__(*args, **kwargs)

    def __contains__(self, item: str) -> bool:
        """Check if the named argument is being managed by this ExtensionFunc.
        """
        return item in self._validators

    def __iter__(self):
        """Iterate through the arguments that are being managed by this
        ExtensionFunc.
        """
        return iter(self._validators)

    def __len__(self) -> int:
        """Return the total number of arguments that are being managed by this
        ExtensionFunc
        """
        return len(self._validators)

    def __repr__(self) -> str:
        """Return a string representation of the decorated function with a
        reconstructed signature incorporating the default values stored in this
        ExtensionFunc.
        """
        sig = self._reconstruct_signature()
        return f"{self.__wrapped__.__qualname__}({', '.join(sig)})"
