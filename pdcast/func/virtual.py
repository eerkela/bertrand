"""This module describes a mechanism for binding naked Python functions to
existing objects using virtual descriptors.

The resulting virtual attributes can be used just like normal instance methods,
and can be optionally hidden behind virtual namespaces.  If an attribute or
namespace masks an existing attribute of the object, it will be remembered, and
is accessible under the attribute's ``.original`` property.

Classes
-------
Attachable
    A mixin class for other decorators that allows them to be attached directly
    to Python objects.

VirtualDescriptor
    Base class for all virtual attribute descriptors.

VirtualAttribute
    Base class for all virtual attributes.

NamespaceDescriptor
    A descriptor that spawns :class:`Namespace <pdcast.Namespace>` objects on
    access.

Namespace
    A virtual namespace that allows for separation from the base object.

MethodDescriptor
    A descriptor that spawns :class:`VirtualMethod <pdcast.VirtualMethod>`
    objects on access.

VirtualMethod
    A method-like object that implicitly passes a class instance as the first
    argument to the decorated function.
"""
from __future__ import annotations
from functools import update_wrapper
from typing import Any, Callable

from .base import Cooperative


#####################
####    MIXIN    ####
#####################


class Attachable:
    """A mixin class that allows class-based function decorators to be attached
    to other objects as instance methods, potentially behind a virtual
    namespace.
    """

    def attach_to(
        self,
        _class: type,
        name: str | None = None,
        namespace: str | None = None
    ) -> None:
        """Attach the decorated callable to the given class definition.

        Parameters
        ----------
        _class : type
            A Python class to attach this callable to.  The callable will be
            available to all instances of the class under the given
            ``name``.
        name : str | None, default None
            The name under which to attach the decorated callable.  This is
            used as an alias when it is called from the associated class.  If
            this is left empty, the name of the decorated callable will be used
            directly.
        namespace : str | None, default None
            If given, a string name specifying a
            :class:`Namespace <pdcast.Namespace>` to attach the decorated
            callable to.  The callable will only be accessible through this
            virtual namespace.

        Notes
        -----
        This method attaches a
        :class:`MethodDescriptor <pdcast.MethodDescriptor>` to
        the associated class, which serves as a factory for
        :class:`VirtualMethod <pdcast.VirtualMethod>` objects.  These are
        callable interfaces for the decorator that this method was invoked on,
        implicitly inserting a reference to ``self`` as the first argument to
        its call signature.  In this way, the decorated function acts
        identically to a normal bound method of the attached class.

        Examples
        --------
        This method makes converting a :class:`DispatchFunc <pdcast.DispatchFunc>`
        or :class:`ExtensionFunc <pdcast.ExtensionFunc>` into a
        :class:`VirtualMethod <pdcast.VirtualMethod>` as straightforward as
        possible.

        .. doctest::

            >>> class MyClass:
            ...     def __repr__(self):  return "MyClass()"

            >>> @extension_func
            ... def foo(bar, baz=2, **kwargs):
            ...     return bar, baz

            >>> foo.attach_to(MyClass)

        This creates a new attribute of ``MyClass`` under ``MyClass.foo``,
        which references our original
        :class:`ExtensionFunc <pdcast.ExtensionFunc>`.  Whenever we invoke it
        this way, an instance of ``MyClass`` will be implicitly passed into the
        :class:`ExtensionFunc <pdcast.ExtensionFunc>` as its first argument,
        mirroring the traditional notion of ``self`` in ordinary Python.

        .. doctest::

            >>> MyClass.foo
            MyClass.foo(baz = 2, **kwargs)
            >>> MyClass().foo()
            (MyClass(), 2)

        In this case, the implicit ``self`` reference takes the place of the
        ``bar`` argument.  If we invoke ``MyClass.foo`` as a class method (i.e.
        without instantiating ``MyClass`` first), then we get the same behavior
        as the naked ``foo`` function.

        .. doctest::

            >>> MyClass.foo()
            Traceback (most recent call last):
                ...
            TypeError: foo() missing 1 required positional argument: 'bar'

        We can also modify defaults and add arguments to our
        :class:`VirtualMethod <pdcast.VirtualMethod>` just like normal.

        .. doctest::

            >>> @MyClass.foo.register_arg
            ... def baz(val: int, defaults: dict) -> int:
            ...     return int(val)

            >>> MyClass.foo.baz
            2
            >>> MyClass.foo.baz = 3
            >>> MyClass().foo()
            (MyClass(), 3)

        .. warning::

            Any arguments (as well as their default values) will be shared
            between the :class:`VirtualMethod <pdcast.VirtualMethod>` and its
            base :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

            .. doctest::

                >>> foo.baz
                3

            This might lead to unexpected changes in behavior if not properly
            accounted for.

        And can register dispatched implementations without much fuss.

        .. doctest::

            >>> @MyClass.foo.register_type(types="int")
            ... @extension_func
            ... def boolean_foo(bar: bool, baz: bool = True, **kwargs):
            ...     return (bar, baz)

            >>> foo(False)
            (False, True)

        Finally, we can hide our attribute behind a virtual
        :class:`Namespace <pdcast.Namespace>` to avoid conflicts with existing
        attributes.

        .. doctest::

            >>> foo.attach_to(MyClass, namespace="abc")
            >>> MyClass().abc.foo()
            (MyClass(), 3)
        """
        # default to decorated function name
        if not name:
            name = self._func.__name__

        # if a namespace is given, hide the function behind it.
        if namespace:
            # generate NamespaceDescriptor and attach it to class
            descriptor = NamespaceDescriptor(_class, namespace)
            setattr(_class, namespace, descriptor)

            # generate MethodDescriptor and attach it to namespace
            method_descriptor = MethodDescriptor(
                descriptor,
                name,
                self
            )
            setattr(descriptor, name, method_descriptor)

        # attach directly to class
        else:
            setattr(_class, name, MethodDescriptor(_class, name, self))


