"""This module describes a mechanism for binding naked Python functions to
external classes using virtual descriptors.

Classes
-------
Attachable
    A cooperative decorator that allows the wrapped function to be dynamically
    attached to external Python classes.

VirtualAttribute
    Base class for all virtual attributes (other than Namespaces).

Namespace
    A virtual namespace that acts as a bridge separating virtual attributes
    from the base class's existing interface.

InstanceMethod
    A method-like descriptor that implicitly passes a class instance as the
    first argument to the decorated function.

ClassMethod
    A class method descriptor that implicitly passes the class itself as the
    first argument to the decorated function.

StaticMethod
    A static method descriptor that has no binding behavior.

Property
    A property-like descriptor that uses the Attachable function as a getter.
"""
from __future__ import annotations
from types import MappingProxyType
from typing import Any, Callable
import weakref

from pdcast.util.type_hints import Descriptor

from .base import FunctionDecorator


######################
####    PUBLIC    ####
######################


def attachable(func: Callable) -> Callable:
    """A decorator that allows naked Python functions to be dynamically
    attached to external class objects.

    Parameters
    ----------
    func : Callable
        A Python function or other callable to be decorated.  When this is
        :meth:`attached <pdcast.Attachable.attach_to>` to a class, an
        appropriate reference will be implicitly passed as its first positional
        argument.

    Returns
    -------
    Attachable
        A :ref:`cooperative <attachable.nested>` decorator that allows
        transparent access to the decorated function.  These objects behave
        exactly like the original function when called, but expose additional
        methods for :meth:`attaching <pdcast.Attachable.attach_to>` them to
        external classes as :ref:`virtual attributes <attachable.attributes>`.
    """
    return Attachable(func)


#######################
####    PRIVATE    ####
#######################


