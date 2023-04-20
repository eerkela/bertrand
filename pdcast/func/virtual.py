"""This module describes a mechanism for binding naked Python functions to
existing classes using virtual descriptors.

Classes
-------
Attachable
    A cooperative decorator that allows the wrapped function to be dynamically
    attached to existing Python classes.

Namespace
    A virtual namespace that acts as a bridge separating virtual attributes
    from the base class's existing interface.

InstanceMethod
    A method-like descriptor that implicitly passes a class instance as the
    first argument to the decorated function.
"""
from __future__ import annotations
from types import MappingProxyType, MethodType
from typing import Any, Callable
import weakref

from .base import BaseDecorator


######################
####    PUBLIC    ####
######################


def attachable(func: Callable) -> Callable:
    """A decorator that allows naked Python functions to be dynamically
    attached to external class objects.

    Parameters
    ----------
    func : Callable
        A python function or other callable to be decorated.  When this is
        :meth:`attached <pdcast.Attachable.attach_to>` to a class, a ``self``
        reference will be implicitly passed as its first positional argument.

    Returns
    -------
    Attachable
        A cooperative decorator that allows full attribute access down the
        decorator stack.  This object behaves exactly like the original
        function when called, but exposes additional methods for
        :meth:`attaching <pdcast.Attachable.attach_to>` it to existing classes
        in a :ref:`variety of ways <attachable.attributes>`.
    """
    return Attachable(func)


#######################
####    PRIVATE    ####
#######################


class Attachable(BaseDecorator):
    """Returned by the :func:`@attachable <pdcast.attachable>` decorator.

    Users should never need to create these objects themselves.

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

    _reserved = BaseDecorator._reserved | {"_attached"}

    def __init__(self, func: Callable):
        super().__init__(func=func)
        self._attached = weakref.WeakKeyDictionary()

    @property
    def attached(self) -> MappingProxyType:
        """A mapping of all the classes that this function has been attached
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

        The actual mapping is stored as a
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
            used as an alias when it is called from the given class.  If empty,
            the name of the callable will be used directly.
        namespace : str | None, default None
            If given, a string specifying a
            :class:`Namespace <pdcast.Namespace>` to attach the decorated
            callable to.
        pattern : str, default "method"
            The pattern to use for accessing this callable.  The available
            options are ``"property"``, ``"method"``, ``"classmethod"``, and
            ``"staticmethod"``.

        Notes
        -----
        This method uses the
        :ref:`descriptor protocol <python:descriptor-invocation>` to transform
        the first argument of the decorated callable into an implicit ``self``
        reference.  It works by attaching a
        :class:`InstanceMethod <pdcast.InstanceMethod>` descriptor to the parent
        class, which is a cooperative decorator for the original function.
        When the descriptor is accessed from an instance of the class, its
        :meth:`__get__() <python:object.get>` method is invoked, which binds
        the instance to the method.  Then, when the method is called, the
        instance is implicitly passed as the first argument of the function.
        This is exactly identical to Python's ordinary
        :ref:`instance binding <python:descriptorhowto>` mechanism for methods
        and other attributes.

        If a ``namespace`` is given, then the process is somewhat different.
        Rather than attaching a :class:`InstanceMethod <pdcast.InstanceMethod>` to
        the class directly, we first attach a
        :class:`Namespace <pdcast.Namespace>` descriptor instead.  We then add
        our :class:`InstanceMethods <pdcast.InstanceMethod>` to this
        :class:`Namespace <pdcast.Namespace>`, which passes along the bound
        instance for us.  This allows us to separate our methods from the
        class's base namespace, leaving them free to take on whatever names
        we'd like without interfering with the object's existing functionality.

        Examples
        --------
        See the :ref:`API docs <attachable.example>` for examples on how to use
        this method.
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
            raise AttributeError(
                f"'{self.__qualname__}' is already attached to "
                f"'{class_.__qualname__}' as '{existing.__qualname__}'"
            )

        # (optionally) generate namespace
        if namespace:
            existing = getattr(class_, namespace, None)
            if isinstance(existing, Namespace):
                namespace = existing
            else:
                # NOTE: we need to create a unique subclass of Namespace to
                # isolate any descriptors that are attached to it at run time.
                class _Namespace(Namespace):
                    pass

                namespace = _Namespace(
                    parent=class_,
                    name=namespace,
                    instance=None,
                    original=getattr(class_, namespace, None)
                )
                setattr(class_, namespace.__name__, namespace)

        parent = namespace or class_

        # generate pattern-specific descriptor
        if pattern == "property":
            raise NotImplementedError()
        elif pattern == "method":
            descriptor = InstanceMethod(
                parent=parent,
                name=name,
                func=self,
                instance=None,
                original=getattr(parent, name, None)
            )
        elif pattern == "classmethod":
            descriptor = ClassMethod(
                parent=parent,
                name=name,
                func=self,
                instance=None,
                original=getattr(parent, name, None)
            )
        else:  # staticmethod
            descriptor = StaticMethod(
                parent=parent,
                name=name,
                func=self,
                instance=None,
                original=getattr(parent, name, None)
            )

        # attach descriptor
        if namespace:  # attach to unique namespace type
            setattr(type(parent), name, descriptor)
        else:
            setattr(parent, name, descriptor)

        # genereate weak reference to attached class
        self._attached[class_] = weakref.ref(descriptor)


