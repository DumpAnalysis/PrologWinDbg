:- module(patterns, [stack_contains/4, shared_function/5, stack_evidence/3,
                     unreadable_at/3, inspect_magic/3]).
:- use_module('../prolog/wd').

stack_contains(S,Thread,Module,Function) :-
    wd:stack_frame(S,Thread,_,IP),
    wd:symbol(S,IP,Module,Function,_).

shared_function(S,ThreadA,ThreadB,Module,Function) :-
    wd:thread(S,ThreadA), wd:thread(S,ThreadB), ThreadA < ThreadB,
    stack_contains(S,ThreadA,Module,Function),
    stack_contains(S,ThreadB,Module,Function).

% Evidence includes status. A captured prefix is not proof of a complete stack.
stack_evidence(S,Thread,evidence(Frames,Coverage)) :-
    wd:thread(S,Thread), wd:coverage(S,stack(Thread),Coverage),
    findall(frame(Depth,IP),wd:stack_frame(S,Thread,Depth,IP),Frames).

unreadable_at(S,Address,Reason) :- wd:memory(S,Address,1,unavailable(Reason)).

inspect_magic(S,Address,Verdict) :-
    wd:memory(S,Address,8,Result),
    ( Result = ok([80,82,79,76,79,71,68,66]) -> Verdict=matched(prologdb,Address)
    ; Result = ok(Bytes) -> Verdict=mismatch(Bytes)
    ; Verdict=inconclusive(Result) ).
