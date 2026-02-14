"""Infrastructure for building crash-safe, resumable, and automatically-logged CLI
pipelines via function decorators.  All nontrivial bertrand commands are implemented
this way outside a container context, in order to minimize impact on the host system.
"""
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
from .pipeline import (
    Atomic,
    JSONValue,
    JSONView,
    Pipeline,
    atomic,
    on_init,
    on_build,
    on_start,
    on_enter,
    on_code,
    on_run,
    on_stop,
    on_pause,
    on_resume,
    on_restart,
    on_prune,
    on_rm,
    on_ls,
    on_monitor,
    on_log,
    on_top,
    on_import,
    on_export,
    on_publish,
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
    CreateHomeDir,
    CreateUser,
    DisableLinger,
    EnableLinger,
    EnsureSubIDs,
    EnsureUserNamespaces,
    InstallSSHKey,
    LockUser,
    RemoveSSHKey,
    RemoveUserFromGroup,
    SetUserShell,
    UnlockUser,
)
