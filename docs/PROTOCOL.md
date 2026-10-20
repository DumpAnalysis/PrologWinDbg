# Local worker protocol, version 1

Messages are ASCII lines with tab-separated fields and LF termination.
Text fields are hex-encoded UTF-8. Byte fields are hex-encoded octets.
Integers are decimal except target addresses in READ/ADDRESS messages, which
are hexadecimal without a prefix. Addresses never pass through floating-point
serialization. Empty hex fields are valid and retained.

| Direction | Fields |
|---|---|
| Worker to DLL | `HELLO 1 VersionHex` |
| DLL to worker | `CONSULT Id AbsolutePathHex` |
| DLL to worker | `QUERY Id Milliseconds Inferences MaxAnswers GoalHex FactsHex` |
| Worker to DLL | `ANSWER Id TermTextHex` |
| Worker to DLL | `OUTPUT Id TextHex` |
| Worker to DLL | `READ Id Sequence AddressHex Length` |
| DLL to worker | `MEM Id Sequence Status BytesHex ReasonHex` |
| Worker to DLL | `LOOKUP Id Sequence SymbolNameHex` |
| DLL to worker | `ADDRESS Id Sequence Status Value` |
| Worker to DLL | `DONE Id Status AnswerCount DetailHex` |
| Host to worker | `QUIT` |

Spaces above separate columns for readability; actual separators are tabs.
For ADDRESS, Value is an address in hex when Status is `ok`, otherwise a
hex-encoded UTF-8 reason when Status is `unavailable`.

An empty fact list creates no debugger snapshot and is used by `!prolog.eval`.
Other fact lists must have their first argument equal to the query Id.

DONE statuses: `ok`, `false`, `answer_limit`, `inference_limit`, `timeout`, or
`error`. Partial answers can precede a timeout or error and must not be treated
as an exhaustive result set. A normal Prolog error preserves the worker.

All requests run sequentially. Memory RPC is synchronous and can only be
issued by the worker's main thread. User standard streams are rebound while
evaluating; ordinary writes become OUTPUT messages and reads see EOF. Native
foreign libraries writing directly to OS stdout can corrupt the protocol and
are outside this version's supported integration model.

Time signals are deferred across each synchronous request/reply exchange. If
an inference exception unwinds an outstanding exchange, the worker consumes
the corresponding late reply before processing the next request. Tests cover
timeouts and inference exhaustion at these protocol boundaries.

The DLL validates response IDs, field counts, numeric ranges, output lengths,
and RPC totals. It kills and restarts the worker after transport failure,
interruption, or expiration of its outer deadline. A Windows job controls worker
lifetime and memory consumption. The protocol has no public network endpoint.
