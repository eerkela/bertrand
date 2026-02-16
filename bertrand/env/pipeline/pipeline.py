"""A general-purpose pipeline framework used to implement Bertrand's CLI commands in
a reversible and maintainable manner.

Each pipeline represents a series of reversible steps, which consist of atomic
operations that can be rolled back in case of failure, or as part of an uninstallation
procedure.  Steps can require other steps as dependencies; the pipeline will precompute
a topological ordering to execute them in the correct sequence.  Steps will also be
recorded in a persistent journal file so that they can be resumed or rolled back
between runs and invalidated after changes to their explicit contract versions, inputs,
or dependencies.
"""
from __future__ import annotations

import json
import hashlib
import re
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from types import MappingProxyType, TracebackType
from typing import (
    Any,
    Callable,
    Iterable,
    Iterator,
    Literal,
    Protocol,
    TypeAlias,
    overload,
)

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    PositiveInt,
    TypeAdapter,
    ValidationError,
    model_validator,
)
from typing_extensions import Annotated

from ..run import LockDir, User, atomic_write_text, mkdir_private


#pylint: disable=broad-except
SYNTAX: int = 1
STATE_DIR = User().home / ".local" / "share" / "bertrand" / "pipelines"
MISSING: Literal["<missing>"] = "<missing>"  # not a valid SHA-256 hexdigest
ATOMIC_UNDO: dict[str, Callable[[Pipeline.InProgress, dict[str, JSONValue], bool], None]] = {}
QualName = Annotated[str, Field(pattern=r"^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$")]
UUID4Hex = Annotated[str, Field(pattern=r"^[a-f0-9]{32}$")]
Hash256 = Annotated[str, Field(pattern=r"^[a-f0-9]{64}$")]
JSONValue: TypeAlias = (
    None
    | bool
    | int
    | float
    | str
    | list["JSONValue"]
    | dict[str, "JSONValue"]
)
JSONView: TypeAlias = (
    None
    | bool
    | int
    | float
    | str
    | tuple["JSONView", ...]
    | MappingProxyType[str, "JSONView"]
)


def _qualname(x: Any) -> str:
    return f"{x.__module__}.{x.__qualname__}"


class Atomic(Protocol):
    """A type hint for a reversible operation performed as part of a
    `Pipeline.InProgress` step.  Operations can be registered using the `@atomic` class
    decorator on any class implementing this protocol.
    """

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        """Apply the operation within an in-progress `Pipeline` step and record a
        payload describing any state needed to undo the operation.

        Parameters
        ----------
        ctx : Pipeline.InProgress
            The current pipeline step.  Indexing the step like a dictionary will yield
            any facts that have been recorded so far, which are immutable.
        payload : dict[str, JSONValue]
            A mutable dictionary that can be populated with any JSON-serializable state
            needed to undo the operation later.  This is appended to the step's list of
            operations before `do()` is called, so it can be modified in-place and
            `ctx.dump()` can be called to persist it to the journal within the
            operation itself, for additional crash safety.
        """

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        """Undo the operation within the given `Pipeline`, using the payload populated
        by the original `do()` call.

        Parameters
        ----------
        ctx : Pipeline.InProgress
            The current pipeline step.  Indexing the step like a dictionary will yield
            any facts that have been recorded so far, which are immutable.
        payload : dict[str, JSONValue]
            The payload populated by the original `do()` call.
        force : bool
            If True, operations may suppress errors and continue a best-effort undo.
        """


def atomic(t: type[Atomic]) -> type[Atomic]:
    """Register an operator for use given operation kind, so that it can be
    looked up during rollback.

    Parameters
    ----------
    t : type[Atomic]
        The operation class to register.

    Returns
    -------
    type[Atomic]
        The same operation class that was passed in, after registering its undo
        method.

    Raises
    ------
    TypeError
        If an operation with the same class name has already been registered.
    """
    kind = _qualname(t)
    if kind in ATOMIC_UNDO:
        raise TypeError(f"Atomic operation must have a unique class name: {kind}")
    ATOMIC_UNDO[kind] = t.undo
    return t


