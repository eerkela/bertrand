"""General installation framework for Bertrand's host dependencies.

The `install()` function accepts a series of installation steps that are expressed
as strategy objects which mutate a shared installation context, and list their
dependencies for proper ordering.  A topological order will be computed using these
dependencies, and the steps will be executed in that order, recording their progress
in an installation registry.  If any step fails, the installation process will
attempt to roll back to the previous stable state by undoing all completed steps in
reverse order, which is the same as the uninstallation procedure.
"""
from __future__ import annotations

import json
import hashlib
import re
import shutil
import uuid

from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from types import TracebackType
from typing import Any, Callable, Iterable, Literal, Protocol, TypedDict, cast

from .run import (
    LockDir,
    UserInfo,
    atomic_write_text,
    atomic_write_bytes,
    mkdir_private
)

#pylint: disable=broad-except


SANITIZE: re.Pattern[str] = re.compile(r"[^a-zA-Z0-9_.-]")


def _utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


##########################
####    JOURNALING    ####
##########################


class Operation(Protocol):
    """A type hint for a reversible operation performed as part of a step in the
    installation/uninstallation registry.  Operations can be registered using the
    `@register_op` class decorator on any class implementing this protocol.

    Methods
    -------
    apply(ctx: Context.InProgress) -> dict[str, Any] | None
        Apply the operation to the host system within an in-progress `Context` step,
        and return an optional payload describing any state needed to undo the
        operation.  Use the `ctx.do()` method to record an operation in the journal.
    undo(ctx: Context, payload: dict[str, Any]) -> None
        Undo the operation on the host system within the given `Context`, using the
        payload returned by the original `apply()` call.
    """
    # pylint: disable=missing-function-docstring
    def apply(self, ctx: Context.InProgress) -> dict[str, Any] | None: ...

    @staticmethod
    def undo(ctx: Context, payload: dict[str, Any]) -> None: ...


OP_UNDO: dict[str, Callable[[Context, dict[str, Any]], None]] = {}


def _op_kind(t: type[Operation]) -> str:
    return f"{t.__module__}.{t.__qualname__}"


def register_op(t: type[Operation]) -> type[Operation]:
    """Register an operator for use given operation kind, so that it can be
    looked up during rollback.

    Parameters
    ----------
    t : type[Operation]
        The operation class to register.

    Returns
    -------
    type[Operation]
        The same operation class that was passed in, after registering its undo
        method.

    Raises
    ------
    TypeError
        If an operation with the same class name has already been registered.
    """
    kind = _op_kind(t)
    if kind in OP_UNDO:
        raise TypeError(f"Operation must have a unique class name: {kind}")
    OP_UNDO[kind] = t.undo
    return t