####################
####    BASE    ####
####################


class VirtualDescriptor:
    """Base class for all virtual accessors.

    These descriptors can be dynamically attached to a class definition to
    allow :class:`VirtualAttributes <pdcast.VirtualAttribute>` access to
    instances of the class as if they were bound to it.

    Parameters
    ----------
    parent : type | VirtualDescriptor
        A Python class or another
        :class:`VirtualDescriptor <pdcast.VirtualDescriptor>` that this
        descriptor is attached to.  Examples of descriptor chaining include
        :class:`NamespaceDescriptors <pdcast.NamespaceDescriptor>` and the
        :class:`VirtualMethods <pdcast.VirtualMethod>` that spawn from them.
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

    _reserved = {"_parent", "_name", "_original"}

    def __init__(
        self,
        parent: type | VirtualDescriptor,
        name: str
    ):
        self._parent = parent
        self._name = name
        self._original = getattr(self._parent, self._name, None)

    @property
    def original(self) -> Any:
        """The original attribute this descriptor is masking, if one exists."""
        # no original implementation
        if self._original is None:
            raise TypeError(
                f"{repr(self._parent)}' has no attribute '{self._name}'"
            )

        return self._original

    def detach(self) -> None:
        """Remove the attribute from the object and replace the original, if
        one exists.
        """
        if self._original is None:
            delattr(self._parent, self._name)
        else:
            setattr(self._parent, self._name, self._original)

    def __repr__(self) -> str:
        if isinstance(self._parent, type):
            return f"{self._parent.__qualname__}.{self._name}"
        return f"{repr(self._parent)}.{self._name}"


class VirtualAttribute:
    """Base class for all virtual attributes, as returned by a
    :class:`VirtualDescriptor <pdcast.func.virtual.VirtualDescriptor>` object.

    These are spawned whenever a
    :class:`VirtualDescriptor <pdcast.func.virtual.VirtualDescriptor>` is
    accessed from one of the attached class's instances.

    Parameters
    ----------
    descriptor : VirtualDescriptor
        The :class:`VirtualDescriptor <pdcast.func.virtual.VirtualDescriptor>`
        that spawned this attribute.  This contains information about the
        attribute like its name, original equivalent (if it has one), and the
        parent object that it is attached to.
    instance : Any
        An instance of the attached class from which this attribute was
        accessed.

    Attributes
    ----------
    _descriptor : VirtualDescriptor
        See the ``descriptor`` parameter above.
    _instance : Any
        See the ``instance`` parameter above.
    """

    _reserved = {"_descriptor", "_instance"}

    def __init__(
        self,
        descriptor: VirtualDescriptor,
        instance: Any
    ):
        self._descriptor = descriptor
        self._instance = instance

    def __repr__(self) -> str:
        return repr(self._descriptor)


#########################
####    NAMESPACE    ####
#########################


class NamespaceDescriptor(VirtualDescriptor):
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
        and the :class:`VirtualMethods <pdcast.VirtualMethod>` that spawn from
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
    _lookup : dict[str, pdcast.func.virtual.VirtualDescriptor]
        A dictionary mapping attributes that are associated with this
        :class:`Namespace <pdcast.Namespace>` to their respective descriptors.
    """

    _reserved = VirtualDescriptor._reserved | {"_lookup"}

    def __init__(
        self,
        parent: type,
        name: str
    ):
        super().__init__(parent=parent, name=name)
        self._lookup = {}

    def __get__(
        self,
        instance,
        owner=None
    ) -> NamespaceDescriptor | Namespace:
        """Spawn a new :class:`Namespace` object if accessing from an instance,
        otherwise return the descriptor itself.
        """
        # from class
        if instance is None:
            return self

        # from instance
        return Namespace(self, instance)

    ############################
    ####    CLASS ACCESS    ####
    ############################

    # NOTE: everything below this line is only accessible from the raw class
    # that this descriptor is attached to, not any of its instances.  Those
    # receive whole Namespaces instead.

    def __getattr__(self, name: str) -> Any:
        """Delegate attribute access to the original implementation, if one
        exists.
        """
        # name is a virtual attribute associated with this namespace
        if name in self._lookup:
            return self._lookup[name]

        # delegate to original
        return getattr(self._original, name)

    def __setattr__(self, name: str, value: Any) -> None:
        # name is internal
        if name in self._reserved:
            self.__dict__[name] = value

        # name is a virtual method to associate with this namespace
        elif isinstance(value, MethodDescriptor):
            self.__dict__["_lookup"][value._name] = value

        # delegate to original
        else:
            setattr(self.original, name, value)

    def __delattr__(self, name: str) -> None:
        if name in self._lookup:
            del self._lookup[name]

        # delegate to original
        return delattr(self.original, name)


