import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType, null


##########################
####    SUPERTYPES    ####
##########################


class BooleanType(AtomicType):
    """Boolean supertype."""

    name = "bool"
    aliases = {
        # type
        bool: {},
        np.bool_: {"backend": "numpy"},

        # dtype
        np.dtype(np.bool_): {"backend": "numpy"},
        pd.BooleanDtype(): {"backend": "pandas"},

        # string
        "bool": {},
        "boolean": {},
        "bool_": {},
        "bool8": {},
        "b1": {},
        "?": {},
        "Boolean": {"backend": "pandas"},
    }
    _backends = (None, "python", "numpy", "pandas")

    def __init__(self, backend: str = None):
        # "bool"
        if backend is None:
            typedef = None
            dtype = None

        # "bool[python]"
        elif backend == "python":
            typedef = bool
            dtype = np.dtype("O")

        # "bool[numpy]"
        elif backend == "numpy":
            typedef = np.bool_
            dtype = np.dtype(bool)

        # "bool[pandas]"
        elif backend == "pandas":
            typedef = np.bool_
            dtype = pd.BooleanDtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        super(BooleanType, self).__init__(
            backend=backend,
            typedef=typedef,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=self.slugify(backend=backend)
        )

    @classmethod
    def slugify(cls, backend: str = None) -> str:
        slug = f"{cls.name}"
        if backend is not None:
            slug = f"{slug}[{backend}]"
        return slug

    def parse(self, input_str: str):
        lower = input_str.lower()
        if lower not in ("true", "false"):
            raise TypeError("could not interpret boolean string: {input_str}")
        return self.typedef(lower == "true")

    def replace(self, backend=null) -> AtomicType:
        if backend is null:
            backend = self.backend
        return self.instance(backend=backend)
