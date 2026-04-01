"""A configuration resource for VSCode, which is a popular text editor with robust
support for remote development and containerized environments.

This resource generates `vscode.code-workspace` artifact from a standardized
`[tool.vscode]` schema stored in project configuration.  The available options are
exhaustively listed in a self-documenting fashion, and may be customized accordingly.
"""
from __future__ import annotations

from pathlib import PosixPath

import jinja2

from ..run import WORKTREE_MOUNT
from .core import (
    Config,
    Resource,
    locate_template,
    resource,
)

# TODO: add more configuration for VSCode, such as recommended extensions and
# settings.


VSCODE_WORKSPACE_FILE: PosixPath = PosixPath(".vscode/vscode.code-workspace")


@resource("vscode", paths={VSCODE_WORKSPACE_FILE})
class VSCodeWorkspace(Resource):
    """A resource representing a VSCode managed workspace JSON file, which allows
    VSCode to attach to a running container context via the remote-containers
    extension, and mount its internal toolchain.
    """

    async def render(self, config: Config, tag: str | None) -> None:
        jinja = jinja2.Environment(
            autoescape=False,
            undefined=jinja2.StrictUndefined,
            keep_trailing_newline=True,
            trim_blocks=False,
            lstrip_blocks=False,
        )
        template = jinja.from_string(
            locate_template("core", "vscode-workspace.v1").read_text(encoding="utf-8")
        )
        target = config.worktree / VSCODE_WORKSPACE_FILE
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(template.render(
            mount_path=WORKTREE_MOUNT.as_posix(),
        ), encoding="utf-8")