@dataclass
class Context:
    """A context object that drives the installation and uninstallation of host
    dependencies.  Includes an exclusive lock and a durable record of installation
    steps that have been performed so they can be rolled back if needed.

    The log is composed of atomic steps, each of which may contain multiple mutating
    operations.  If a step is interrupted or fails, the installation process will
    replay the log in reverse order to get back to a stable state, and then resume at
    that point on the next execution attempt.

    Attributes
    ----------
    user : UserInfo
        The user information (uid/gid/name/home directory) for the user performing the
        installation.
    assume_yes : bool
        Whether to assume "yes" for all prompts during installation.
    timeout : int
        The timeout in seconds for acquiring the installation lock.  May also be used
        by operations that need to wait for external resources.
    tool : str
        A string identifying the tool writing the journal, for compatibility checks.
    schema : int
        The schema version of the journal format, for compatibility checks.
    attempt_id : str
        A UUID for the current installation attempt.
    facts : dict[str, Any]
        A scratch space for storing arbitrary facts during installation.  These will
        be loaded from completed steps in previous runs if needed, and are otherwise
        populated idempotently by steps during installation.
    """
    class Op(TypedDict):
        """Type hint for an operation taken as part of a step."""
        kind: str  # fully-qualified name of the operation class
        payload: dict[str, Any]  # operation-specific payload for undoing the operation

    class Step(TypedDict, total=False):
        """Type hint for an atomic step in the journal."""
        id: str  # individual uuid for this step
        attempt_id: str  # uuid of the installation run that created this step
        name: str  # human-readable step name
        version: int  # for backwards compatibility
        status: Literal["in_progress", "completed", "failed"]
        started_at: str  # ISO timestamp
        ended_at: str  # ISO timestamp
        provides: list[str]  # capability flags
        ops: list[Context.Op]  # ordered operations performed in this step
        backups: list[str]  # backup filenames associated with this step
        error: str  # error message if step failed

    # installer-wide config
    user: UserInfo = field(default_factory=UserInfo)
    assume_yes: bool = False
    timeout: int = 30  # seconds

    # internal metadata
    tool: str = "bertrand.install"
    schema: int = 1
    attempt_id: str = field(default_factory=lambda: uuid.uuid4().hex)

    # state
    state_dir: Path = field(init=False)
    _lock: LockDir = field(init=False, repr=False)
    _data: dict[str, Any] = field(default_factory=dict, repr=False)
    _active: str | None = field(default=None, repr=False)  # to forbid nested steps

    # scratch + notes
    # TODO: facts should persist across runs by loading from completed steps
    facts: dict[str, Any] = field(default_factory=dict, repr=False)

    def __post_init__(self) -> None:
        self.state_dir = self.user.home / ".local" / "state" / "bertrand"
        mkdir_private(self.state_dir)
        mkdir_private(self.backup_dir)
        self._lock = LockDir(path=self.state_dir / ".lock", timeout=self.timeout)

        # attempt to load existing journal if present
        if self.journal.exists():
            self._data = json.loads(self.journal.read_text(encoding="utf-8"))
            if self._data.get("schema") != self.schema or self._data.get("tool") != self.tool:
                raise OSError(
                    f"Registry schema/tool mismatch at {self.journal}. "
                    f"Found schema={self._data.get('schema')}, tool={self._data.get('tool')}."
                )
            return

        # write new journal
        self._data = {
            "schema": self.schema,
            "tool": self.tool,
            "created_at": _utc_now_iso(),
            "uid": self.user.uid,
            "user": self.user.name,
            "steps": [],
        }
        self.dump()

    def _rollback_step(self, step: Context.Step) -> None:
        # undo operations in reverse order
        for op in reversed(step.get("ops", [])):
            undo = OP_UNDO.get(op["kind"])
            if undo:
                try:
                    undo(self, op["payload"])
                except Exception:
                    pass  # best-effort

        # remove backup files associated with this step
        backups = step.get("backups", [])
        while backups:
            b = backups.pop()
            try:
                p = self.backup_dir / b
                if p.exists() and p.is_file():
                    p.unlink()
            except Exception:
                pass  # best-effort

        # mark step as failed
        step["ended_at"] = _utc_now_iso()
        step["status"] = "failed"

    def _rollback(self) -> None:
        # roll back any in-progress steps
        changed = False
        for s in reversed(self.steps):
            if s.get("status") == "in_progress":
                self._rollback_step(s)
                changed = True
        if changed:
            self.dump()

    def _clean_backups(self) -> None:
        keep: set[str] = {
            b
            for s in self.steps
            for b in (s.get("backups", []) or []) if isinstance(b, str) and b
        }
        try:
            for p in self.backup_dir.iterdir():
                if p.is_file() and p.name not in keep:
                    try:
                        p.unlink()
                    except OSError:
                        pass  # best-effort
        except OSError:
            pass  # best-effort

    def __enter__(self) -> Context:
        self._lock.__enter__()
        if self._lock.depth > 1:
            return self  # re-entrant case
        self._rollback()
        self._clean_backups()
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        self._rollback()
        self._lock.__exit__(exc_type, exc_value, traceback)

    @property
    def backup_dir(self) -> Path:
        """The directory for storing backup files during installation.

        Returns
        -------
        Path
            The backup directory path.
        """
        return self.state_dir / "backups"

    @property
    def journal(self) -> Path:
        """The path to the installation journal file.

        Returns
        -------
        Path
            The journal file path.
        """
        return self.state_dir / "journal.json"

    @property
    def steps(self) -> list[Context.Step]:
        """
        Returns
        -------
        list[Context.Step]
            A list of steps recorded in the journal.
        """
        return cast(list[Context.Step], self._data.setdefault("steps", []))

    def dump(self) -> None:
        """Write the current journal data to disk."""
        atomic_write_text(self.journal, json.dumps(self._data, indent=2, sort_keys=True) + "\n")

    def backup(self, filename: str) -> bytes | None:
        """Read backup data from a file in the backup directory.

        Parameters
        ----------
        filename : str
            The filename of the backup file relative to the backup directory.

        Returns
        -------
        bytes | None
            The raw bytes read from the backup file, or None if the file does not
            exist.

        Raises
        ------
        ValueError
            If the filename is not an immediate child of the backup directory.
        """
        p = Path(filename)
        if p.is_absolute() or len(p.parts) != 1:
            raise ValueError(
                "Backup filename must be an immediate child of the backup "
                f"directory: {filename}"
            )
        path = self.backup_dir / filename
        if not path.exists():
            return None
        return path.read_bytes()

    @dataclass
    class InProgress:
        """A context manager representing an in-progress installation step."""
        ctx: Context
        name: str
        version: int
        provides: Iterable[str]
        rec: Context.Step = field(init=False)

        def __enter__(self) -> Context.InProgress:
            if self.ctx._active is not None:
                raise OSError("nested installation steps are not supported")
            self.rec = {
                "id": uuid.uuid4().hex,
                "attempt_id": self.ctx.attempt_id,
                "name": self.name,
                "version": int(self.version),
                "status": "in_progress",
                "started_at": _utc_now_iso(),
                "provides": list(self.provides),
                "ops": [],
                "backups": [],
                "error": "",
            }
            self.ctx.steps.append(self.rec)
            self.ctx.dump()
            self.ctx._active = self.rec["id"]
            return self

        def __exit__(
            self,
            exc_type: type[BaseException] | None,
            exc_value: BaseException | None,
            traceback: TracebackType | None
        ) -> None:
            if self.ctx._active != self.rec["id"]:
                raise OSError("installation step exit does not match active step")
            try:
                if exc_type is None:
                    self.rec["status"] = "completed"
                    self.rec["ended_at"] = _utc_now_iso()
                else:
                    self.ctx._rollback_step(self.rec)
                    self.rec["error"] = f"{exc_type.__name__}: {exc_value}"
                self.ctx.dump()
            finally:
                self.ctx._active = None

        @property
        def user(self) -> str:
            """
            Returns
            -------
            str
                The name of the user performing the installation.
            """
            return self.ctx.user.name

        @property
        def uid(self) -> int:
            """
            Returns
            -------
            int
                The numeric user ID of the user performing the installation.
            """
            return self.ctx.user.uid

        @property
        def gid(self) -> int:
            """
            Returns
            -------
            int
                The numeric group ID of the user performing the installation.
            """
            return self.ctx.user.gid

        @property
        def assume_yes(self) -> bool:
            """
            Returns
            -------
            bool
                Whether to assume "yes" for all prompts during installation.
            """
            return self.ctx.assume_yes

        @property
        def timeout(self) -> int:
            """
            Returns
            -------
            int
                The timeout in seconds for acquiring the installation lock.
            """
            return self.ctx.timeout

        @property
        def state_dir(self) -> Path:
            """
            Returns
            -------
            Path
                The base directory for storing installation state.
            """
            return self.ctx.state_dir

        @property
        def attempt_id(self) -> str:
            """
            Returns
            -------
            str
                The unique identifier for the current installation attempt.
            """
            return self.ctx.attempt_id

        def __getitem__(self, key: str) -> Any:
            """Look up a fact stored in the installation context.

            Parameters
            ----------
            key : str
                The fact key to look up.

            Returns
            -------
            Any
                The value of the requested fact.
            """
            return self.ctx.facts[key]

        def do(self, op: Operation) -> None:
            """Apply an operation within this installation step, recording it in the
            journal.

            Parameters
            ----------
            op : Operation
                The operation to apply.
            """
            payload = op.apply(self)
            if payload is not None:
                self.rec.setdefault("ops", []).append({
                    "kind": _op_kind(type(op)),
                    "payload": payload
                })
                self.ctx.dump()

        def backup(self, path: Path) -> str:
            """Write backup data to a uniquely named file in the backup directory.

            Parameters
            ----------
            path : Path
                The path of the file to back up.  The raw bytes of this file will be
                loaded and written to a uniquely named backup file, which can later be
                recovered using `Context.backup()`.

            Returns
            -------
            str
                The filename of the backup file relative to the backup directory.  This
                should be stored in the operation's payload for recovery during
                `undo()`.

            Raises
            ------
            FileNotFoundError
                If the specified file does not exist, or is not a file.
            """
            if not path.exists() or not path.is_file():
                raise FileNotFoundError(f"Cannot back up non-existent file: {path}")
            filename = f"{uuid.uuid4().hex}__{SANITIZE.sub('_', path.name.strip())}"
            atomic_write_bytes(self.ctx.backup_dir / filename, path.read_bytes())
            self.rec.setdefault("backups", []).append(filename)
            self.ctx.dump()
            return filename

    def step(
        self,
        *,
        name: str,
        version: int,
        provides: Iterable[str],
    ) -> Context.InProgress:
        """Start a new installation step in the journal.

        Parameters
        ----------
        name : str
            The human-readable name of the step.
        version : int
            The version of the step, for backwards compatibility.
        provides : Iterable[str]
            The capability flags provided by this step upon successful completion.

        Returns
        -------
        Context.InProgress
            A context manager that begins the step on entry and marks it completed on
            exit, or rolls it back in case of an error.

        Raises
        ------
        OSError
            If the installation context has not been acquired as a context manager.
        """
        if self._lock.depth < 1:
            raise OSError("must acquire lock before starting an installation step")
        return self.InProgress(
            ctx=self,
            name=name,
            version=version,
            provides=provides
        )

    def completed(self, name: str, version: int) -> Context.Step | None:
        """Retrieve for the latest step with the given name and version.

        Parameters
        ----------
        name : str
            The name of the step to search for.
        version : int
            The version of the step to search for.

        Returns
        -------
        Context.Step | None
            The latest matching step, or None if not found.
        """
        for step in reversed(self.steps):
            if (
                step.get("name") == name and
                int(step.get("version", 0)) == int(version) and
                step.get("status") == "completed"
            ):
                return step
        return None

    def uninstall(self) -> None:
        """Replay all steps in the journal in reverse order to undo their effects.

        Raises
        ------
        OSError
            If the installation context has not been acquired as a context manager.
        """
        if self._lock.depth < 1:
            raise OSError("must acquire lock before uninstalling")
        for s in reversed(self.steps):
            if s.get("status") == "completed":
                self._rollback_step(s)


