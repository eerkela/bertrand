"""In-cluster editor-open requests for Bertrand development sessions."""

from __future__ import annotations

import os
import shutil
import sys
import time
import uuid

from bertrand.env.config.bertrand import Bertrand, Editor
from bertrand.env.config.core import Config
from bertrand.env.config.vscode import (
    VSCODE_MCP_FILE,
    VSCODE_WORKSPACE_FILE,
    VSCodeWorkspace,
)
from bertrand.env.git import (
    PROJECT_MOUNT,
    REPO_ID_ENV,
    WORKTREE_ENV,
    WORKTREE_MOUNT,
    GitRepository,
    inside_container,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.dev.mailbox import (
    CODE_OPEN_RESOURCE,
    CodeOpenRecord,
    CodeOpenSpec,
    code_open_request_labels,
    code_open_request_name,
    wait_code_open_request,
)
from bertrand.env.kube.dev.session import (
    DEV_HOST_ID_ENV,
    DEV_POD_NAME_ENV,
    DEV_PRIMARY_CONTAINER_ENV,
    DEV_SESSION_ENV,
)

CODE_OPEN_TIMEOUT: float = 30.0


async def send_code_open_request(
    *,
    block: bool,
    editor: Editor | None = None,
    deadline: float | None = None,
) -> None:
    """Create a mailbox request and wait for the host bridge response.

    Parameters
    ----------
    block : bool, optional
        Whether the host bridge should preserve editor lifetime semantics.
    editor : Editor | None, optional
        Optional editor alias override.
    deadline : float | None, optional
        Unix timestamp deadline for the request. Defaults to thirty seconds from now.

    Raises
    ------
    TimeoutError
        If the request deadline expires before completion.
    """
    deadline = deadline if deadline is not None else time.time() + CODE_OPEN_TIMEOUT
    spec, host_id = await _request_spec(
        block=block,
        editor=editor,
        deadline=deadline,
    )
    remaining = deadline - time.time()
    if remaining <= 0:
        msg = "deadline exhausted before editor request could be submitted"
        raise TimeoutError(msg)
    with Kube.inside_cluster() as kube:
        record = await CODE_OPEN_RESOURCE.create(
            kube,
            name=code_open_request_name(spec.session_id, spec.request_id),
            spec=spec,
            labels=code_open_request_labels(spec, host_id),
            timeout=remaining,
        )
        terminal = await wait_code_open_request(
            kube,
            name=record.name,
            timeout=max(0.001, deadline - time.time()),
        )
    _raise_if_unsuccessful(terminal)


async def _request_spec(
    *,
    block: bool,
    editor: Editor | None,
    deadline: float,
) -> tuple[CodeOpenSpec, str]:
    if not inside_container():
        msg = (
            "`bertrand code` requires a live Bertrand dev Pod context. Run "
            "`bertrand enter` first."
        )
        raise RuntimeError(msg)
    session_id = _required_env(DEV_SESSION_ENV)
    host_id = _required_env(DEV_HOST_ID_ENV)
    repo_id = _required_env(REPO_ID_ENV)
    worktree = _required_env(WORKTREE_ENV)
    pod_name = _required_env(DEV_POD_NAME_ENV)
    primary_container = _required_env(DEV_PRIMARY_CONTAINER_ENV)

    with Kube.inside_cluster() as kube:
        async with await Config.load(
            WORKTREE_MOUNT,
            kube=kube,
            repo=GitRepository(git_dir=PROJECT_MOUNT / ".git"),
        ) as config:
            config.resources[VSCodeWorkspace.name] = None
            await config.sync(image_build=True)
            bertrand = config.get(Bertrand)
            if bertrand is None:
                msg = (
                    f"Bertrand configuration is missing from the worktree config "
                    f"at {WORKTREE_MOUNT}."
                )
                raise RuntimeError(msg)
            resolved_editor = editor or bertrand.editor
            _request_prereqs(resolved_editor)

    return (
        CodeOpenSpec(
            session_id=session_id,
            request_id=uuid.uuid4().hex,
            repo_id=repo_id,
            worktree=worktree,
            pod_name=pod_name,
            container_name=primary_container,
            workspace_path=VSCODE_WORKSPACE_FILE.as_posix(),
            editor=resolved_editor,
            block=block,
            deadline=deadline,
        ),
        host_id,
    )


def _request_prereqs(editor: Editor) -> None:
    if editor != "vscode":
        msg = f"unsupported editor for code.open mailbox request: {editor}"
        raise ValueError(msg)
    missing = [
        path
        for path in (VSCODE_WORKSPACE_FILE, VSCODE_MCP_FILE)
        if not path.exists() or not path.is_file()
    ]
    if missing:
        rendered = ", ".join(path.as_posix() for path in missing)
        msg = (
            "VS Code editor artifact(s) not found at expected container path(s): "
            f"{rendered}. Try re-running `bertrand code` after "
            "refreshing internal config artifacts."
        )
        raise RuntimeError(msg)
    for tool, hint in (
        ("clangd", "C/C++ language features may be degraded in this editor session."),
        (
            "ruff",
            "Python linting/formatting features may be degraded in this editor "
            "session.",
        ),
        ("ty", "Python type-checking/language-service features may be degraded."),
        ("pytest", "Python test discovery/execution features may be degraded."),
        ("bertrand-mcp", "MCP server integration may be unavailable."),
    ):
        if shutil.which(tool) is None:
            print(
                f"bertrand: could not locate tool {tool!r} inside dev Pod\n\t{hint}",
                file=sys.stderr,
            )


def _raise_if_unsuccessful(record: CodeOpenRecord) -> None:
    if record.status.phase == "Succeeded":
        return
    detail = record.status.message or f"editor request ended in {record.status.phase}"
    if record.status.phase == "Expired":
        raise TimeoutError(detail)
    raise RuntimeError(detail)


def _required_env(name: str) -> str:
    value = os.environ.get(name, "").strip()
    if value:
        return value
    msg = (
        f"required dev-session environment variable {name!r} is missing. Run "
        "`bertrand enter` first."
    )
    raise RuntimeError(msg)
