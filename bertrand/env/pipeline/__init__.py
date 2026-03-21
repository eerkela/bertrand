"""Infrastructure for building crash-safe, resumable, and automatically-logged CLI
pipelines via function decorators.  All nontrivial bertrand commands are implemented
this way outside a container context, in order to minimize impact on the host system.
"""
from .core import (
    Atomic,
    JSONValue,
    Pipeline,
    atomic,
    on_init,
    on_publish,
    on_enter,
    on_code,
    on_restart,
)
from .filesystem import (
    Chmod,
    Chown,
    Copy,
    Extract,
    Mkdir,
    Move,
    Remove,
    Stash,
    Swap,
    Symlink,
    Touch,
    WriteBytes,
    WriteText,
)
from .network import Download
from .package import (
    AddRepository,
    DetectPackageManager,
    InstallCACert,
    InstallPackage,
    UninstallPackage,
    detect_package_manager,
)
from .systemd import (
    DelegateUserControllers,
    DisableService,
    EnableService,
    ReloadDaemon,
    RestartService,
    StartService,
    StopService,
)
from .user import (
    AddUserToGroup,
    CreateGroup,
    DisableLinger,
    EnableLinger,
    EnsureSubIDs,
    EnsureUserNamespaces,
    InstallSSHKey,
    RemoveSSHKey,
    RemoveUserFromGroup,
)