class Attachable(FunctionDecorator):
    """A wrapper for the decorated callable that manages its attached
    endpoints.

    Parameters
    ----------
    func : Callable
        The decorated function or other callable.

    Examples
    --------
    The behavior of the decorated function is left unchanged.

    .. doctest::

        >>> @attachable
        ... def foo(data):
        ...     print("Hello, World!")
        ...     return data

        >>> foo(1)
        Hello, World!
        1
    """

    _reserved = FunctionDecorator._reserved | {"_attached"}

    def __init__(self, func: Callable):
        super().__init__(func=func)
        self._attached = weakref.WeakKeyDictionary()

    @property
    def attached(self) -> MappingProxyType:
        """A mapping of all the classes that this callable has been attached
        to.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping class objects to their associated
            descriptors.

        Examples
        --------
        .. doctest::

            >>> class MyClass:
            ...     pass

            >>> @attachable
            ... def foo(data):
            ...     print("Hello, World!")
            ...     return data

            >>> foo.attach_to(MyClass)
            >>> foo.attached   # doctest: +SKIP
            mappingproxy({<class '__main__.MyClass'>: <function foo at 0x7f7a265684c0>})

        The actual map is stored as a
        :class:`WeakKeyDictionary <weakref.WeakKeyDictionary>`, which evicts
        entries if the attached type is garbage collected.

        .. doctest::

            >>> import gc

            >>> del MyClass
            >>> gc.collect()  # marks MyClass for deletion
            >>> gc.collect()  # actually deletes MyClass
            >>> foo.attached
            mappingproxy({})
        """
        result = {class_: ref() for class_, ref in self._attached.items()}
        return MappingProxyType(result)

    def attach_to(
        self,
        class_: type,
        name: str | None = None,
        namespace: str | None = None,
        pattern: str = "method"
    ) -> None:
        """Attach the decorated callable to the given class definition.

        Parameters
        ----------
        class_ : type
            A Python class to attach this callable to.  The callable will be
            available to all instances of the class under the given
            ``name``.
        name : str | None, default None
            The name under which to attach the decorated callable.  This is
            used as an alias when it is accessed from the given class.  If
            empty, the name of the callable will be used directly.
        namespace : str | None, default None
            If given, a string specifying a
            :class:`Namespace <pdcast.Namespace>` to attach the decorated
            callable to.
        pattern : str, default "method"
            The pattern to use for accessing this callable.  The available
            options are ``"method"``, ``"classmethod"``, ``"staticmethod"``,
            and ``"property"``.

        Raises
        ------
        ValueError
            If the given ``pattern`` is not recognized.
        RuntimeError
            If the callable is already attached to the class.

        Notes
        -----
        This method uses the
        :ref:`descriptor protocol <python:descriptor-invocation>` to transform
        the first argument of the decorated callable into an implicit reference
        to the specified class.  The style in which these references are passed
        depends on the ``pattern`` argument:

            *   ``"method"`` - attaches the callable as an **instance** method
                of the specified class.  When it is called, an instance will be
                passed as the first argument.
            *   ``"classmethod"`` - attaches the callable as a **class** method
                of the specified class.  When called, the class itself will be
                passed as the first argument.
            *   ``"staticmethod"`` - attaches the callable as a **static**
                method of the specified class.  It can be invoked via a dotted
                look up from the specified class, but no arguments will be
                forwarded.
            *   ``"property"`` - attaches the callable as a managed
                **property** of the specified class.  The callable itself will
                be used as the property's getter, and it will be passed an
                instance of the class as its first argument.  Setters and
                deleters can be added using the standard
                :class:`@property <python:property>` interface (e.g.
                ``@class_.name.setter`` and ``@class_.name.deleter``,
                respectively).

        On an implementation level, this method works by attaching a
        :class:`VirtualAttribute <pdcast.VirtualAttribute>` descriptor to the
        parent class, which is a cooperative decorator for the original
        function.  When the descriptor is accessed via a dotted lookup, its
        :meth:`__get__() <python:object.__get__>` method is invoked, which
        binds an object to the attribute according to the specified
        ``pattern``.  When the attribute is invoked, the bound object is
        implicitly passed as the first argument of the function.

        .. note::

            This is exactly identical to Python's ordinary
            :ref:`instance binding <python:descriptorhowto>` mechanism for
            methods and other attributes.

        If a ``namespace`` is given, then the process is somewhat different.
        Rather than attaching a
        :class:`VirtualAttribute <pdcast.VirtualAttribute>` to the class
        directly, we first attach a :class:`Namespace <pdcast.Namespace>`
        descriptor to it instead.  We then add our
        :class:`VirtualAttributes <pdcast.VirtualAttribute>` to this
        :class:`Namespace <pdcast.Namespace>` descriptor, which passes along
        the bound object for us.  This allows us to separate our virtual
        attributes from the class's base namespace, leaving them free to take
        on whatever names we'd like without interfering with the object's
        existing functionality.

        Examples
        --------
        See the :doc:`API docs </content/api/attachable>` for examples on how
        to use this method.
        """
        # default to name of wrapped callable
        if not name:
            name = self.__wrapped__.__name__

        # check pattern is valid
        valid_patterns = ("property", "method", "classmethod", "staticmethod")
        if pattern not in valid_patterns:
            raise ValueError(
                f"`pattern` must be one of {valid_patterns}, not "
                f"{repr(pattern)}"
            )

        # check func is not already attached to class
        if class_ in self._attached:
            existing = self._attached[class_]()
            raise RuntimeError(
                f"'{self.__qualname__}' is already attached to "
                f"'{class_.__qualname__}' as '{existing.__qualname__}'"
            )

        # generate namespace
        if namespace:
            parent, original = generate_namespace(class_, name, namespace)
        else:  # attach to class itself
            parent = class_
            original = get_descriptor(class_, name, None)

        # generate kwargs for descriptor
        kwargs = {
            "func": self,
            "parent": parent,
            "name": name,
            "instance": None,
            "original_descriptor": original
        }

        # generate pattern-specific descriptor
        if pattern == "property":
            descriptor = Property(**kwargs)
        elif pattern == "method":
            descriptor = InstanceMethod(**kwargs)
        elif pattern == "classmethod":
            descriptor = ClassMethod(**kwargs)
        else:  # staticmethod
            descriptor = StaticMethod(**kwargs)

        # attach descriptor
        if namespace:  # attach to unique namespace type
            setattr(type(parent), name, descriptor)
        else:
            setattr(parent, name, descriptor)

        # genereate weak reference to attached class
        self._attached[class_] = weakref.ref(descriptor)


