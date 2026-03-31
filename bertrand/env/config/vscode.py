"""TODO"""
from __future__ import annotations

import jinja2

from .core import (
    VSCODE_WORKSPACE_FILE,
    Config,
    Resource,
    locate_template,
    resource,
)
from ..run import WORKTREE_MOUNT


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
