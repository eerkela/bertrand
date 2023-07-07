"""This module describes an ``@extension_func`` decorator that transforms an
ordinary Python function into one that can accept managed arguments with custom
validators and dynamic values.
"""
from __future__ import annotations
from functools import wraps
import inspect
import threading
from types import MappingProxyType
from typing import Any, Callable, Mapping, get_type_hints

from .base import EMPTY, Arguments, FunctionDecorator, Signature


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
        :ref:`extension arguments <extension_func.extension>` that are defined
        at runtime.

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
    """A wrapper for a function that manages its arguments.

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
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> @foo.argument
            ... def bar(val, context: dict) -> int:
            ...     return int(val)

            >>> foo.arguments   # doctest: +SKIP
            mappingproxy({'bar': <function bar at 0x7ff5ad9c6e60>})
        """
        return MappingProxyType(self._signature.validators)

    @property
    def settings(self) -> MappingProxyType:
        """A mapping of all managed arguments to their current values.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping argument names to their current
            values for this :class:`ExtensionFunc <pdcast.ExtensionFunc>`.  If
            no explicit value is associated with an argument, then it will be
            excluded from the map.

        Examples
        --------
        .. doctest::

            >>> @extension_func
            ... def foo(bar, baz=2, **kwargs)
            ...     return {"bar": bar, "baz": baz} | kwargs

            >>> @foo.argument(default=1)
            ... def bar(val, context: dict) -> int:
            ...     return int(val)

            >>> @foo.argument
            ... def baz(val, context: dict) -> int:
            ...     return int(val)

            >>> foo.settings
            mappingproxy({'bar': 1, 'baz': 2})
            >>> foo.bar, foo.baz = 24, "-17"
            >>> foo.settings
            mappingproxy({'bar': 24, 'baz': -17})
        """
        return self._signature.settings

    def argument(
        self,
        func: Callable = None,
        *,
        name: str | None = None,
        default: Any = EMPTY
    ) -> Callable:
        """A decorator that transforms a validation function into a managed
        argument for this :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

        Parameters
        ----------
        name : str | None, default None
            The name of the argument that the validator validates.  If this is
            left as :data:`None <python:None>`, then the name of the validator
            will be used instead.
        default : Any | None, default EMPTY
            The default value to use for this argument.  This is implicitly
            passed to the validator itself, so any custom logic that is
            implemented there will also be applied to this value if it is not
            empty.  Otherwise, if this argument is omitted and the decorated
            function defines a default value in its call signature, then that
            value will be used instead.

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
            # ensure validator is valid
            self._signature.check_validator(validator)

            # use name of validator as argument name if not explicitly given
            _name = name
            if _name is None:
                _name = validator.__name__
            elif not isinstance(_name, str):
                raise TypeError(f"name must be a string, not {type(_name)}")

            # ensure name is not already in use
            if _name in self._signature.validators:
                raise KeyError(f"argument '{_name}' already exists")

            # ensure name is not overwriting a reserved attribute
            if _name in dir(self):
                raise TypeError(f"'{_name}' is a reserved attribute")

            # wrap validator to accept argument context
            @wraps(validator)
            def validate_context(val, context=self.settings, **kwargs):
                """Automatically populate `context` argument of validator."""
                return validator(val, context, **kwargs)

            # add argument to signature and generate @property
            prop = self._signature.register_argument(
                name=_name,
                validator=validate_context,
                default=default,
                annotation=get_type_hints(validator)["return"]
            )
            setattr(type(self), _name, prop)

            # return wrapped validator
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
        self._signature.defaults.pop(name, None)
        self._signature.validators.pop(name)

        # replace with original parameter if present
        try:
            self._signature.reset_parameter(name)
        except TypeError:
            self._signature.delete_parameter(name)

    def reset_defaults(self) -> None:
        """Reset all arguments to their original defaults.

        Notes
        -----
        This is effectively equivalent to calling the ``del`` keyword on every
        argument registered to this
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
        self._signature.reset_defaults()

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __call__(self, *args, **kwargs) -> Any:
        """Execute the decorated function with the managed arguments.

        Parameters
        ----------
        *args, **kwargs
            Arbitrary positional and keyword arguments passed to this function.
            These will always be redirected through their respective validation
            function before being passed to the decorated callable.

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
        # generate ExtensionArguments
        bound = self._signature(*args, **kwargs)

        # pass arguments through their respective validators
        bound.validate()

        # NOTE: validate() implicitly applies defaults

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
            foo(bar=1, baz=2, *, qux=3, **kwargs)
            >>> repr(foo)
            'foo(bar=1, baz=2, *, qux=3, **kwargs)'
        """
        return self._signature.reconstruct(annotations=False)