class VirtualAttribute(FunctionDecorator):
    """Base class for all :ref:`virtual attributes <attachable.attributes>`.

    These are produced by
    :meth:`Attachable.attach_to() <pdcast.Attachable.attach_to>` and should
    never be instantiated directly.

    Parameters
    ----------
    func : Attachable
        An :class:`Attachable <pdcast.Attachable>` function to forward calls
        to.
    parent : type | Namespace
        The type or :class:`Namespace <pdcast.Namespace>` that this attribute
        is assigned to.
    name : str
        The name of the attribute during dotted lookups.
    original_descriptor : Descriptor | Any | None
        The original implementation that this attribute is masking, if one
        exists.  This must be unbound and might implement the descriptor
        protocol, in which case its :meth:`__get__() <python:object.__get__>`
        method will be called during
        :attr:`.original <pdcast.VirtualAttribute.original>` lookups.
    instance : Any | None
        An instance of the class that this attribute was accessed from or
        :data:`None <python:None>` if it was invoked from the class itself.

    Notes
    -----
    See Python's :ref:`descriptor tutorial <python:descriptorhowto>` for more
    information on how these work.  The actual
    :meth:`__get__() <python:object.__get__>` implementations are defined in
    subclasses.

    Examples
    --------
    See the :ref:`API docs <attachable.attributes>` for examples of virtual
    attributes and their uses.
    """

    _reserved = FunctionDecorator._reserved | {
        "_parent", "_original", "__name__", "__qualname__", "__self__"
    }

    def __init__(
        self,
        func: Attachable,
        parent: type | Namespace,
        name: str,
        original_descriptor: Descriptor | Any | None,
        instance: Any | None
    ):
        super().__init__(func=func)
        self._parent = parent  # either a class or namespace
        self.__name__ = name
        self.__qualname__ = f"{self._parent.__qualname__}.{self.__name__}"
        self._original = original_descriptor
        self.__self__ = instance

    @property
    def original(self) -> Any:
        """Recover the original implementation, if one exists.

        Returns
        -------
        Any
            The same attribute that would have been accessed if this descriptor
            did not exist.

        Raises
        ------
        NotImplementedError
            If no original implementation could be found.

        Examples
        --------
        The returned attribute will be bound according to its original
        definition.

        .. doctest::

            >>> class MyClass:
            ... 
            ...     def foo(self):
            ...         print("method")
            ... 
            ...     @classmethod
            ...     def bar(cls):
            ...         print("class method")
            ... 
            ...     class baz:
            ... 
            ...         def __init__(self, instance):
            ...             self._instance = instance
            ... 
            ...         @property
            ...         def foo(self):
            ...             return "namespace property"
            ... 
            ...         @staticmethod
            ...         def bar():
            ...             print("namespace static method")

            >>> @attachable
            ... def foo(data):
            ...     print("virtual method")

            >>> foo.attach_to(MyClass)
            >>> MyClass().foo.original()
            method

            >>> MyClass.foo.detach()
            >>> foo.attach_to(MyClass, name="bar")
            >>> MyClass.bar.original()
            class method

            >>> MyClass.bar.detach()
            >>> foo.attach_to(MyClass, namespace="baz")
            >>> MyClass().baz.foo.original
            namespace property

            >>> MyClass.baz.foo.detach()
            >>> foo.attach_to(MyClass, namespace="baz", name="bar")
            >>> MyClass().baz.bar.original()
            namespace static method
        """
        parent = self._parent
        original = self._original
        if original is None:
            raise NotImplementedError(
                f"'{parent.__qualname__}' has no attribute '{self.__name__}'"
            )

        # from namespace
        if isinstance(parent, Namespace):
            instance = parent.original
            if isinstance(instance, type):
                owner = instance
                instance = None
            else:
                owner = type(instance)
        else:
            instance = self.__self__
            owner = parent

        # follow descriptor protocol
        if hasattr(original, "__get__"):
            return original.__get__(instance, owner)

        return original

    def detach(self) -> None:
        """Remove the attribute from the object and replace the
        :attr:`original <pdcast.VirtualAttribute.original>`, if one exists.

        Examples
        --------
        This method resets the attribute back to its original state, as it was
        before :meth:`Attachable.attach_to() <pdcast.Attachable.attach_to>`
        was called.

        .. doctest::

            >>> class MyClass:
            ...     pass

            >>> @attachable
            ... def foo(data):
            ...     return data

            >>> foo.attach_to(MyClass)
            >>> MyClass.foo   # doctest: +SKIP
            <function foo at 0x7f1f230944c0>
            >>> MyClass.foo.detach()
            >>> MyClass.foo
            Traceback (most recent call last):
                ...
            RuntimeError: type object 'MyClass' has no attribute 'foo'

        If the attribute has an
        :attr:`original <pdcast.VirtualAttribute.original>` implementation, it
        will be gracefully replaced.

        .. doctest::

            >>> class MyClass:
            ...     def foo(self):
            ...         return self

            >>> foo.attach_to(MyClass)
            >>> MyClass.foo   # doctest: +SKIP
            <function foo at 0x7f1f230944c0>
            >>> MyClass.foo.detach()
            >>> MyClass.foo   # doctest: +SKIP
            <function MyClass.foo at 0x7f1eeee9cd30>

        This method also automatically cleans up
        :class:`Namespaces <pdcast.Namespace>` if they are no longer managing
        any attributes.

        .. doctest::

            >>> class MyClass:
            ...     pass

            >>> foo.attach_to(MyClass, namespace="bar")
            >>> MyClass.bar.foo   # doctest: +SKIP
            <function foo at 0x7f1f230944c0>
            >>> MyClass.bar.foo.detach()
            >>> MyClass.bar.foo
            Traceback (most recent call last):
                ...
            RuntimeError: type object 'MyClass' has no attribute 'bar'
        """
        # replace original implementation
        if isinstance(self._parent, Namespace):
            delattr(type(self._parent), self.__name__)
        elif self._original is None:
            delattr(self._parent, self.__name__)
        else:
            setattr(self._parent, self.__name__, self._original)

        # delete namespace if empty
        if isinstance(self._parent, Namespace):
            class_ = self._parent._parent
            if not self._parent.attached:
                self._parent.detach()
        else:
            class_ = self._parent

        # remove from Attachable.attached
        del self.__wrapped__._attached[class_]

    def __call__(self, *args, **kwargs):
        """Call the attribute, passing in the bound object if one exists."""
        if self.__self__ is None:
            return self.__wrapped__(*args, **kwargs)  # static

        return self.__wrapped__(self.__self__, *args, **kwargs)  # bound

    def __get__(
        self,
        instance: Any,
        owner: type = None
    ) -> VirtualAttribute:
        """Access the attribute via a dotted lookup.

        See the Python :ref:`descriptor tutorial <python:descriptorhowto>` for
        more information on how this works.
        """
        raise NotImplementedError(
            f"'{type(self).__qualname__}' objects do not implement '.__get__()'"
        )


