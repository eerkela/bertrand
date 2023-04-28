from .base import BaseDecorator
from .dispatch import dispatch, DispatchDict, DispatchFunc
from .extension import extension_func, no_default, ExtensionFunc
from .attachable import (
    attachable, Attachable, ClassMethod, InstanceMethod, Namespace, Property,
    StaticMethod, VirtualAttribute
)