##############################
####    PLANNING/STEPS    ####
##############################


class Step(Protocol):
    """A type annotation describing a composable step in an installation pipeline.

    Attributes
    ----------
    name : str
        The name of the step, for identification and logging purposes.
    requires : frozenset[str]
        The capability flags required by this step.  Each step will be executed in
        strict topological order based on these dependencies.  If a required capability
        is not provided by any other step in the pipeline, an error will be raised
        before execution.
    provides : frozenset[str]
        The capability flags provided by this step upon successful installation, which
        can satisfy the `requires` of other steps.

    Methods
    -------
    __call__(ctx: Context) -> None
        Execute the step within the given installation context, mutating the host
        system as needed and recording all changes in the installation journal via
        `ctx.do()`.  A `ctx.begin_step()` and `ctx.journal.complete_step()` call will
        be inserted before and after this method to save its progress, and any
        errors raised during it will trigger a partial rollback via the journal.
    """
    # pylint: disable=missing-function-docstring
    @property
    def name(self) -> str: ...
    @property
    def version(self) -> int: ...
    @property
    def requires(self) -> frozenset[str]: ...
    @property
    def provides(self) -> frozenset[str]: ...

    def __call__(self, ctx: Context.InProgress) -> None: ...


@dataclass(frozen=True)
class Pipeline:
    """A sequence of installation steps that can be planned and executed in order.


    """
    steps: list[tuple[frozenset[str], frozenset[str], Step]]

    def plan(self) -> tuple[Step, ...]:
        """Compute a deterministic topological order from each step's requires/provides
        dependencies.

        Returns
        -------
        tuple[Step, ...]
            The ordered list of steps to execute.

        Raises
        ------
        OSError
            If the pipeline cannot proceed due to unsatisfied dependencies.
        """
        pending = list(self.steps)
        have: set[str] = set()
        order: list[Step] = []

        while pending:
            new_pending: list[Step] = []
            for step in pending:
                if step.requires.issubset(have):
                    order.append(step)
                    have.update(step.provides)
                else:
                    new_pending.append(step)

            if len(new_pending) == len(pending):
                lines = ["Pipeline stalled; unsatisfied dependencies:"]
                for s in pending:
                    missing = sorted(s.requires - have)
                    lines.append(f"  - {s.name}: missing {missing}")
                raise OSError("\n".join(lines))

            pending = new_pending

        return tuple(order)

    def install(self, ctx: Context) -> None:
        """Run the installation pipeline with the given `Context`.

        Parameters
        ----------
        ctx : Context
            The initial installation context.

        Raises
        ------
        OSError
            If the pipeline cannot proceed due to unsatisfied dependencies.
        Exception
            If any step in the installation process fails.  The type of the exception
            will depend on the underlying error.
        """
        # compute topological order
        order = self.plan()

        # acquire lock + journal context
        with ctx:
            # apply each step in topological order
            for step in order:
                if ctx.completed(step.name, step.version):
                    continue

                # run step within a new journal entry
                with ctx.step(
                    name=step.name,
                    version=step.version,
                    provides=step.provides
                ) as entry:
                    step(entry)

    def uninstall(self, ctx: Context) -> None:
        """Replay the installation journal in reverse order to uninstall all changes.

        Parameters
        ----------
        ctx : Context
            The in-flight installation context.
        """
        with ctx:
            # uninstall all completed steps
            ctx.uninstall()

    def __call__(
        self,
        *,
        requires: frozenset[str] = frozenset(),
        provides: frozenset[str] = frozenset()
    ) -> Callable[[type[Step]], type[Step]]:
        def _decorator(t: type[Step]) -> type[Step]:
            self.steps.append((requires, provides, t()))
            return t
        return _decorator