def generate_namespace(
    class_: type,
    name: str,
    namespace: str
) -> tuple:
    """Get an existing namespace or generate a new one with."""
    masked = get_descriptor(class_, namespace, None)

    if isinstance(masked, Namespace):
        space = masked
        masked = masked.original
    else:
        # NOTE: we need to create a unique subclass of Namespace to isolate
        # any descriptors that are attached at run time.

        class _Namespace(Namespace):
            pass

        space = _Namespace(
            parent=class_,
            name=namespace,
            instance=None,
            original_descriptor=masked
        )
        masked = getattr(class_, namespace, None)  # invoke __get__
        setattr(class_, space.__name__, space)

    if masked is None:
        return space, None
    if not isinstance(masked, type):
        masked = type(masked)
    return space, get_descriptor(masked, name, None)


def get_descriptor(class_: type, name: str, default: Any) -> Any:
    """Get a descriptor from a class without invoking its ``__get__()`` method.

    This works like getattr(), except that it avoids invoking the descriptor
    protocol.
    """
    # follow method resolution order (MRO)
    for base in class_.__mro__:
        try:
            return object.__getattribute__(base, name)
        except AttributeError:
            continue

    return default


#########################
####    NAMESPACE    ####
#########################


class Namespace:
    """A descriptor that can be used to hide
    :class:`VirtualAttributes <pdcast.VirtualAttribute>` behind a shared
    namespace.

    Parameters
    ----------
    parent : type
        The type that this namespace is assigned to.
    name : str
        The name of the namespace during lookups.
    original_descriptor : Any | None
        The original implementation that this namespace is masking, if one
        exists.  This must be unbound and might implement the descriptor
        protocol, in which case its :meth:`__get__() <python:object.__get__>`
        method will be called during
        :attr:`.original <pdcast.VirtualAttribute.original>` lookups.
    instance : Any | None
        An instance of the class that this attribute was accessed from or
        :data:`None <python:None>` if it was invoked from the class itself.

    Notes
    -----
    See the :ref:`descriptor tutorial <python:descriptorhowto>` for more
    information on how these work.

    Examples
    --------
    See the :ref:`API docs <attachable.namespace>` for examples of namespaces
    and their benefits.
    """

    _reserved = {
        "_parent", "_original", "__name__", "__qualname__", "__self__"
    }

    def __init__(
        self,
        parent: type,
        name: str,
        original_descriptor: Descriptor | Any | None,
        instance: Any | None
    ):
        self._parent = parent
        self.__name__ = name
        self.__qualname__ = f"{self._parent.__qualname__}.{self.__name__}"
        self._original = original_descriptor
        self.__self__ = instance

    @property
    def attached(self) -> MappingProxyType:
        """A mapping of all the attributes that are being managed by this
        :class:`Namespace <pdcast.Namespace>`.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping attribute names to their associated
            descriptors.

        Examples
        --------
        .. doctest::

            >>> class MyClass:
            ...     pass

            >>> @attachable
            ... def foo(data):
            ...     print("Hello, World!")
            ...     return data

            >>> foo.attach_to(MyClass, namespace="bar")
            >>> foo.bar.attached
            mappingproxy({"foo": <function foo at 0x7f7a265684c0>})
        """
        attrs = type(self).__dict__
        result = {
            k: v for k, v in attrs.items() if isinstance(v, VirtualAttribute)
        }
        return MappingProxyType(result)

    @property
    def original(self) -> Any:
        """Recover the original attribute, if one exists.

        Returns
        -------
        Any
            The same attribute that would have been accessed if this descriptor
            did not exist.

        Raises
        ------
        RuntimeError
            If no original implementation could be found.

        Examples
        --------
        The returned attribute will be bound according to its original
        definition.

        .. doctest::

            >>> class MyClass:
            ... 
            ...     def foo(self):
            ...         print("method")
            ... 
            ...     @classmethod
            ...     def bar(cls):
            ...         print("class method")
            ... 
            ...     class baz:
            ... 
            ...         def __init__(self, instance):
            ...             self._instance = instance
            ... 
            ...         @property
            ...         def foo(self):
            ...             return "namespace property"
            ... 
            ...         @staticmethod
            ...         def bar():
            ...             print("namespace static method")

            >>> @attachable
            ... def foo(data):
            ...     print("virtual method")

            >>> foo.attach_to(MyClass, namespace="foo")
            >>> MyClass().foo.original()
            method

            >>> MyClass.foo.detach()
            >>> foo.attach_to(MyClass, namespace="bar")
            >>> MyClass.bar.original()
            class method

            >>> MyClass.bar.detach()
            >>> foo.attach_to(MyClass, namespace="baz")
            >>> MyClass.baz.original
            <class 'MyClass.baz'>
        """
        parent = self._parent
        original = self._original
        if original is None:
            raise RuntimeError(
                f"'{parent.__qualname__}' has no attribute {self.__name__}'"
            )

        # follow descriptor protocol
        if hasattr(original, "__get__"):
            return original.__get__(self.__self__, parent)    

        return original

    def detach(self) -> None:
        """Remove the namespace from the object and replace the
        :attr:`original <pdcast.Namespace.original>` attribute, if one exists.

        Examples
        --------
        This method resets an attribute back to its original state, as it was
        before :meth:`Attachable.attach_to() <pdcast.Attachable.attach_to>`
        created it.

        .. doctest::

            >>> class MyClass:
            ...     pass

            >>> @attachable
            ... def foo(data):
            ...     return data

            >>> foo.attach_to(MyClass, namespace="foo")
            >>> MyClass.foo   # doctest: +SKIP
            <pdcast.decorators.attachable.generate_namespace.<locals>._Namespace object at 0x7fbd591adba0>
            >>> MyClass.foo.detach()
            >>> MyClass.foo
            Traceback (most recent call last):
                ...
            AttributeError: type object 'MyClass' has no attribute 'foo'

        If the attribute has an
        :attr:`original <pdcast.VirtualAttribute.original>` implementation, it
        will be gracefully replaced.

        .. doctest::

            >>> class MyClass:
            ...     def foo(self):
            ...         return self

            >>> foo.attach_to(MyClass)
            >>> MyClass.foo   # doctest: +SKIP
            <function foo at 0x7f1f230944c0>
            >>> MyClass.foo.detach()
            >>> MyClass.foo   # doctest: +SKIP
            <function MyClass.foo at 0x7f1eeee9cd30>

        This method also removes all attributes that are being managed by this
        :class:`Namespace <pdcast.Namespace>`.

        .. doctest::

            >>> class MyClass:
            ...     pass

            >>> @attachable
            ... def bar(data):
            ...     return data

            >>> foo.attach_to(MyClass, namespace="baz")
            >>> bar.attach_to(MyClass, namespace="baz")
            >>> MyClass.baz.attached   # doctest: +SKIP
            mappingproxy({'foo': <function foo at 0x7f1f230944c0>, 'bar': <function bar at 0x7f3f5dcac4c0>})
            >>> MyClass.baz.detach()
            >>> MyClass.baz.foo
            Traceback (most recent call last):
                ...
            AttributeError: type object 'MyClass' has no attribute 'baz'
            >>> foo.attached
            mappingproxy({})
            >>> bar.attached
            mappingproxy({})
        """
        # detach bound attributes
        for _, attr in self.attached.items():
            attr.detach()

        # replace original implementation
        if self._original:
            setattr(self._parent, self.__name__, self._original)
        elif hasattr(self._parent, self.__name__):  # might already be deleted
            delattr(self._parent, self.__name__)

    def __get__(
        self,
        instance: Any,
        owner: type = None
    ) -> Namespace:
        """Access the :class:`Namespace <pdcast.Namespace>` via a dotted
        lookup.

        See the Python :ref:`descriptor tutorial <python:descriptorhowto>` for
        more information on how this works.
        """
        # from class
        if instance is None:
            return self

        # from instance
        return type(self)(
            parent=self._parent,
            name=self.__name__,
            original_descriptor=self._original,
            instance=instance
        )

    def __getattr__(self, name: str) -> Any:
        """Delegate attribute access to the original implementation, if one
        exists.
        """
        return getattr(self.original, name)

    def __setattr__(self, name: str, value: Any) -> None:
        """Delegate attribute access to the original implementation, if one
        exists.
        """
        # name is internal
        if name in self._reserved:
            self.__dict__[name] = value

        # name is a virtual method to associate with this namespace
        elif isinstance(value, VirtualAttribute):
            setattr(type(self), name, value)

        # delegate to original
        else:
            setattr(self.original, name, value)

    def __delattr__(self, name: str) -> None:
        """Delegate attribute access to the original implementation, if one
        exists.
        """
        delattr(self.original, name)


