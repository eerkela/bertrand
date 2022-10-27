import inspect


def delegates(to=None, keep=False):
    """Decorator: replace `**kwargs` in signature with params from `to`.

    Adapted from:
        https://www.fast.ai/posts/2019-08-06-delegation.html
    """

    def wrapper(code):
        if to is None:
            to_f, from_f = code.__base__.__init__, code.__init__
        else:
            to_f, from_f = to , code

        # get signature of passed code object
        sig = inspect.signature(from_f)
        sig_map = dict(sig.parameters)
        k = sig_map.pop('kwargs')
        sig2 = {k: v for k, v in inspect.signature(to_f).parameters.items()
              if v.default != inspect.Parameter.empty and k not in sig_map}
        sig_map.update(sig2)
        if keep:
            sig_map['kwargs'] = k

        # replace signature
        from_f.__signature__ = sig.replace(parameters=sig_map.values())
        return code

    return wrapper
