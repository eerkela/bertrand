"""Render VSCode workspace configuration.

This resource generates an ephemeral VSCode workspace artifact for development
containers.  The artifact is rendered inside the container context and is not written
to the project worktree.
"""

from __future__ import annotations

import jinja2

from bertrand.env.git import (
    CONTAINER_ARTIFACT_MOUNT,
    WORKTREE_MOUNT,
    atomic_write_text,
)

from .core import (
    Config,
    Resource,
    locate_template,
    resource,
)

# TODO: add more configuration for VSCode, such as recommended extensions and
# settings.


VSCODE_WORKSPACE_FILE = CONTAINER_ARTIFACT_MOUNT / "vscode.code-workspace"


@resource("vscode")
class VSCodeWorkspace(Resource):
    """Render a VSCode managed workspace artifact.

    The workspace lets VSCode attach to a running container context through the
    remote-containers extension and mount its internal toolchain without writing
    editor state into the source worktree.
    """

    async def render(self, config: Config, *, image_build: bool) -> None:
        """Render the workspace artifact inside image/dev contexts."""
        if not image_build:
            return
        del config
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
        atomic_write_text(
            VSCODE_WORKSPACE_FILE,
            template.render(
                artifact_path=CONTAINER_ARTIFACT_MOUNT.as_posix(),
                mount_path=WORKTREE_MOUNT.as_posix(),
            ),
            encoding="utf-8",
        )
