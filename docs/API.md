# Version 1 predicate API

All exported predicates live in module `wd`. A snapshot identifier is local to
the loaded extension instance. Obtain it with `wd:current_snapshot(S)` inside
`!prolog.query`. It is an observation identifier, not an operating-system ID or
a retained memory image.

## Captured facts

| Predicate | Meaning |
|---|---|
| `current_snapshot(-S)` | Current query's snapshot; throws if absent |
| `target(?S,?Kind,?ProcessId,?PointerBytes)` | `Kind` is `dump` or `live`; ProcessId is the OS PID; pointer width is 8 |
| `architecture(?S,?Architecture)` | Effective target processor: `amd64` or `arm64`; added in 0.2.0 |
| `thread(?S,?T)` | T is DbgEng's thread ID in the selected process |
| `thread_id(?S,?T,?WindowsTid)` | Maps engine thread ID to OS thread ID |
| `stack_frame(?S,?T,?Depth,?IP)` | Depth is zero-based; IP is an unsigned target address |
| `stack_pointer(?S,?T,?Depth,?SP)` | Stack pointer for the captured frame |
| `symbol(?S,?Address,?Module,?Function,?Displacement)` | Symbol resolved for a captured instruction address |
| `symbol_status(?S,?Address,?Status)` | `resolved` or `unavailable(Reason)` |
| `module(?S,?Name,?Base,?Size)` | Loaded module information; unloaded modules are excluded |
| `coverage(?S,?Area,?Status)` | Evidence coverage, described below |

Fact arguments are ordinary Prolog terms, with integer addresses. Module and
function names are atoms returned by the debugger; names and case are preserved.
The `symbol/5` relation contains only addresses encountered in the captured
stacks. Use `lookup/3` for a name-to-address request.

Thread coverage is `enumerated(Count)` or `truncated(Captured,Total)`. Module
coverage is `enumerated(Count)`, `partial(Captured,Total)`, or
`unavailable(Reason)`. Stack coverage uses an area `stack(T)` and a status
`captured_prefix(Count,unverified_end)`, `captured_prefix(Count,frame_limit)`,
or `unavailable(Reason)`.

An enumerated thread list is a list returned by the engine for that stop. It is
not proof that a limited dump contains all threads that existed historically.
Stack prefix status deliberately makes no completeness claim.

## Memory and symbol requests

`memory(+S,+Address,+Length,-Result)` reads virtual memory in the selected
process. Length must be an integer from 0 through 65,536. Addresses must fit an
unsigned 64-bit range without wrapping. Invalid arguments throw; unreadable
target data produces an explicit result:

- `ok(Bytes)` — the full requested range was returned successfully.
- `partial(Bytes,RequestedLength,Reason)` — some data was returned but the
  request was not fully successful. `Bytes` contains only bytes actually read.
- `unavailable(Reason)` — no usable bytes were returned.

`Bytes` is a list of integer octets. Exact repeated reads are cached for the
duration of a query. There is no cross-query memory cache. The C++ side checks
both HRESULT and BytesRead, including successful short reads.

`bytes(+S,+Address,+Length,-Bytes)` requires a complete read. Otherwise it throws
`error(incomplete_memory(Address,Length,Result),Context)`.

`pointer(+S,+Address,-Value)` reads one native target pointer and converts the
little-endian bytes to an unsigned integer. A pointer stored in target memory
is a target address; it is never dereferenced in the debugger's own process.

`lookup(+S,+Name,-Result)` resolves a qualified symbol atom such as
`'myapp!g_state'`. The result is `ok(Address)` or `unavailable(Reason)`. The name
is passed to GetOffsetByNameWide, not to the debugger command interpreter.

## Evidence-aware rules

Raw Prolog negation is negation-as-failure. With incomplete evidence,
`\+ wd:stack_frame(S,T,_,SomeIP)` means only that a matching fact was not found.
It does not prove that the corresponding function was never on the full stack.

Use explicit results for diagnostic conclusions. For example:

```prolog
check_signature(S,A,Verdict) :-
    wd:memory(S,A,4,Result),
    ( Result = ok([80,82,79,76]) -> Verdict=matched
    ; Result = ok(_) -> Verdict=mismatch
    ; Verdict=inconclusive(Result) ).
```

The match and mismatch here concern the four bytes at this address and stop.
They make no claim about the entire process or an earlier execution state.

## Query lifecycle

The extension captures facts on the debugger command thread, restores the
saved selection/scope, and sends the facts and goal to the worker. Lazy memory
and symbol requests are serviced on that same command thread. The worker
enumerates answers under a single aggregate inference/time budget.

Fact text is accepted only as a list of ground, whitelisted fact terms. It
cannot install directives or clauses. Query text and consulted files, however,
are intentionally executable Prolog supplied by the user.

The worker clears facts and caches after success, failure, or an exception.
User-defined rules persist. No continuation is retained after the answer limit.
Rerunning a query captures a new snapshot and can repeat user-defined side
effects. Version 1 provides no target-writing predicates or execution-control
predicates.