@dataclass
class Pipeline:
    """A reversible sequence of steps that can be planned and executed in order.

    Attributes
    ----------
    state_dir : Path
        The base directory for storing pipeline state, including the lock, journal,
        and any other files stored by one of its operations.
    timeout : int, optional
        The timeout in seconds for acquiring the pipeline lock.  May also be used by
        operations that need to wait for external resources.  Defaults to 30 seconds.
    keep : int, optional
        The number of previous attempts to keep in the journal for each step.  Defaults
        to 3.

    Notes
    -----
    Instances of this class can be used both as function decorators and context
    managers.

    When used as a function decorator, the `__call__()` method will register new
    steps by appending a decorated function to the pipeline's internal list, which will
    then be topologically sorted by their capabilities and executed when the `run()`
    method is invoked.  Decorators cannot be applied while the pipeline context is
    active.

    When used as a context manager, the pipeline will acquire an exclusive lock on a
    user-defined state directory, and load or create a persistent journal to determine
    which steps have already been completed, as well as roll back those that were
    interrupted or failed.  When the `run()` method is invoked, it will begin by
    acquiring this context and executing any incomplete steps with nested `InProgress`
    contexts, which record operations in the journal and complete the step on exit.  If
    the step function raises an error during execution, then the recorded operations
    will be replayed in reverse order to roll the pipeline back to a previous state
    before propagating the error.

    Steps are always ordered according to their `requires` capabilities, with ties
    broken by their registration order.  If a step is added/removed or its explicit
    contract version is changed, then all dependent steps will be rolled back and
    re-executed on the next run.  The `undo()` method also allows users to roll back
    steps by dependency, or all at once, by replaying the journal in reverse order.
    """
    class Function(Protocol):
        """A type hint for a function that can be decorated as a pipeline step."""
        __qualname__: str
        __module__: str
        def __call__(self, ctx: Pipeline.InProgress) -> None: ...

    @dataclass(frozen=True)
    class Target:
        """An entry in the pipeline, along with its dependency information.

        Attributes
        ----------
        func : Pipeline.Function
            The function to execute for this step, which accepts an in-progress
            context and uses it to record mutating operations in the journal.  The
            function's fully-qualified (dotted) name will be used as the step name, and
            must be unique within the pipeline.
        version : PositiveInt
            The explicit contract version for this step.  Trivially set to 1 and
            ignored for ephemeral steps.
        ephemeral : bool
            If False, the step's effects will be recorded in the journal and persisted
            between runs (subject to invalidation).  Otherwise, the step will always be
            rolled back at the end of the pipeline run, even if it completed
            successfully.  Persistent steps must never depend on ephemeral steps.
        requires : frozenset[Pipeline.Target]
            The set of previous targets which must be completed before this step can
            run.  Each step will be executed in strict topological order based on these
            dependencies.  If a requirement is not present in the pipeline, then an
            error will be raised during registration.
        """
        func: Pipeline.Function
        version: PositiveInt
        ephemeral: bool
        requires: frozenset[Pipeline.Target]

        def __str__(self) -> str:
            return _qualname(self.func)

        def __hash__(self) -> int:
            return id(self.func)

        def __eq__(self, other: Any) -> bool:
            if isinstance(other, Pipeline.Target):
                return self.func is other.func
            return NotImplemented

    class OpRecord(BaseModel):
        """JSON representation for an atomic operation taken as part of a step.

        Attributes
        ----------
        kind : QualName
            The fully-qualified (dotted) name of the operation class.
        payload : dict[str, JSONValue]
            The operation-specific payload needed to undo the operation.  Must be
            JSON-serializable.
        """
        model_config = ConfigDict(extra="forbid")  # reject unexpected keys
        kind: QualName
        undo: bool
        payload: dict[str, JSONValue] = Field(default_factory=dict)

    class StepRecord(BaseModel):
        """JSON representation for a full step in the journal.

        Attributes
        ----------
        syntax : int
            The syntax version of the journal format, for forward compatibility.
        run : UUID4Hex
            The UUID of the `run()` that created this step.
        name : QualName
            The fully-qualified (dotted) name of the step function.  Guaranteed to be
            unique within the pipeline.
        status : Literal["in_progress", "completed", "rolled_back"]
            The current status of the step.
        started_at : datetime
            The ISO timestamp when the step began execution.
        ended_at : datetime | None
            The ISO timestamp when the step completed execution or was successfully
            rolled back, or None if the step is still in-progress.
        version : PositiveInt
            The explicit contract version for this step.  Trivially set to 1 and
            ignored for ephemeral steps, since they never transition to the "completed"
            state.
        accesses : dict[str, Hash256 | Literal["<missing>"]]
            A mapping that tracks the arguments and global facts that were accessed
            during this step, where each key is an argument/fact name and each value is
            a hash of its JSON-serialized value.  This is used for change detection in
            addition to `version`; if any argument or fact is removed or changes value
            between runs, then all steps that access it will be invalidated.
        requires : list[QualName]
            The list of dotted names of prerequisite steps that must be completed
            before this step can run.  If any of these steps are invalidated, or
            require an invalidated step in turn (recursively), then this step will also
            be invalidated.
        facts : dict[str, JSONValue]
            A mapping of the facts that were generated during this step.  The keys will
            never conflict with `accesses`, and the values must be JSON-serializable.
        ops : list[Pipeline.OpRecord]
            An ordered list of atomic operations that were performed during this step,
            which will be replayed in reverse order if the step needs to be rolled
            back.
        error : str | None
            An informative error message if the step failed.
        """
        model_config = ConfigDict(extra="forbid")  # reject unexpected keys
        syntax: int
        run: UUID4Hex
        name: QualName
        status: Literal["in_progress", "completed", "rolled_back"]
        started_at: datetime
        ended_at: datetime | None
        version: PositiveInt
        accesses: dict[str, Hash256 | Literal["<missing>"]]
        requires: list[QualName]
        facts: dict[str, JSONValue]
        ops: list[Pipeline.OpRecord]
        error: str

        @model_validator(mode="after")
        def _check_syntax(self) -> Pipeline.StepRecord:
            if self.syntax != SYNTAX:
                raise ValueError(f"unsupported journal syntax version: {self.syntax}")
            return self

        @model_validator(mode="after")
        def _check_ended_at(self) -> Pipeline.StepRecord:
            if self.ended_at is None:
                if self.status != "in_progress":
                    raise ValueError(
                        f"ended_at must be an ISO timestamp when status='{self.status}'"
                    )
            elif self.ended_at < self.started_at:
                raise ValueError("ended_at must be after started_at")
            return self

    @dataclass(frozen=True)
    class Fact:
        """An entry in the global fact context for the pipeline, representing a value
        that can be shared between steps.

        Attributes
        ----------
        origin : QualName | None
            The name of the step that created this fact.  If another step accesses this
            fact without declaring the origin step as a prerequisite, then an error
            will be raised.  If None, then the fact is considered to be ephemeral, and
            may have originated from kwargs passed to `pipeline.run()`.
        hash : Hash256
            The SHA-256 hash of the JSON-serialized value of this fact.
        value : JSONView
            The deserialized value of this fact.  If the value is a mutable type (e.g.
            list or dict), then it will be wrapped in a read-only proxy to prevent
            accidental modification by dependent steps.
        """
        origin: QualName | None
        hash: Hash256
        value: JSONView

    # pipeline-wide config
    state_dir: Path
    timeout: int = 30
    keep: int = 3

    # registration
    _targets: list[Pipeline.Target] = field(default_factory=list, repr=False)
    _ordered: bool = field(default=False, repr=False)
    _lookup: dict[QualName, Pipeline.Target] = field(default_factory=dict, repr=False)

    # context
    _lock: LockDir = field(init=False, repr=False)
    _records: list[Pipeline.StepRecord] = field(default_factory=list, repr=False)
    _facts: dict[str, Pipeline.Fact] = field(default_factory=dict, repr=False)
    _completed: dict[Pipeline.Target, PositiveInt] = field(default_factory=dict, repr=False)
    _run_id: UUID4Hex = field(init=False, repr=False)

    def __post_init__(self) -> None:
        if self.keep < 1:
            raise ValueError("pipeline must keep at least one attempt per step")
        mkdir_private(self.state_dir)
        self._lock = LockDir(path=self.state_dir / ".lock", timeout=self.timeout)

    @staticmethod
    def _json(obj: Any) -> str:
        # NOTE: all kwargs and facts must be JSON-serializable, and need to produce
        # stable hashes for change detection.  We also store in the same format, so that
        # we can round-trip arbitrary data.
        return json.dumps(
            obj,
            sort_keys=True,
            indent=2,
            ensure_ascii=True,
            allow_nan=False
        )

    @staticmethod
    def _readonly(obj: JSONValue) -> JSONView:
        if isinstance(obj, list):
            return tuple(Pipeline._readonly(v) for v in obj)
        if isinstance(obj, dict):
            return MappingProxyType({k: Pipeline._readonly(v) for k, v in obj.items()})
        return obj

    def _dump(self) -> None:
        data = JournalAdapter.dump_python(self._records, mode="json")
        atomic_write_text(self.state_dir / "journal.json", self._json(data) + "\n", private=True)

    def _plan(self) -> None:
        if self._ordered:
            return

        # topological sort
        pending = list(self._targets)
        have: set[Pipeline.Target] = set()
        order: list[Pipeline.Target] = []
        while pending:
            new_pending: list[Pipeline.Target] = []
            for step in pending:
                if step.requires.issubset(have):
                    have.add(step)
                    order.append(step)
                else:
                    new_pending.append(step)

            # no progress made - requirements unsatisfied
            if len(new_pending) == len(pending):
                lines = ["Pipeline stalled; unsatisfied dependencies:"]
                for s in pending:
                    lines.append(f"  - {s}: missing {s.requires - have}")
                raise OSError("\n".join(lines))

            pending = new_pending

        self._targets = order
        self._ordered = True

    def _origin_is_required(self, origin: QualName, requires: list[QualName]) -> bool:
        target = self._lookup.get(origin)
        if target is None:
            return False
        seen: set[Pipeline.Target] = set()
        stack = [self._lookup[r] for r in requires if r in self._lookup]
        while stack:
            t = stack.pop()
            if t == target:
                return True
            seen.add(t)
            stack.extend(t.requires - seen)
        return False

    @staticmethod
    def _rollback_fail_error(step: Pipeline.StepRecord, error: str) -> None:
        if step.error:
            step.error = f"{step.error}; partial rollback failure:\n{error}"
        else:
            step.error = f"partial rollback failure:\n{error}"

    def _rollback_step(self, step: Pipeline.StepRecord, force: bool) -> None:
        # undo operations in reverse order, stopping on first failure unless forced
        with Pipeline.UndoStep(ephemeral=True, pipeline=self, record=step) as ctx:
            while step.ops:
                op = step.ops.pop()  # destroy as we go
                if not op.undo:
                    continue  # one-way operation; skip undo

                # lookup undo handler
                undo = ATOMIC_UNDO.get(op.kind)
                if not undo:
                    self._rollback_fail_error(step, f"unknown atomic operation kind '{op.kind}'")
                    if not force:
                        step.ops.append(op)
                        break  # stop at first failure
                    continue  # swallow error and keep going

                # attempt undo
                try:
                    undo(ctx, op.payload, force)
                except Exception as err:
                    self._rollback_fail_error(step, str(err))
                    if not force:
                        step.ops.append(op)
                        break  # stop at first failure
                    continue  # swallow error and keep going

    def _rollback_in_progress(self) -> bool:
        changed = False
        for s in reversed(self._records):
            if s.status == "in_progress":
                self._rollback_step(s, force=False)
                changed = True
        return changed

    def __enter__(self) -> Pipeline:
        # acquire lock
        self._lock.__enter__()
        if self._lock.depth > 1:
            return self  # re-entrant case

        try:
            # generate new run ID
            self._run_id = uuid.uuid4().hex

            # load + validate journal state
            changed = False
            journal = self.state_dir / "journal.json"
            if journal.exists():
                raw = journal.read_text(encoding="utf-8")
                try:
                    self._records = JournalAdapter.validate_json(raw)
                except ValidationError as err:
                    raise OSError(f"Pipeline journal is malformed:\n{err}") from err
            else:  # write new journal
                self._records = []
                changed = True

            # roll back in-progress steps from previous runs
            if self._rollback_in_progress():
                changed = True

            # write changes to journal if needed
            if changed:
                self._dump()

            # hydrate in-memory state
            self._facts.clear()
            self._completed.clear()
            for s in self._records:
                if s.status != "completed":
                    continue

                target = self._lookup.get(s.name)
                if target is None:
                    continue

                # hydrate facts with hashes + readonly views
                self._completed[target] = s.version
                for k, v in s.facts.items():
                    v_norm = v
                    v_json = self._json(v_norm)
                    v_hash = hashlib.sha256(v_json.encode("utf-8")).hexdigest()
                    v_readonly = self._readonly(v_norm)
                    self._facts[k] = Pipeline.Fact(
                        origin=s.name,
                        hash=v_hash,
                        value=v_readonly
                    )

            return self

        # ensure lock is properly released on error
        except Exception as err:
            self._lock.__exit__(type(err), err, getattr(err, "__traceback__", None))
            raise

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        if self._lock.depth > 1:
            self._lock.__exit__(exc_type, exc_value, traceback)
            return  # re-entrant case

        try:
            # roll back any in-progress steps, including ephemeral steps if there is an error
            self._rollback_in_progress()

            # compact the journal to keep only the last N attempts per step
            compacted: list[Pipeline.StepRecord] = []
            seen: dict[QualName, int] = {}
            for step in reversed(self._records):
                if step.status == "rolled_back":
                    count = seen.get(step.name, 0)
                    if count < self.keep:
                        compacted.append(step)
                        seen[step.name] = count + 1
                else:  # preserve completed and in-progress steps
                    compacted.append(step)
            self._records = compacted
            self._records.reverse()

            # always synchronize journal state on exit
            self._dump()
            self._records.clear()
            self._facts.clear()
            self._completed.clear()

        # always release lock
        finally:
            self._lock.__exit__(exc_type, exc_value, traceback)

    @overload
    def __call__(
        self,
        func: Pipeline.Function,
        *,
        requires: Iterable[Pipeline.Function] | None = ...,
        enable: bool = ...,
        ephemeral: bool = ...,
        version: PositiveInt | None = ...,
    ) -> Pipeline.Function: ...
    @overload
    def __call__(
        self,
        func: None = ...,
        *,
        requires: Iterable[Pipeline.Function] | None = ...,
        enable: bool = ...,
        ephemeral: bool = ...,
        version: PositiveInt | None = ...,
    ) -> Callable[[Pipeline.Function], Pipeline.Function]: ...
    def __call__(
        self,
        func: Pipeline.Function | None = None,
        *,
        requires: Iterable[Pipeline.Function] | None = None,
        enable: bool = True,
        ephemeral: bool = False,
        version: int | None = None,
    ) -> Pipeline.Function | Callable[[Pipeline.Function], Pipeline.Function]:
        """Register a step function with the given dependencies.

        Parameters
        ----------
        requires : Iterable[Pipeline.Function] | None, optional
            The prerequisite functions that must be completed before this step can run.
            These should be provided as function objects that have previously been
            decorated with this method.  If None (the default), then all previous steps
            will be required, effectively forcing sequential execution.
        enable : bool, optional
            Whether to enable this step in the pipeline.  If False, the step will not
            be registered, and the pipeline will be unchanged.  Default is True.
        ephemeral : bool, optional
            Whether this step's effects should be persisted between runs (False) or
            rolled back successful completion (True).  Default is False.  If any
            persistent step depends on an ephemeral one, an error will be raised during
            registration.
        version : int | None, optional
            Explicit contract version for this step.  Persistent steps must provide a
            positive integer version.  Ephemeral steps must not provide a version.
            Users should increment versions when a step's externally-observable
            contract changes (e.g. facts or side effects), not for internal refactors
            that don't meaningfully change the behavior.

        Returns
        -------
        func : Pipeline.Function
            The decorated step function, which will execute the step with the given
            `Pipeline.InProgress` context.  The `ctx.do()` method should be used within
            the function to record mutating operations in the journal, so that they can
            be rolled back if needed.  If an error is raised during the function, the
            pipeline will roll back to the previous stable state using the journal.

        Raises
        ------
        TypeError
            If any provided capabilities are not unique within the pipeline, or if
            the step name is not unique, or if a persistent step depends on an
            ephemeral step.
        OSError
            If the pipeline context is currently active.
        """
        if self._lock.depth > 0:
            raise OSError("cannot register pipeline steps while within its context")

        if not enable:  # no-op
            return func if func is not None else lambda func: func

        def _decorator(func: Pipeline.Function) -> Pipeline.Function:
            name = _qualname(func)
            if name in self._lookup:
                raise TypeError(f"Pipeline step function must be unique: '{name}'")

            # gather prerequisites
            r: frozenset[Pipeline.Target]
            if requires is None:
                r = frozenset(self._targets)
            else:
                _r = set()
                for dep in requires:
                    dep_name = _qualname(dep)
                    dep_target = self._lookup.get(dep_name)
                    if not dep_target:
                        raise TypeError(f"Pipeline step dependency not found: '{dep_name}'")
                    _r.add(dep_target)
                r = frozenset(_r)

            # validate cache dependencies and explicit contract versioning
            step_version = 1
            if not ephemeral:
                for t in r:
                    if t.ephemeral:
                        raise TypeError(
                            f"Persistent pipeline step '{name}' cannot depend on ephemeral "
                            f"step '{t}'."
                        )
                if not isinstance(version, int) or isinstance(version, bool) or version < 1:
                    raise TypeError(
                        f"Persistent pipeline step '{name}' must declare a positive "
                        "integer contract version via version=<int>."
                    )
                step_version = version
            elif version is not None:
                raise TypeError(
                    f"Ephemeral pipeline step '{name}' cannot declare version={version}. "
                    "Ephemeral steps must omit version."
                )
            if (
                not isinstance(step_version, int) or
                isinstance(step_version, bool) or
                step_version < 1
            ):
                raise TypeError(
                    f"Invalid contract version for pipeline step '{name}': {step_version}"
                )

            # register step
            target = Pipeline.Target(
                func=func,
                version=step_version,
                ephemeral=ephemeral,
                requires=r
            )
            self._targets.append(target)
            self._lookup[name] = target
            self._ordered = False  # re-plan on next run
            return func

        return _decorator(func) if func is not None else _decorator

    def __len__(self) -> int:
        """The number of functions registered in this pipeline.

        Returns
        -------
        int
            The number of registered functions.
        """
        return len(self._targets)

    def __iter__(self) -> Iterator[Pipeline.Function]:
        """Iterate over the functions registered in this pipeline in topological order.

        Returns
        -------
        Iterator[Pipeline.Function]
            An iterator over the registered functions.
        """
        self._plan()
        for target in self._targets:
            yield target.func

    def __contains__(self, key: str) -> bool:
        """Check if a fact is present in the global pipeline context.

        Parameters
        ----------
        key : str
            The name of the fact to check for.

        Returns
        -------
        bool
            True if the fact is present in the global context, False otherwise.

        Raises
        ------
        OSError
            If the pipeline context is not currently active.
        """
        if self._lock.depth < 1:
            raise OSError("must acquire pipeline context before accessing facts")
        return key in self._facts

    def __getitem__(self, key: str) -> JSONView:
        """Look up a fact stored in the global pipeline context.

        Parameters
        ----------
        key : str
            The key to look up.  Note that only facts stored by persistent steps
            will be available in the global context, so this will not return any facts
            that were recorded by ephemeral steps., unless it is somehow called during
            pipeline execution (which is discouraged - use the in-progress context
            instead).

        Returns
        -------
        JSONView
            A read-only JSON view of the requested fact, where lists are converted to
            tuples and dictionaries to `MappingProxyType` instances in order to
            discourage modification.

        Raises
        ------
        OSError
            If the pipeline context is not currently active.
        KeyError
            If the requested fact is not found in the global context.
        """
        if self._lock.depth < 1:
            raise OSError("must acquire pipeline context before accessing facts")
        return self._facts[key].value

    def get(self, key: str, default: JSONValue = None) -> JSONView:
        """Look up a fact stored in the global pipeline context, returning a default
        value if the fact is not found.

        Parameters
        ----------
        key : str
            The key to look up.
        default : JSONValue, optional
            The value to return if the fact is not found.  Must be JSON-serializable,
            and will be converted to a deserialized, read-only view.

        Returns
        -------
        JSONView | None
            A read-only JSON view of the requested fact if it is found, or default
            value otherwise, where lists are converted to tuples and dictionaries to
            `MappingProxyType` instances in order to discourage modification.

        Raises
        ------
        OSError
            If the pipeline context is not currently active.
        """
        if self._lock.depth < 1:
            raise OSError("must acquire pipeline context before accessing facts")
        if key in self._facts:
            return self._facts[key].value
        return self._readonly(default)

    @dataclass
    class InProgress:
        """A convenient interface to the state recorded during an in-progress pipeline
        step.

        Attributes
        ----------
        ephemeral : bool
            Whether the current step is ephemeral (True) or persistent (False).  If
            True, then no facts or accesses will be recorded in the journal, and will
            only be kept in memory for the duration of the step.
        record : Pipeline.StepRecord
            The mutable journal record for the current step, which will be updated
            and persisted as the step progresses.
        pipeline : Pipeline
            The parent pipeline that is executing this step.
        """
        # pylint: disable=protected-access
        ephemeral: bool
        record: Pipeline.StepRecord
        pipeline: Pipeline = field(repr=False)
        _facts: dict[str, Pipeline.Fact] = field(default_factory=dict, repr=False)

        @property
        def run(self) -> str:
            """
            Returns
            -------
            str
                The unique identifier for the current run.
            """
            return self.pipeline._run_id

        @property
        def timeout(self) -> int:
            """
            Returns
            -------
            int
                The timeout in seconds for acquiring the pipeline lock.
            """
            return self.pipeline.timeout

        @property
        def state_dir(self) -> Path:
            """
            Returns
            -------
            Path
                The base directory for storing pipeline state.
            """
            return self.pipeline.state_dir

        def __getitem__(self, key: str) -> JSONView:
            """Look up a fact stored in the pipeline context.

            Parameters
            ----------
            key : str
                The key to look up.  If this corresponds to a fact in the global
                context, then the argument and its hashed value will be recorded in the
                journal for change detection.

            Returns
            -------
            JSONView
                A read-only JSON view of the requested fact, where lists are converted
                to tuples and dictionaries to `MappingProxyType` instances in order to
                discourage modification.

            Raises
            ------
            KeyError
                If the requested fact is not found, or if it originates from the global
                context but the corresponding step is not a prerequisite.
            """
            # check local facts first
            if key in self._facts:
                return self._facts[key].value

            # check global facts
            if key in self.pipeline._facts:
                fact = self.pipeline._facts[key]
                if (
                    fact.origin is not None and
                    key not in self.record.accesses and
                    not self.pipeline._origin_is_required(fact.origin, self.record.requires)
                ):
                    raise KeyError(
                        f"Fact '{key}' originates from step '{fact.origin}', which is not "
                        f"a prerequisite of the current step '{self.record.name}'.  "
                        "Add it to the 'requires' list to ensure proper ordering."
                    )
                if not self.ephemeral:
                    self.record.accesses[key] = fact.hash
                return fact.value

            # record negative global access and surface key error
            if not self.ephemeral:
                self.record.accesses[key] = MISSING
            raise KeyError(f"Fact '{key}' not found in local or global context")

        def __setitem__(self, key: str, value: JSONValue) -> None:
            """Set a fact in the local step context, which will be written to the
            global context upon step completion.

            Parameters
            ----------
            key : str
                The key to set.
            value : JSONValue
                The value to set for the keyed fact.  Must be JSON-serializable, and
                will be converted to a deserialized, read-only view for storage.

            Raises
            ------
            KeyError
                If the fact key conflicts with a fact in the global context.
            """
            if key in self.pipeline._facts:
                raise KeyError(f"Cannot overwrite '{key}' in global context")

            # serialize value, hash, and then deserialize for storage
            value_json = self.pipeline._json(value)
            value_hash = hashlib.sha256(value_json.encode("utf-8")).hexdigest()
            value_norm = json.loads(value_json)
            if not self.ephemeral:
                self.record.facts[key] = value_norm

            # store read-only view in local context, for future reference
            value_readonly = self.pipeline._readonly(value_norm)
            self._facts[key] = Pipeline.Fact(
                origin=self.record.name,
                hash=value_hash,
                value=value_readonly
            )
            self.dump()

        def __delitem__(self, key: str) -> None:
            """Delete a fact from the local step context.  Never modifies the global
            context.

            Parameters
            ----------
            key : str
                The key to delete.

            Raises
            ------
            KeyError
                If the requested fact is not found in the local step context.
            """
            if key not in self._facts:
                raise KeyError(f"Fact '{key}' not found in local context")

            self._facts.pop(key, None)
            self.record.facts.pop(key, None)
            self.dump()

        def __contains__(self, key: str) -> bool:
            """Check if a fact exists in the local step context or the global context.

            Parameters
            ----------
            key : str
                The fact key to check.

            Returns
            -------
            bool
                True if the fact exists in either context, False otherwise.
            """
            # check local facts first
            if key in self._facts:
                return True

            # record positive global access
            if key in self.pipeline._facts:
                fact = self.pipeline._facts[key]
                if (
                    fact.origin is not None and
                    key not in self.record.accesses and
                    not self.pipeline._origin_is_required(fact.origin, self.record.requires)
                ):
                    raise KeyError(
                        f"Fact '{key}' originates from step '{fact.origin}', which is "
                        f"not a prerequisite of the current step '{self.record.name}'.  "
                        "Add it to the 'requires' list to ensure proper ordering."
                    )
                if not self.ephemeral:
                    self.record.accesses[key] = fact.hash
                return True

            # record negative global access
            if not self.ephemeral:
                self.record.accesses[key] = MISSING
            return False

        def get(self, key: str, default: JSONValue = None) -> JSONView:
            """Look up a fact stored in the pipeline context, preferring the local
            step context and falling back to the global context.

            Parameters
            ----------
            key : str
                The fact key to look up.
            default : JSONValue, optional
                The default value to return if the fact is not found, by default None.

            Returns
            -------
            JSONView
                The value of the requested fact, or the default value if not found.

            Raises
            ------
            KeyError
                If the requested fact originates from the global context but the
                corresponding step is not a prerequisite.
            """
            # check local facts first
            if key in self._facts:
                return self._facts[key].value

            # record positive global access
            if key in self.pipeline._facts:
                fact = self.pipeline._facts[key]
                if (
                    fact.origin is not None and
                    key not in self.record.accesses and
                    not self.pipeline._origin_is_required(fact.origin, self.record.requires)
                ):
                    raise KeyError(
                        f"Fact '{key}' originates from step '{fact.origin}', which is not "
                        f"a prerequisite of the current step '{self.record.name}'.  "
                        "Add it to the 'requires' list to ensure proper ordering."
                    )
                if not self.ephemeral:
                    self.record.accesses[key] = fact.hash
                return fact.value

            # record negative global access
            if not self.ephemeral:
                self.record.accesses[key] = MISSING
            return self.pipeline._readonly(default)

        def do(self, op: Atomic, undo: bool = True) -> None:
            """Apply an operation within this pipeline step, recording it in the
            journal.

            Parameters
            ----------
            op : Atomic
                The operation to apply.  See the `Atomic` type for implementation
                details.
            undo : bool, optional
                Whether this operation should be undone if the step is rolled back, by
                default True.  Setting this to false will still record the operation in
                the journal, but it will be skipped during rollback, making it one-way.
            """
            rec = Pipeline.OpRecord(kind=_qualname(type(op)), undo=undo)
            self.record.ops.append(rec)
            self.dump()
            op.do(self, rec.payload)
            self.dump()

        def dump(self) -> None:
            """Write the in-progress step to the journal before completion.

            This is meant to be used as a helper for writing steps that need to persist
            their payloads even in the event of a crash (such as destructive file
            operations).  Note that the payload will always be written at the end of
            the step, so it is not necessary to call this method in any other case.
            """
            self.pipeline._dump()

    class RunStep(Pipeline.InProgress):
        """A context manager that appends a new step on entrance and transitions it
        to "completed" on exit, or rolls it back on error.
        """
        target: Pipeline.Target

        def __init__(self, pipeline: Pipeline, target: Pipeline.Target) -> None:
            self.target = target
            super().__init__(
                ephemeral=target.ephemeral,
                pipeline=pipeline,
                record=Pipeline.StepRecord(
                    syntax=SYNTAX,
                    run=pipeline._run_id,
                    name=str(target),
                    version=target.version,
                    status="in_progress",
                    started_at=datetime.now(timezone.utc),
                    ended_at=None,
                    requires=sorted(str(p) for p in target.requires),
                    accesses={},
                    ops=[],
                    facts={},
                    error="",
                )
            )

        def __enter__(self) -> Pipeline.RunStep:
            self.pipeline._records.append(self.record)
            self.dump()
            return self

        def __exit__(
            self,
            exc_type: type[BaseException] | None,
            exc_value: BaseException | None,
            exc_traceback: TracebackType | None
        ) -> None:
            if exc_type is None:
                # mark step as completed
                try:
                    # ephemeral steps never transition to "completed"
                    if not self.target.ephemeral:
                        self.pipeline._completed[self.target] = self.target.version
                        self.record.status = "completed"
                    self.record.ended_at = datetime.now(timezone.utc)
                    self.pipeline._facts.update(self._facts)
                    self.dump()
                    return

                # fall through to rollback
                except Exception as err:
                    exc_type = type(err)
                    exc_value = err
                    exc_traceback = getattr(err, "__traceback__", None)

            # roll back step on error
            self.pipeline._rollback_step(self.record, force=False)
            if self.record.error:
                self.record.error = f"{self.record.error}; {exc_type.__name__}: {exc_value}"
            else:
                self.record.error = f"{exc_type.__name__}: {exc_value}"
            self.dump()

    class UndoStep(Pipeline.InProgress):
        """A context manager that loads a step on entrance and transitions it to
        "rolled_back" on exit.
        """

        def __enter__(self) -> Pipeline.UndoStep:
            self.record.status = "in_progress"
            self.record.ended_at = None

            # remove from completed targets if present
            target = self.pipeline._lookup.get(self.record.name)
            if target is not None:
                self.pipeline._completed.pop(target, None)

            # remove global facts set by this step + hydrate local facts
            self._facts.clear()
            for k, v in self.record.facts.items():
                self.pipeline._facts.pop(k, None)
                v_json = self.pipeline._json(v)
                v_hash = hashlib.sha256(v_json.encode("utf-8")).hexdigest()
                v_readonly = self.pipeline._readonly(v)
                self._facts[k] = Pipeline.Fact(
                    origin=self.record.name,
                    hash=v_hash,
                    value=v_readonly
                )

            self.pipeline._dump()
            return self

        def __exit__(
            self,
            exc_type: type[BaseException] | None,
            exc_value: BaseException | None,
            exc_traceback: TracebackType | None
        ) -> None:
            # if some ops haven't been undone, leave as "in_progress" to trigger
            # future rollback
            if len(self.record.ops) == 0:
                self.record.status = "rolled_back"
                self.record.ended_at = datetime.now(timezone.utc)
            self.pipeline._dump()

    def do(self, **kwargs: JSONValue) -> dict[str, JSONView]:
        """Run the pipeline with the given keyword arguments.

        Parameters
        ----------
        **kwargs : JSONValue
            Keyword arguments that can be accessed by `Pipeline.InProgress` steps
            during execution via the subscript interface.  Each value must be
            JSON-serializable, and a hash + read-only view of its serialized value will
            be stored for change detection and lookup.  If the value changes between
            runs, then any steps that access it will be invalidated and re-executed.

        Returns
        -------
        dict[str, JSONView]
            The final set of facts in the pipeline context after execution, as
            read-only views.

        Raises
        ------
        OSError
            If the pipeline cannot proceed due to unsatisfied dependencies, or if the
            pipeline context is active.
        KeyError
            If any provided keyword argument conflicts with a fact in the pipeline
            context.
        Exception
            If any step in the pipeline execution fails.  The type of the exception
            will depend on the underlying error.
        """
        if self._lock.depth > 0:
            raise OSError("cannot run a pipeline while within its context")

        # compute topological order
        self._plan()

        # acquire lock + journal context
        with self:
            # serialize kwargs, compute hashes, and store read-only views for lookup
            conflicts: list[str] = [k for k in kwargs if k in self._facts]
            if conflicts:
                raise KeyError(f"kwargs cannot alias facts in pipeline context: {conflicts}")
            for k, v in kwargs.items():
                v_json = self._json(v)
                v_hash = hashlib.sha256(v_json.encode("utf-8")).hexdigest()
                v_readonly = self._readonly(json.loads(v_json))
                self._facts[k] = Pipeline.Fact(
                    origin=None,
                    hash=v_hash,
                    value=v_readonly
                )

            # look for steps that access changed facts or whose requirements have changed
            invalid: set[Pipeline.Target] = set()
            for s in self._records:
                if s.status != "completed":
                    continue

                target = self._lookup.get(s.name)
                if target is None:
                    continue

                requires = sorted(str(r) for r in target.requires)
                if (
                    requires != s.requires or  # requirements changed
                    not all(
                        access_name in s.facts or (
                            access_name in self._facts and  # positive access
                            self._facts[access_name].hash == access_hash
                        ) or (
                            access_hash == MISSING and  # negative access
                            access_name not in self._facts
                        )
                        for access_name, access_hash in s.accesses.items()
                    )
                ):
                    invalid.add(target)

            # extend to cover all changed targets + dependencies
            for t in self._targets:
                # the targets have already have been topologically sorted, so any
                # dependent capabilities can be invalidated by checking earlier targets
                if self._completed.get(t) != t.version or any(r in invalid for r in t.requires):
                    invalid.add(t)

            # roll back any completed steps that are now invalid, or have been removed
            # from the pipeline
            for s in reversed(self._records):
                if s.status == "completed":
                    target = self._lookup.get(s.name)
                    if target is None or target in invalid:
                        self._rollback_step(s, force=False)
                        if s.error:
                            s.error = f"{s.error}; step invalidated due to dependency change"
                        else:
                            s.error = "step invalidated due to dependency change"

            # apply incomplete steps in topological order
            for t in self._targets:
                if t in self._completed:
                    continue

                # run step within a new journal entry
                with Pipeline.RunStep(pipeline=self, target=t) as ctx:
                    t.func(ctx)

            # return final set of facts as read-only views
            return {k: v.value for k, v in self._facts.items()}

    def undo(
        self,
        steps: Iterable[Pipeline.Function] | None = None,
        *,
        force: bool = False
    ) -> None:
        """Replay the journal in reverse order to undo changes.

        Parameters
        ----------
        steps : Iterable[Pipeline.Function] | None
            An optional sequence of step functions which should be rolled back, along
            with any steps that depend on them.  If None (the default), all completed
            steps will be rolled back.
        force : bool, optional
            If True, continue rolling back even if individual undo operations fail.
            This is intended to be used in a `clean` command, which uninstalls all
            changes immediately before deleting the state directory.  Operations should
            therefore make sure not to leave important data in the state directory if
            this flag is set, since it may be permanently lost after the `clean`
            command finishes.

        Raises
        ------
        OSError
            If the pipeline context is active.
        KeyError
            If any specified capability is not found in the pipeline.
        """
        if self._lock.depth > 0:
            raise OSError("cannot undo a pipeline while within its context")

        # roll back all completed steps in reverse order
        if steps is None:
            with self:
                for s in reversed(self._records):
                    if s.status == "completed":
                        self._rollback_step(s, force=force)
            return

        # convert step functions into dotted names
        invalid: set[QualName] = set()
        for func in steps:
            func_name = _qualname(func)
            if func_name not in self._lookup:
                raise KeyError(func_name)
            invalid.add(func_name)
        if not invalid:
            return  # nothing to roll back

        # roll back only steps providing or requiring the specified capabilities
        with self:
            # extend invalid steps to include all dependent capabilities
            completed = [s for s in self._records if s.status == "completed"]
            while True:
                n = len(invalid)
                for s in completed:
                    if any(r in invalid for r in s.requires):
                        invalid.add(s.name)
                if len(invalid) == n:
                    break  # no changes

            # roll back all steps that provide or require an invalidated capability
            for s in reversed(completed):
                if s.name in invalid:
                    self._rollback_step(s, force=force)


