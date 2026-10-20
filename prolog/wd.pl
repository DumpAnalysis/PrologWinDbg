:- module(wd, [current_snapshot/1, thread/2, thread_id/3, stack_frame/4,
               stack_pointer/4, symbol/5, symbol_status/3, module/4,
               coverage/3, target/4, architecture/2, memory/4, bytes/4, pointer/3,
               lookup/3]).
:- use_module(library(error)).
:- dynamic snapshot/1, thread/2, thread_id/3, stack_frame/4, stack_pointer/4,
           symbol/5, symbol_status/3, module/4, coverage/3, target/4, architecture/2.
:- multifile memory_provider/4, lookup_provider/3.
:- dynamic memory_cache/4, lookup_cache/3.

current_snapshot(S) :-
    ( snapshot(S) -> true ; existence_error(debugger_snapshot, current) ).

require_snapshot(S) :-
    must_be(integer, S),
    ( snapshot(S) -> true ; existence_error(debugger_snapshot, S) ).

% Internal protocol entry point. Accept facts only, never directives or clauses.
install_snapshot(S, Facts) :-
    must_be(integer, S), must_be(list, Facts),
    maplist(valid_fact(S), Facts),
    clear_snapshot,
    assertz(snapshot(S)),
    maplist(assertz, Facts).

valid_fact(S, Fact) :-
    ( ground(Fact), compound(Fact), functor(Fact, Name, Arity),
      allowed_fact(Name, Arity), arg(1, Fact, S)
    -> true ; domain_error(snapshot_fact, Fact) ).
allowed_fact(thread, 2).
allowed_fact(thread_id, 3).
allowed_fact(stack_frame, 4).
allowed_fact(stack_pointer, 4).
allowed_fact(symbol, 5).
allowed_fact(symbol_status, 3).
allowed_fact(module, 4).
allowed_fact(coverage, 3).
allowed_fact(target, 4).
allowed_fact(architecture, 2).

clear_snapshot :-
    retractall(snapshot(_)), retractall(thread(_,_)), retractall(thread_id(_,_,_)),
    retractall(stack_frame(_,_,_,_)), retractall(stack_pointer(_,_,_,_)),
    retractall(symbol(_,_,_,_,_)), retractall(symbol_status(_,_,_)),
    retractall(module(_,_,_,_)), retractall(coverage(_,_,_)), retractall(target(_,_,_,_)),
    retractall(architecture(_,_)),
    retractall(memory_cache(_,_,_,_)), retractall(lookup_cache(_,_,_)).

memory(S, Address, Length, Result) :-
    require_snapshot(S), must_be(integer, Address), must_be(integer, Length),
    ( Address >= 0, Address =< 0xffffffffffffffff,
      Length >= 0, Length =< 65536,
      (Length =:= 0 ; Address + Length - 1 =< 0xffffffffffffffff)
    -> true ; domain_error(memory_range, Address-Length) ),
    ( memory_cache(S, Address, Length, Cached) -> Result = Cached
    ; ( once(memory_provider(S, Address, Length, R)) -> true
      ; R = unavailable(no_memory_provider) ),
      assertz(memory_cache(S, Address, Length, R)), Result = R ).

% Convenient exact read; absence of bytes is an exception, never silent failure.
bytes(S, Address, Length, Bytes) :-
    memory(S, Address, Length, R),
    ( R = ok(Bytes) -> true ; throw(error(incomplete_memory(Address, Length, R), _)) ).

pointer(S, Address, Value) :-
    require_snapshot(S), target(S, _, _, PointerBytes),
    bytes(S, Address, PointerBytes, Bytes),
    little_endian(Bytes, 0, 0, Value).
little_endian([], _, V, V).
little_endian([B|Bs], Shift, Acc, V) :-
    Next is Acc \/ (B << Shift), S is Shift + 8,
    little_endian(Bs, S, Next, V).

lookup(S, Name, Result) :-
    require_snapshot(S), must_be(atom, Name),
    ( lookup_cache(S, Name, Cached) -> Result = Cached
    ; ( once(lookup_provider(S, Name, R)) -> true
      ; R = unavailable(no_symbol_provider) ),
      assertz(lookup_cache(S, Name, R)), Result = R ).