######################
####    METHOD    ####
######################


class InstanceMethod(VirtualAttribute):
    """Transforms an :class:`Attachable <pdcast.Attachable>` function into a
    bound method.

    These descriptors are returned by
    :meth:`Attachable.attach_to <pdcast.Attachable.attach_to>` with
    ``pattern="method"`` (the default).  They behave exactly like ordinary
    Python instance methods.
    """

    def __get__(
        self,
        instance: Any,
        owner: type = None
    ) -> InstanceMethod:
        """See
        :meth:`VirtualAttribute.__get__() <pdcast.VirtualAttribute.__get__>`.
        """
        parent = self._parent

        # from namespace
        if isinstance(instance, Namespace):
            parent = instance
            instance = instance.__self__

        # from class
        if instance is None:
            return self

        # from instance
        return InstanceMethod(
            func=self.__wrapped__,
            parent=parent,
            name=self.__name__,
            original_descriptor=self._original,
            instance=instance
        )


############################
####    CLASS METHOD    ####
############################


class ClassMethod(VirtualAttribute):
    """Transforms an :class:`Attachable <pdcast.Attachable>` function into a
    :func:`classmethod <python:classmethod>`.

    These descriptors are returned by
    :meth:`Attachable.attach_to <pdcast.Attachable.attach_to>` with
    ``pattern="classmethod"``.  They behave exactly like
    :func:`@classmethod <python:classmethod>` decorators in normal Python.
    """

    def __get__(
        self,
        instance: Any,
        owner: type = None
    ) -> ClassMethod:
        """See
        :meth:`VirtualAttribute.__get__() <pdcast.VirtualAttribute.__get__>`.
        """
        parent = self._parent

        # from namespace
        if isinstance(instance, Namespace):
            parent = instance
            owner = instance._parent

        # from class
        if owner is None:
            owner = type(instance)

        # from instance
        return ClassMethod(
            func=self.__wrapped__,
            parent=parent,
            name=self.__name__,
            original_descriptor=self._original,
            instance=owner
        )