class VirtualAttribute(BaseDecorator):
    """Base class for all :ref:`bound attributes <attachable.attributes>`.
    """

    _reserved = BaseDecorator._reserved | {
        "_parent", "_original", "__name__", "__qualname__", "__self__"
    }

    def __init__(
        self,
        parent: type | Namespace,
        name: str,
        func: Callable,
        instance: Any | None,
        original: Any | None
    ):
        super().__init__(func=func)
        self._parent = parent  # either a class or namespace
        self.__name__ = name
        self.__qualname__ = f"{self._parent.__qualname__}.{self.__name__}"
        self.__self__ = instance
        self._original = original

    @property
    def original(self) -> Callable:
        """Get the original implementation, if one exists."""
        if self._original is None:
            raise AttributeError(
                f"'{self._parent.__qualname__}' has no attribute "
                f"'{self.__name__}'"
            )

        # NOTE: @classmethod pre-binds the original class.

        # from namespace
        if isinstance(self._parent, Namespace):
            # original is instance method of original namespace
            if not isinstance(self._parent.original, type):
                return MethodType(self._original, self._parent.original)

            # original is class or static method
            return self._original

        # from class
        if self.__self__ is None or isinstance(self.__self__, type):
            return self._original

        # from instance
        return MethodType(self._original, self.__self__)

    def detach(self) -> None:
        """Remove the attribute from the object and replace the original, if
        one exists.
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
        """TODO"""
        raise NotImplementedError(
            f"'{type(self).__qualname__}' objects do not implement '.__get__()'"
        )


#########################
####    NAMESPACE    ####
#########################


class Namespace:
    """A descriptor that spawns :class:`Namespace <pdcast.Namespace>` objects
    on access, which can be used to hide other
    :class:`VirtualAttributes <pdcast.VirtualAttribute>`.

    Parameters
    ----------
    parent : type | VirtualDescriptor
        A Python class or another
        :class:`VirtualDescriptor <pdcast.func.virtual.VirtualDescriptor>` that
        this descriptor is attached to.  Examples of descriptor chaining
        include
        :class:`NamespaceDescriptors <pdcast.func.virtual.NamespaceDescriptor>`
        and the :class:`InstanceMethods <pdcast.InstanceMethod>` that spawn from
        them.
    name : str
        The name of this descriptor as supplied to
        :func:`setattr <python:setattr>`.

    Attributes
    ----------
    _parent : type | VirtualDescriptor
        See the ``parent`` parameter above.
    _name : str
        See the ``name`` parameter above.
    _original : Any | None
        The original attribute being masked by this descriptor, if one exists.
    """

    _reserved = {
        "_parent", "_original", "__name__", "__qualname__", "__self__"
    }

    def __init__(
        self,
        parent: type,
        name: str,
        instance: Any | None,
        original: Any | None
    ):
        self._parent = parent
        self.__self__ = instance
        self.__name__ = name
        self.__qualname__ = f"{self._parent.__qualname__}.{self.__name__}"
        self._original = original

    @property
    def attached(self) -> MappingProxyType:
        """TODO"""
        attrs = type(self).__dict__
        result = {
            k: v for k, v in attrs.items() if isinstance(v, VirtualAttribute)
        }
        return MappingProxyType(result)

    @property
    def original(self) -> Any:
        """Get the original implementation of the namespace, if one exists."""
        if self._original is None:
            raise AttributeError(
                f"'{self._parent.__qualname__}' has no attribute "
                f"'{self.__name__}'"
            )

        # from class
        if self.__self__ is None:
            return self._original

        # from instance
        return self._original(self.__self__)

    def detach(self) -> None:
        """Remove the attribute from the object and replace the original, if
        one exists.
        """
        # detach bound attributes
        for _, attr in self.attached.items():
            attr.detach()

        # replace original implementation
        if self._original is None:
            delattr(self._parent, self.__name__)
        else:
            setattr(self._parent, self.__name__, self._original)

    def __get__(
        self,
        instance: Any,
        owner: type = None
    ) -> Namespace | Namespace:
        """Spawn a new :class:`Namespace` object if accessing from an instance,
        otherwise return the descriptor itself.
        """
        # from class
        if instance is None:
            return self

        # from instance
        return type(self)(
            parent=self._parent,
            name=self.__name__,
            instance=instance,
            original=self._original
        )

    def __getattr__(self, name: str) -> Any:
        """Delegate attribute access to the original implementation, if one
        exists.
        """
        return getattr(self.original, name)  # delegate to original

    def __setattr__(self, name: str, value: Any) -> None:
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
        delattr(self.original, name)  # delegate to original


######################
####    METHOD    ####
######################


class InstanceMethod(VirtualAttribute):
    """A descriptor that spawns :class:`InstanceMethod <pdcast.InstanceMethod>`
    objects on access, which can be used to transform Python functions into
    self-binding instance methods.

    Parameters
    ----------
    parent : type | VirtualDescriptor
        A Python class or another
        :class:`VirtualDescriptor <pdcast.func.virtual.VirtualDescriptor>` that
        this descriptor is attached to.  Examples of descriptor chaining
        include
        :class:`NamespaceDescriptors <pdcast.func.virtual.NamespaceDescriptor>`
        and the :class:`InstanceMethods <pdcast.InstanceMethod>` that spawn from
        them.
    name : str
        The name of this descriptor as supplied to
        :func:`setattr <python:setattr>`.
    func : Callable
        A function to bind as an instance method of the attached class.

    Attributes
    ----------
    _parent : type | VirtualDescriptor
        See the ``parent`` parameter above.
    _name : str
        See the ``name`` parameter above.
    _original : Any | None
        The original attribute being masked by this descriptor, if one exists.
    __wrapped__ : Callable
        See the ``func`` parameter above.
    """

    def __get__(
        self,
        instance: Any,
        owner: type = None
    ) -> InstanceMethod:
        """See :meth:`VirtualAttribute.__get__() <pdcast.VirtualAttribute.__get__>`.
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
            parent=parent,
            name=self.__name__,
            func=self.__wrapped__,
            instance=instance,
            original=self._original
        )


