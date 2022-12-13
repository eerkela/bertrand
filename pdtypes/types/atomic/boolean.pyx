import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType, shared_registry


##########################
####    SUPERTYPES    ####
##########################


class BooleanType(AtomicType):
    """Boolean supertype."""

    _name = "bool"
    _backends = ("python", "numpy", "pandas")
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

    def __init__(self, backend: str):
        slug = f"{self._name}"

        # "bool"
        if backend is None:
            object_type = None
            dtype = None

        # "bool[python]"
        elif backend == "python":
            slug += f"[{backend}]"
            object_type = bool
            dtype = np.dtype("O")

        # "bool[numpy]"
        elif backend == "numpy":
            slug += f"[{backend}]"
            object_type = np.bool_
            dtype = np.dtype(bool)

        # "bool[pandas]"
        elif backend == "pandas":
            slug += f"[{backend}]"
            object_type = np.bool_
            dtype = pd.BooleanDtype()

        # unrecognized
        else:
            raise TypeError(f"{slug} backend not recognized: {repr(backend)}")

        super(BooleanType, self).__init__(
            backend=backend,
            object_type=object_type,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=slug
        )