class ExtensionSignature(Signature):
    """A wrapper around an :class:`inspect.Signature <python:inspect.Signature>`
    object that serves as a factory for
    :class:`ExtensionArguments <pdcast.ExtensionArguments>`.

    Parameters
    ----------
    func : Callable
        The function whose signature will be wrapped.

    Notes
    -----
    These capture the arguments that are passed to
    :class:`ExtensionFunc.__call__() <pdcast.ExtensionFunc.__call__>` and
    encapsulate them in a dedicated
    :class:`ExtensionArguments <pdcast.ExtensionArguments>` object.

    :class:`ExtensionArguments <pdcast.ExtensionArguments>` provide a
    high-level interface for interacting with and validating arguments, without
    having to interact with the complicated machinery of the
    :mod:`inspect <python:inspect>` module.
    """

    def __init__(self, func: Callable):
        super().__init__(func=func)
        self.accepts_kwargs = any(
            par.kind == par.VAR_KEYWORD for par in self.parameters
        )
        self.defaults = {
            par.name: par.default for par in self.parameters
            if par.default is not EMPTY
        }
        self.validators = {}

    @property
    def settings(self) -> Mapping[str, Any]:
        """A mapping of all managed arguments to their current values.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping argument names to their current
            values for this
            :class:`ExtensionSignature <pdcast.ExtensionSignature>`.  If no
            explicit value is associated with an argument, then it will be
            excluded from the map.
        """
        return MappingProxyType({
            par.name: par.default for par in self.parameters
            if par.name in self.validators and par.default is not EMPTY
        })

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
        self.sig = other.sig  # copy other signature
        self.validators = other.validators.copy()
        self.defaults = other.defaults.copy()

    def reset_defaults(self) -> None:
        """Reset all settings to their hardcoded defaults.

        Notes
        -----
        This resets to either the ``default`` that was given in
        :meth:`@argument <pdcast.ExtensionFunc.argument>` or the hardcoded
        value in the function's original signature, whichever comes first.
        """
        self.parameters = tuple(
            par.replace(default=self.defaults.get(par.name, EMPTY))
            for par in self.parameters
        )

    def check_validator(self, validator: Callable) -> None:
        """Confirm that a validator is callable with the expected parameters.

        Parameters
        ----------
        validator : Callable
            A custom validation function decorated with
            :meth:`@argument <pdcast.ExtensionFunc.argument>`.

        Raises
        ------
        TypeError
            If the validator is not callable, or if it does not accept at least
            2 arguments: the value itself and a context dictionary storing the
            values of every other argument that the function was invoked with.
        """
        # ensure validator is callable
        if not callable(validator):
            raise TypeError(f"validator must be callable: {validator}")

        # ensure validator accepts at least 2 arguments
        if len(inspect.signature(validator).parameters) < 2:
            raise TypeError(
                f"validator must accept at least 2 arguments: {validator}"
            )

    def register_argument(
        self,
        name: str,
        validator: Callable,
        default: Any = EMPTY,
        annotation: Any = EMPTY
    ) -> property:
        """Add an argument to this signature.

        Parameters
        ----------
        name : str
            The name of the argument to add.
        validator : Callable
            The validation function to use when validating and setting the
            argument.  This must be decorated with
            :meth:`@ExtensionFunc.argument <pdcast.ExtensionFunc.argument>`.

        Returns
        -------
        property
            A managed :class:`property <python:property>` to be added to the
            :class:`ExtensionFunc <pdcast.ExtensionFunc>` itself.  This has
            full getter, setter, and deleter logic based on the ``validator``
            that was passed as input.

        Raises
        ------
        TypeError
            If the argument name does not appear in the function's signature
            and the function does not accept a variable-length ``**kwargs``
            argument (or a differently-named equivalent).

        Notes
        -----
        The returned :class:`property <python:property>` uses the following
        logic for attribute access:

            *   When :meth:`__get__() <python:object.__get__>` is called, the
                result is always equal to the default value of the associated
                :class:`Parameter <python:inspect.Parameter>` for this
                :class:`ExtensionSignature <pdcast.ExtensionSignature>`.
            *   When :meth:`__set__() <python:object.__set__>` is called, the
                assigned value will be passed through the validator and then
                stored directly on the argument's
                :class:`Parameter <python:inspect.Parameter>`.
            *   When :meth:`__del__() <python:object.__del__>` is called, the
                argument will be reset to its original state, removing any
                dynamic values that were assigned to it.
        """
        # pass default value through validator
        if default is EMPTY:
            pars = self.parameter_map
            if name in pars and pars[name].default is not EMPTY:
                default = validator(pars[name].default)
        else:
            default = validator(default)

        # if argument is in signature, modify its default
        if name in self.parameter_map:
            self.set_parameter(name, default=default, annotation=annotation)

        # otherwise, extend signature with new argument
        else:
            # assert signature accepts a `**kwargs`-equivalent
            if not self.accepts_kwargs:
                raise TypeError(
                    f"'{self.__qualname__}()' has no argument '{name}'"
                )

            # generate parameter and insert into signature
            self.add_parameter(
                name,
                kind="KEYWORD_ONLY",
                default=default,
                annotation=annotation
            )

        # remember validator, default
        self.validators[name] = validator
        if default is not EMPTY:
            self.defaults[name] = default

        # generate getter, setter, and deleter for @property
        def getter(self) -> Any:
            """Get the value of a managed argument."""
            result = self._signature.parameter_map[name].default
            if result is EMPTY:
                raise TypeError(f"'{name}' has no default value")
            return result

        def setter(self, val: Any) -> None:
            """Set the value of a managed argument."""
            self._signature.set_parameter(name, default=validator(val))

        def deleter(self) -> None:
            """Replace the value of a managed argument with its default."""
            val = self._signature.defaults.get(name, EMPTY)
            if val is not EMPTY:
                setter(self, val)
            else:
                self._signature.set_parameter(name, default=EMPTY)

        return property(getter, setter, deleter, doc=validator.__doc__)

    # pylint: disable=no-self-argument
    def __call__(__self, *args, **kwargs) -> ExtensionArguments:
        """Bind the arguments to this signature and return a corresponding
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
        bound = __self.sig.bind_partial(*args, **kwargs)
        return ExtensionArguments(bound=bound, signature=__self)


class ExtensionArguments(Arguments):
    """Captured arguments from a call to
    :meth:`ExtensionFunc(*args, **kwargs) <pdcast.ExtensionFunc.__call__>`.

    Notes
    -----
    Whenever an :class:`ExtensionFunc <pdcast.ExtensionFunc>` is called, its
    arguments are intercepted and encapsulated in an
    :class:`ExtensionArguments <pdcast.ExtensionArguments>` object.  These
    behave similarly to
    :class:`inspect.BoundArguments <python:inspect.BoundArguments>`, but with
    additional operations for validating their values and applying dynamic
    defaults.
    """

    def validate(self) -> None:
        """Pass each argument through its associated validator.

        Notes
        -----
        This should always be called before
        :meth:`apply_defaults() <pdcast.Arguments.apply_defaults>` in order to
        minimize validation calls.  Since defaults are pre-validated by their
        associated properties, there is no need to check them manually.
        """
        # record original arguments before applying defaults
        explicit = tuple(self.values)

        # apply defaults.  These are pre-validated by their properties.
        self.apply_defaults()

        # validate the non-default arguments
        for name in explicit:
            if name in self.signature.validators:
                validator = self.signature.validators[name]
                self.values[name] = validator(self.values[name], self.values)
