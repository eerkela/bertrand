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

BoundMethod
    A method-like descriptor that implicitly passes a class instance as the
    first argument to the decorated function.
"""
from __future__ import annotations
from types import MappingProxyType, MethodType
from typing import Any, Callable
from weakref import WeakKeyDictionary

from .base import BaseDecorator


######################
####    PUBLIC    ####
######################


def attachable(func: Callable) -> Callable:
    """A decorator that allows naked Python functions to be dynamically
    attached to class objects at run time.

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
        in a variety of ways.

    See Also
    --------
    Attachable.attach_to :
        bind the decorated function to an external class.
    Attachable.attached :
        a map of all the classes that this function has been attached to.
    """
    return Attachable(func)


#######################
####    PRIVATE    ####
#######################


class Attachable(BaseDecorator):
    """Holds implementation for :func:`@attachable <pdcast.attachable>`."""

    _reserved = BaseDecorator._reserved | {"_attached"}

    def __init__(self, func: Callable):
        super().__init__(func=func)
        self._attached = WeakKeyDictionary()

    @property
    def attached(self) -> MappingProxyType:
        """A mapping of all the classes that this function has been attached
        to.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping class objects to their associated
            descriptors.
        """
        return MappingProxyType(dict(self._attached))  # {class: descriptor}

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
            callable under.  The callable will only be accessible through this
            virtual namespace.
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
        :class:`BoundMethod <pdcast.BoundMethod>` descriptor to the parent
        class, which is a cooperative decorator for the original function.
        When the descriptor is accessed from an instance of the class, its
        :meth:`__get__() <python:object.get>` method is invoked, which binds
        the instance to the method.  Then, when the method is called, the
        instance is implicitly passed as the first argument of the function.
        This is exactly identical to Python's ordinary
        :ref:`instance binding <python:descriptorhowto>` mechanism for methods
        and other attributes.

        If a ``namespace`` is given, then the process is somewhat different.
        Rather than attaching a :class:`BoundMethod <pdcast.BoundMethod>` to
        the class directly, we first attach a
        :class:`Namespace <pdcast.Namespace>` descriptor instead.  We then add
        our :class:`BoundMethods <pdcast.BoundMethod>` to this
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
        valid_patterns = ("property", "method", "classmethod")
        if pattern not in valid_patterns:
            raise ValueError(
                f"`pattern` must be one of {valid_patterns}, not "
                f"{repr(pattern)}"
            )

        # check func is not already attached to class
        if class_ in self._attached:
            existing = self._attached[class_]
            raise AttributeError(
                f"'{self.__qualname__}' is already attached to "
                f"'{class_.__qualname__}' as '{existing.__qualname__}'"
            )

        parent = class_

        # (optionally) generate namespace
        if namespace:
            existing = getattr(class_, namespace, None)
            if isinstance(existing, Namespace):
                parent = existing
            else:
                parent = Namespace(
                    parent=class_,
                    name=namespace,
                    attrs={},
                    instance=None,
                    original=getattr(parent, namespace, None)
                )
                setattr(class_, parent.__name__, parent)

        # generate pattern-specific descriptor
        if pattern == "property":
            raise NotImplementedError()
        elif pattern == "method":
            descriptor = BoundMethod(
                parent=parent,
                name=name,
                func=self,
                instance=None,
                original=getattr(parent, name, None)
            )
        else:
            raise NotImplementedError()

        # attach descriptor
        setattr(parent, name, descriptor)
        self._attached[class_] = descriptor


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
        and the :class:`BoundMethods <pdcast.BoundMethod>` that spawn from
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
    _attrs : dict[str, pdcast.func.virtual.VirtualDescriptor]
        A dictionary mapping attributes that are associated with this
        :class:`Namespace <pdcast.Namespace>` to their respective descriptors.
    """

    _reserved = {
        "_parent", "_original", "_attrs", "__name__", "__qualname__",
        "__self__"
    }

    def __init__(
        self,
        parent: type,
        name: str,
        attrs: dict,
        instance: Any | None,
        original: Any | None
    ):
        self._parent = parent
        self.__self__ = instance
        self.__name__ = name
        self.__qualname__ = f"{self._parent.__qualname__}.{self.__name__}"
        self._attrs = attrs
        self._original = original

    @property
    def attached(self) -> MappingProxyType:
        """TODO"""
        return MappingProxyType(self._attrs)

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
        for _, attr in self._attrs.items():
            attr.detach()

        # replace original implementation
        if self._original is None:
            delattr(self._parent, self.__name__)
        else:
            setattr(self._parent, self.__name__, self._original)

    def __get__(
        self,
        instance,
        owner=None
    ) -> Namespace | Namespace:
        """Spawn a new :class:`Namespace` object if accessing from an instance,
        otherwise return the descriptor itself.
        """
        # from class
        if instance is None:
            return self

        # from instance
        return Namespace(
            parent=self._parent,
            name=self.__name__,
            attrs=self._attrs,
            instance=instance,
            original=self._original
        )

    def __getattr__(self, name: str) -> Any:
        """Delegate attribute access to the original implementation, if one
        exists.
        """
        # name is a virtual attribute associated with this namespace
        if name in self._attrs:
            unbound = self._attrs[name]
            return BoundMethod(
                parent=self,
                name=unbound.__name__,
                func=unbound.__wrapped__,
                instance=self.__self__,
                original=unbound._original
            )

        # delegate to original
        return getattr(self._original, name)

    def __setattr__(self, name: str, value: Any) -> None:
        # name is internal
        if name in self._reserved:
            self.__dict__[name] = value

        # name is a virtual method to associate with this namespace
        elif isinstance(value, BoundMethod):
            self.__dict__["_attrs"][value.__name__] = value

        # delegate to original
        else:
            setattr(self.original, name, value)

    def __delattr__(self, name: str) -> None:
        # check if name is a virtual attribute of this namespace
        if name in self._attrs:
            del self._attrs[name]

        # delegate to original
        else:
            delattr(self.original, name)