class Namespace(VirtualAttribute):
    """A virtual name space that can be attached to arbitrary Python objects,
    and can store its own virtual attributes.

    Parameters
    ----------
    descriptor : VirtualDescriptor
        The :class:`NamespaceDescriptor <pdcast.func.virtual.NamespaceDescriptor>`
        that spawned this attribute.  This contains information about the
        namespace like its name, original equivalent (if it has one), the
        parent object that it is attached to, and a map of all the other
        :class:`VirtualAttributes <pdcast.VirtualAttribute>` that have been
        assigned to it.
    instance : Any
        An instance of the attached class from which this attribute was
        accessed.

    Attributes
    ----------
    _descriptor : VirtualDescriptor
        See the ``descriptor`` parameter above.
    _instance : Any
        See the ``instance`` parameter above.
    """

    def __init__(
        self,
        descriptor: NamespaceDescriptor,
        instance: Any
    ):
        super().__init__(descriptor=descriptor, instance=instance)

    @property
    def original(self) -> Any:
        """The original attribute this namespace is masking, if one exists."""
        return self._descriptor.original(self._instance)

    def __getattr__(self, name: str) -> Any:
        """Delegate attribute access to the original implementation, if one
        exists.
        """
        lookup = self._descriptor._lookup

        # name is a virtual attribute associated with this namespace
        if name in lookup:
            descriptor = lookup[name]

            # virtual method
            if isinstance(descriptor, MethodDescriptor):
                return VirtualMethod(
                    descriptor,
                    self._instance,
                    namespace=self
                )

            # TODO: at some point, this could be extended to support properties
            raise NotImplementedError()

        # delegate to original
        return getattr(self.original, name)

    def __setattr__(self, name: str, value: Any) -> None:
        # name is internal
        if name in self._reserved:
            self.__dict__[name] = value

        # name is a virtual method to associate with this namespace
        elif isinstance(value, MethodDescriptor):
            self.__dict__["_lookup"][name] = value

        # delegate to original
        else:
            setattr(self.original, name, value)


