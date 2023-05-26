"""This module controls instance creation and identification for
:class:`ScalarType <pdcast.ScalarType>` objects.
"""
from pdcast.util.structs cimport LRUDict

from .scalar cimport ScalarType


##############################
####    IDENTIFICATION    ####
##############################


cdef class SlugFactory:
    """An interface for creating string representations of a type based on its
    base name and parameters.
    """

    def __init__(self, str name, tuple parameters):
        self.name = name
        self.parameters = parameters

    def __call__(self, tuple args, dict kwargs) -> str:
        """Construct a string representation with the given *args, *kwargs."""
        cdef object args_iter
        cdef object ordered
        cdef str params

        args_iter = iter(args)
        ordered = (
            str(kwargs[param]) if param in kwargs else str(next(args_iter))
            for param in self.parameters
        )
        params = ", ".join(ordered)
        if not params:
            return self.name
        return f"{self.name}[{params}]"


cdef class BackendSlugFactory:
    """A SlugFactory that automatically appends a type's backend specifier as
    the first parameter of the returned slug.
    """

    def __init__(self, str name, tuple parameters, str backend):
        super().__init__(name, parameters)
        self.backend = backend

    def __call__(self, tuple args, dict kwargs) -> str:
        """Construct a string representation with the given *args, *kwargs."""
        cdef object args_iter
        cdef object ordered
        cdef str params

        args_iter = iter(args)
        ordered = (
            str(kwargs[param]) if param in kwargs else str(next(args_iter))
            for param in self.parameters
        )
        params = ", ".join(ordered)
        if not params:
            return f"{self.name}[{self.backend}]"
        return f"{self.name}[{self.backend}, {params}]"


#############################
####    INSTANTIATION    ####
#############################


cdef class InstanceFactory:
    """An interface for controlling instance creation for
    :class:`ScalarType <pdcast.ScalarType>` objects.
    """

    def __init__(self, type base_class):
        self.base_class = base_class

    def __call__(self, *args, **kwargs):
        raise self.base_class(*args, **kwargs)


cdef class FlyweightFactory(InstanceFactory):
    """An InstanceFactory that implements the flyweight caching strategy."""

    def __init__(
        self,
        type base_class,
        SlugFactory slugify,
        int cache_size
    ):
        super().__init__(base_class)
        self.slugify = slugify
        if cache_size < 0:
            self.instances = {}
        else:
            self.instances = LRUDict(maxsize=cache_size)

    def __call__(self, *args, **kwargs) -> ScalarType:
        cdef str slug
        cdef ScalarType instance

        slug = self.slugify(args, kwargs)
        instance = self.instances.get(slug, None)
        if instance is None:
            instance = self.base_class(*args, **kwargs)
            self.instances[slug] = instance
        return instance

    def __repr__(self) -> str:
        return repr(self.instances)

    def __getitem__(self, str key) -> ScalarType:
        return self.instances[key]

    def __setitem__(self, str key, ScalarType value) -> None:
        self.instances[key] = value