#################################
####    ATOMIC OPERATIONS    ####
#################################


@register_op
@dataclass(frozen=True)
class WriteTextFileOp:
    """An operation that writes text to a file on the host system and backs up any
    existing file as needed.

    Attributes
    ----------
    path : Path
        The path of the file to write.
    text : str
        The text content to write to the file.
    mode : int | None
        The file mode (permissions) to set on the written file, or None to leave
        unchanged.
    mkdir_parents : bool
        Whether to create parent directories as needed, by default True.
    encoding : str
        The text encoding to use when writing the file, by default "utf-8".
    newline : str | None
        The newline mode to use when writing the file, by default None (universal).
    """
    # pylint: disable=unused-argument
    path: Path = Path()
    text: str = ""
    mode: int | None = None
    mkdir_parents: bool = True
    encoding: str = "utf-8"
    newline: str | None = None

    # TODO: newline currently isn't being used.  It should either be deleted or
    # implemented properly.

    def apply(self, ctx: Context) -> dict[str, Any] | None:
        """Write the specified text to a file on the host system, backing up any
        existing file as needed, and record the action in the installation registry.

        Parameters
        ----------
        ctx : Context
            The in-flight installation context.

        Returns
        -------
        dict[str, Any] | None
            A payload describing the written file for undo purposes, or None if no
            changes were made.

        Raises
        ------
        OSError
            If the installation context is not properly initialized, or if backing up
            the existing file fails.
        """
        if not hasattr(ctx, "journal"):
            raise OSError("WriteTextFileOp requires ctx.journal to be initialized")

        # ensure parent directories exist
        p = self.path
        if self.mkdir_parents:
            p.parent.mkdir(parents=True, exist_ok=True)

        # compute new content hash
        data = self.text.encode(self.encoding)
        new_hash = hashlib.sha256(data).hexdigest()

        # check existing file and do nothing if contents already match
        existed = p.exists()
        backup: str | None = None
        old_mode: int | None = None
        old_bytes: bytes | None = None
        if existed:
            try:
                old_mode = p.stat().st_mode & 0o777
            except OSError:
                old_mode = None
            try:
                old_bytes = p.read_bytes()
            except OSError:
                old_bytes = None
            if old_bytes is not None:
                # if contents already match, only apply mode changes if needed
                if old_bytes == data:
                    mode_changed = False
                    if self.mode is not None and old_mode is not None and old_mode != self.mode:
                        try:
                            p.chmod(self.mode)
                            mode_changed = True
                        except OSError:
                            pass
                    if mode_changed:
                        return {
                            "path": str(p),
                            "existed": True,
                            "backup": None,
                            "restore_mode": old_mode,
                            "expected_size": len(data),
                            "expected_hash": new_hash,
                            "wrote": False,
                            "mode_changed": True,
                        }
                    return None

                # if contents differ, back up existing mode
                backup = ctx.write_backup(old_bytes, p.name)

        # write new contents
        atomic_write_bytes(p, data)

        # apply mode if requested (best-effort)
        if self.mode is not None:
            try:
                p.chmod(self.mode)
            except OSError:
                pass
        return {
            "path": str(p),
            "existed": existed,
            "backup": backup,  # filename in backup dir
            "restore_mode": old_mode,
            "expected_size": len(data),
            "expected_hash": new_hash,
            "wrote": True,
            "mode_changed": self.mode is not None and (old_mode is None or old_mode != self.mode),
        }

    @staticmethod
    def undo(ctx: Context, payload: dict[str, Any]) -> None:
        """Restore the backed-up file on the host system, or remove the file if it did
        not exist before.

        Parameters
        ----------
        ctx : Docker
            The in-flight Docker installation state.
        payload : dict[str, Any]
            The payload returned by the original `apply()` call.
        """
        p = Path(str(payload.get("path", "")))
        existed = bool(payload.get("existed", False))
        backup = payload.get("backup")
        restore_mode = payload.get("restore_mode")
        expected_size = payload.get("expected_size")
        expected_hash = payload.get("expected_hash")
        mode_changed = bool(payload.get("mode_changed", False))

        # helper to see if the current file still looks like the one we wrote
        def _matches_expected() -> bool:
            if not expected_hash:
                return True  # no reference; be permissive
            try:
                cur = p.read_bytes()
            except OSError:
                return False
            return (
                (expected_size is None or len(cur) == expected_size) and
                hashlib.sha256(cur).hexdigest() == expected_hash
            )

        # if we overwrote an existing file, restore it, but only if safe
        if existed:
            if backup is not None:
                backup_data = ctx.read_backup(backup)
                if backup_data is None or (p.exists() and not _matches_expected()):
                    return  # backup is empty or user has changed original file - don't clobber
                try:
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_bytes(backup_data)
                    if restore_mode is not None:
                        try:
                            p.chmod(int(restore_mode))
                        except OSError:
                            pass
                    # TODO: remove backup file?
                    # -> This requires some level of garbage collection to avoid deleting
                    # backups that may still be needed due to rollback or similar
                except OSError:
                    pass
                return

            #iIf we didn't create a backup but we did change mode only, restore mode
            if mode_changed and restore_mode is not None and p.exists():
                try:
                    p.chmod(int(restore_mode))
                except OSError:
                    pass
            return

        # file did not exist before - delete only if it still matches what we wrote
        if p.exists() and _matches_expected():
            try:
                p.unlink()
            except OSError:
                pass


@register_op
@dataclass(frozen=True)
class RemoveTreeOp:
    """An operation that removes a directory tree from the host system."""
    # pylint: disable=unused-argument
    path: Path = Path()
    ignore_errors: bool = True

    def apply(self, ctx: Context.InProgress) -> dict[str, Any] | None:
        """Remove the specified directory tree from the host system and record the
        action in the installation registry.

        Parameters
        ----------
        ctx : Context.InProgress
            The current installation step.

        Returns
        -------
        dict[str, Any] | None
            A payload describing the removed path for undo purposes, or None if the
            path did not exist.
        """
        p = self.path
        if not p.exists():
            return None
        shutil.rmtree(p, ignore_errors=self.ignore_errors)
        return {"path": str(p)}

    @staticmethod
    def undo(ctx: Context, payload: dict[str, Any]) -> None:
        """Undo the removal of a directory tree on the host system.  This operation is
        irreversible, so this method is intentionally a no-op.

        Parameters
        ----------
        ctx : Context
            The in-flight installation context.
        payload : dict[str, Any]
            The payload returned by the original `apply()` call.
        """
        return