######################
####    METHOD    ####
######################


class MethodDescriptor(Cooperative, VirtualDescriptor):
    """A descriptor that spawns :class:`VirtualMethod <pdcast.VirtualMethod>`
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
        and the :class:`VirtualMethods <pdcast.VirtualMethod>` that spawn from
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
    _func : Callable
        See the ``func`` parameter above.
    """

    def __init__(
        self,
        parent: type | NamespaceDescriptor,
        name: str,
        func: Callable
    ):
        self._func = func
        super().__init__(parent=parent, name=name)

    def __get__(
        self,
        instance,
        owner=None
    ) -> MethodDescriptor | VirtualMethod:
        # from class
        if instance is None:
            return self

        # from instance
        return VirtualMethod(self, instance)


class VirtualMethod(Cooperative, VirtualAttribute):
    """A self-binding virtual method.

    Parameters
    ----------
    descriptor : MethodDescriptor
        The :class:`MethodDescriptor <pdcast.func.virtual.MethodDescriptor>`
        that spawned this attribute.  This contains information about the
        method like its name, original equivalent (if it has one), the
        parent object that it is attached to, and the namespace it comes from,
        if any.
    instance : Any
        An instance of the attached class from which this attribute was
        accessed.
    namespace : Namespace | None
        The namespace this method was accessed from, if one was provided to
        :meth:`Attachable.attach_to() <pdcast.func.virtual.Attachable.attach_to>`.

    Attributes
    ----------
    _descriptor : MethodDescriptor
        See the ``descriptor`` parameter above.
    _instance : Any
        See the ``instance`` parameter above.
    _func : Callable
        A reference to the function being decorated.
    """

    _reserved = VirtualAttribute._reserved | {"_namespace"}

    def __init__(
        self,
        descriptor: MethodDescriptor,
        instance: Any,
        namespace: Namespace | None = None
    ):
        self._func = descriptor._func
        super().__init__(descriptor=descriptor, instance=instance)
        self._namespace = namespace

        # NOTE: this changes the type of self._func for some reason
        # update_wrapper(self, self._func)

    @property
    def original(self) -> VirtualMethod:
        """Get the original implementation of the method, if one exists."""
        return VirtualMethod(
            descriptor=MethodDescriptor(
                parent=self._descriptor,
                name=self._descriptor._name,
                func=self._descriptor.original
            ),
            instance=self._instance
        )

    def __call__(self, *args, **kwargs):
        # from class
        if self._instance is None:
            return self._func(*args, **kwargs)

        # pipe original namespace into .original call
        if (
            self._namespace is not None and
            isinstance(self._descriptor._parent, MethodDescriptor)
        ):
            return self._func(self._namespace.original, *args, **kwargs)

        # from instance
        return self._func(self._instance, *args, **kwargs)

    def __repr__(self) -> str:
        parent = self._descriptor._parent
        if isinstance(parent, type):
            return f"{parent.__qualname__}.{self._descriptor._name}"
        return f"{repr(parent)}.{repr(self._func)}"




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


# def foo(self):
#     print("Hello, World!")
#     return self


# MyClass.foo = MethodDescriptor(MyClass, "foo", foo)


# MyClass.bar = NamespaceDescriptor(MyClass, "bar")
# MyClass.bar.foo = MethodDescriptor(MyClass.bar, "foo", foo)

# MyClass.baz = NamespaceDescriptor(MyClass, "baz")
