from pdcast.util.structs cimport LRUDict

from . cimport scalar


cdef class InstanceFactory:
    """Interface for controlling instance creation for
    :class:`ScalarType <pdcast.ScalarType>` objects.
    """

    def __init__(self, type base_class):
        self.base_class = base_class

    def __call__(self, *args, **kwargs) -> scalar.ScalarType:
        """Create a new instance of the """
        raise NotImplementedError(f"{type(self)} does not implement __call__")

    def __str__(self) -> str:
        return repr(self)


cdef class FlyweightFactory(InstanceFactory):
    """An InstanceFactory that caches instances according to the flyweight
    pattern.
    """

    def __init__(self, type base_class, unsigned int cache_size):
        super().__init__(base_class)
        if not cache_size:
            self.instances = {}
        else:
            self.instances = LRUDict(maxsize=cache_size)

    def __call__(self, *args, **kwargs) -> scalar.ScalarType:
        """"""
        cdef str slug
        cdef scalar.ScalarType result

        slug = self.base_class.slugify(*args, **kwargs)
        result = self.instances.get(slug, None)
        if result is None:  # create new flyweight
            result = self.base_class(*args, **kwargs)
            self.instances[slug] = result

        return result

    def __repr__(self) -> str:
        return f"{type(self).__name__}({self.instances})"


cdef class NoInstanceFactory(InstanceFactory):
    """An InstanceFactory that passes to the normal constructor."""

    def __call__(self, *args, **kwargs) -> scalar.ScalarType:
        return self.base_class(*args, **kwargs)

    def __repr__(self) -> str:
        return f"{type(self).__name__}()"
