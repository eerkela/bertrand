import inspect



def from_caller(name: str, stack_index: int = 1):
    """Get an arbitrary object from a parent calling context."""
    # split name into '.'-separated object path
    full_path = name.split(".")
    frame = inspect.stack()[stack_index].frame

    # get first component of full path from calling context
    first = full_path[0]
    if first in frame.f_locals:
        result = frame.f_locals[first]
    elif first in frame.f_globals:
        result = frame.f_globals[first]
    elif first in frame.f_builtins:
        result = frame.f_builtins[first]
    else:
        raise ValueError(
            f"could not find {name} in calling frame at position {stack_index}"
        )

    # walk through any remaining path components
    for component in full_path[1:]:
        result = getattr(result, component)

    # return fully resolved object
    return result