#############################
####    STATIC METHOD    ####
#############################


class StaticMethod(VirtualAttribute):
    """Transforms an :class:`Attachable <pdcast.Attachable>` function into a
    :func:`staticmethod <python:staticmethod>`.

    These descriptors are returned by
    :meth:`Attachable.attach_to <pdcast.Attachable.attach_to>` with
    ``pattern="staticmethod"``.  They behave exactly like
    :func:`@staticmethod <python:staticmethod>` decorators in normal Python.
    """

    def __get__(
        self,
        instance: Any,
        owner: type = None
    ) -> StaticMethod:
        """See
        :meth:`VirtualAttribute.__get__() <pdcast.VirtualAttribute.__get__>`.
        """
        return self


########################
####    PROPERTY    ####
########################


class Property(VirtualAttribute):
    """Transforms an :class:`Attachable <pdcast.Attachable>` function into a
    managed :class:`@property <python:property>`.

    These descriptors are returned by
    :meth:`Attachable.attach_to <pdcast.Attachable.attach_to>` with
    ``pattern="property"``.  They behave exactly like
    :class:`@property <python:property>` decorators in normal Python.
    """

    _reserved = VirtualAttribute._reserved | {"_property"}

    def __init__(
        self,
        func: Attachable,
        parent: type | Namespace,
        name: str,
        original_descriptor: Descriptor | Any | None,
        instance: Any | None
    ):
        super().__init__(
            func=func,
            parent=parent,
            name=name,
            original_descriptor=original_descriptor,
            instance=instance
        )
        self._property = property(fget=func)

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._property.__get__(instance, owner)

    def __set__(self, instance, value):
        return self._property.__set__(instance, value)

    def __delete__(self, instance):
        return self._property.__delete__(instance)

    def getter(self, fget):
        """Analogue of `@prop.getter` for virtual properties."""
        self._property = self._property.getter(fget)
        return self

    def setter(self, fset):
        """Analogue of `@prop.setter` for virtual properties."""
        self._property = self._property.setter(fset)
        return self

    def deleter(self, fdel):
        """Analogue of `@prop.deleter` for virtual properties."""
        self._property = self._property.deleter(fdel)
        return self

    def __call__(self, *args, **kwargs):
        """Call the property, producing an identical error to python."""
        return self._property(*args, **kwargs)  # raises TypeError

    def __repr__(self) -> str:
        return object.__repr__(self)