######################
####    METHOD    ####
######################


class BoundMethod(BaseDecorator):
    """A descriptor that spawns :class:`BoundMethod <pdcast.BoundMethod>`
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
        and the :class:`BoundMethods <pdcast.BoundMethod>` that spawn from
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
        """Get the original implementation of the method, if one exists."""
        if self._original is None:
            raise AttributeError(
                f"'{self._parent.__qualname__}' has no attribute "
                f"'{self.__name__}'"
            )

        # from namespace
        if isinstance(self._parent, Namespace):
            return MethodType(self._original, self._parent.original)

        # from class
        if self.__self__ is None:
            return self._original

        # from instance
        return MethodType(self._original, self.__self__)

    def detach(self) -> None:
        """Remove the attribute from the object and replace the original, if
        one exists.
        """
        # replace original implementation
        if self._original is None or isinstance(self._parent, Namespace):
            delattr(self._parent, self.__name__)
        else:
            setattr(self._parent, self.__name__, self._original)

        # delete namespace if empty
        if isinstance(self._parent, Namespace):
            class_ = self._parent._parent
            if not self._parent._attrs:
                self._parent.detach()
        else:
            class_ = self._parent

        # remove from Attachable.attached
        del self.__wrapped__._attached[class_]

    def __call__(self, *args, **kwargs):
        # from class
        if self.__self__ is None:
            return self.__wrapped__(*args, **kwargs)

        # from instance
        return self.__wrapped__(self.__self__, *args, **kwargs)

    def __get__(
        self,
        instance,
        owner=None
    ) -> BoundMethod:
        # from class
        if instance is None:
            return self

        # from instance
        return BoundMethod(
            parent=self._parent,
            name=self.__name__,
            func=self,
            instance=instance,
            original=self._original
        )


############################
####    CLASS METHOD    ####
############################


# class ClassMethod(object):
#     "Emulate PyClassMethod_Type() in Objects/funcobject.c"
#     def __init__(self, f):
#         self.f = f

#     def __get__(self, obj, klass=None):
#         if klass is None:
#             klass = type(obj)
#         def newfunc(*args):
#             return self.f(klass, *args)
#         return newfunc


#############################
####    STATIC METHOD    ####
#############################


# class StaticMethod(object):
#     "Emulate PyStaticMethod_Type() in Objects/funcobject.c"
#     def __init__(self, f):
#         self.f = f

#     def __get__(self, obj, objtype=None):
#         return self.f





# class MyClass:

#     def foo(self):
#         print("Goodbye, World!")
#         return self

#     class bar:

#         def __init__(self, vals):
#             self._vals = vals

#         def foo(self):
#             print("Goodbye, World!")
#             return self._vals


# @attachable
# def foo(self):
#     print("Hello, World!")
#     return self


# # foo.attach_to(MyClass)
# foo.attach_to(MyClass, namespace="bar")
# MyClass.bar.foo.detach()  # TODO: this drops foo to MyClass rather than MyClass.bar


# MyClass.foo  # MethodDescriptor
# MyClass().foo  # BoundMethod
# MyClass.foo.original  # function
# MyClass().foo.original  # method

# MyClass.bar  # NamespaceDescriptor
# MyClass().bar  # Namespace
# MyClass.bar.original  # type
# MyClass().bar.original  # MyClass.bar

# MyClass.bar.foo  # MethodDescriptor
# MyClass().bar.foo  # BoundMethod
# MyClass.bar.foo.original  # function
# MyClass().bar.foo.original  # method
