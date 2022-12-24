import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType


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
            object_type = None
            dtype = None

        # "bool[python]"
        elif backend == "python":
            object_type = bool
            dtype = np.dtype("O")

        # "bool[numpy]"
        elif backend == "numpy":
            object_type = np.bool_
            dtype = np.dtype(bool)

        # "bool[pandas]"
        elif backend == "pandas":
            object_type = np.bool_
            dtype = pd.BooleanDtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        super(BooleanType, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=self.generate_slug(backend=backend)
        )

    @classmethod
    def generate_slug(cls, backend: str = None) -> str:
        slug = f"{cls.name}"
        if backend is not None:
            slug = f"{slug}[{backend}]"
        return slug