JournalAdapter = TypeAdapter(list[Pipeline.StepRecord])


on_init = Pipeline(state_dir=STATE_DIR / "init", keep=5)
on_build = Pipeline(state_dir=STATE_DIR / "build", keep=5)
on_start = Pipeline(state_dir=STATE_DIR / "start", keep=5)
on_enter = Pipeline(state_dir=STATE_DIR / "enter", keep=5)
on_code = Pipeline(state_dir=STATE_DIR / "code", keep=5)
on_run = Pipeline(state_dir=STATE_DIR / "run", keep=5)
on_stop = Pipeline(state_dir=STATE_DIR / "stop", keep=5)
on_pause = Pipeline(state_dir=STATE_DIR / "pause", keep=5)
on_resume = Pipeline(state_dir=STATE_DIR / "resume", keep=5)
on_restart = Pipeline(state_dir=STATE_DIR / "restart", keep=5)
on_prune = Pipeline(state_dir=STATE_DIR / "prune", keep=5)
on_rm = Pipeline(state_dir=STATE_DIR / "rm", keep=5)
on_ls = Pipeline(state_dir=STATE_DIR / "ls", keep=5)
on_monitor = Pipeline(state_dir=STATE_DIR / "monitor", keep=5)
on_log = Pipeline(state_dir=STATE_DIR / "log", keep=5)
on_top = Pipeline(state_dir=STATE_DIR / "top", keep=5)
on_import = Pipeline(state_dir=STATE_DIR / "import", keep=5)
on_export = Pipeline(state_dir=STATE_DIR / "export", keep=5)
on_publish = Pipeline(state_dir=STATE_DIR / "publish", keep=5)