############################
####    CLASS METHOD    ####
############################


class ClassMethod(VirtualAttribute):
    """A descriptor that spawns :class:`InstanceMethod <pdcast.InstanceMethod>`
    objects on access, which can be used to transform Python functions into
    self-binding instance methods.

    Parameters
    ----------
    parent : type | VirtualDescriptor
        A Python class or another
        :class:`VirtualDescriptor <pdcast.func.virtual.VirtualDescriptor>` that
        this descriptor is attached to.  Examples of descriptor chaining
        include
        :class:`NamespaceDescriptors <pdcast.func.virtual.NamespaceDescriptor>`
        and the :class:`InstanceMethods <pdcast.InstanceMethod>` that spawn from
        them.
    name : str
        The name of this descriptor as supplied to
        :func:`setattr <python:setattr>`.
    func : Callable
        A function to bind as an instance method of the attached class.

    Attributes
    ----------
    _parent : type | VirtualDescriptor
        See the ``parent`` parameter above.
    _name : str
        See the ``name`` parameter above.
    _original : Any | None
        The original attribute being masked by this descriptor, if one exists.
    __wrapped__ : Callable
        See the ``func`` parameter above.
    """

    def __get__(
        self,
        instance: Any,
        owner: type = None
    ) -> ClassMethod:
        """See :meth:`VirtualAttribute.__get__() <pdcast.VirtualAttribute.__get__>`.
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
            parent=parent,
            name=self.__name__,
            func=self.__wrapped__,
            instance=owner,
            original=self._original
        )


#############################
####    STATIC METHOD    ####
#############################


class StaticMethod(VirtualAttribute):
    "Emulate PyStaticMethod_Type() in Objects/funcobject.c"

    def __get__(
        self,
        instance: Any,
        owner: type = None
    ) -> StaticMethod:
        return self






class MyClass:

    def foo(self):
        print("Goodbye, World!")
        return self

    @classmethod
    def baz(cls):
        print("classmethod")
        return cls

    class bar:

        def __init__(self, vals):
            self._vals = vals

        def foo(self):
            print("Goodbye, World!")
            return self._vals


@attachable
def foo(data):
    print("Hello, World!")
    return data


# foo.attach_to(MyClass)
# foo.attach_to(MyClass, namespace="bar")
foo.attach_to(MyClass, namespace="bar", pattern="classmethod")
# foo.attach_to(MyClass, namespace="bar", pattern="method")


# TODO: classmethod:
# MyClass().bar.foo() returns an object, wheras
# MyClass().baz() returns a type
