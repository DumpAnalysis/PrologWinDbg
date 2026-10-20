:- module(prologdbg_worker, [main/0]).
:- use_module(library(readutil)).
:- use_module(library(utf8)).
:- use_module(library(time)).
:- use_module(library(error)).
:- use_module(library(prolog_stream)).
:- use_module(wd).
:- dynamic transport/2, active_request/1, consulting/0, pending_reply/3.
:- multifile user:message_hook/3.
:- multifile wd:memory_provider/4, wd:lookup_provider/3.

wd:memory_provider(S,A,L,R) :- prologdbg_worker:read_memory(S,A,L,R).
wd:lookup_provider(S,N,R) :- prologdbg_worker:lookup_symbol(S,N,R).

user:message_hook(_, error, _) :-
    prologdbg_worker:consulting,
    nb_getval(pd_load_errors,N), Next is N+1, nb_setval(pd_load_errors,Next),
    fail.  % Preserve SWI's normal diagnostic output as well.

main :-
    current_input(In), current_output(Out),
    set_stream(In, encoding(utf8)), set_stream(Out, encoding(utf8)),
    assertz(transport(In,Out)),
    set_prolog_flag(debug_on_error, false),
    current_prolog_flag(version_data, swi(Major,Minor,Patch,_)),
    format(string(Version), '~d.~d.~d', [Major,Minor,Patch]),
    text_hex(Version, VH), send(["HELLO","1",VH]),
    loop.

loop :-
    transport(In,_), read_line_to_string(In, Line),
    ( Line == end_of_file -> true
    ; split_string(Line, "\t", "", Fields),
      ( Fields = ["QUIT"] -> true
      ; catch((dispatch(Fields) -> true ; throw(error(protocol_failed, _))), E,
              protocol_error(Fields,E)), loop ) ).

protocol_error(Fields, E) :-
    ( Fields = [_,Id|_] -> true ; Id = "0" ),
    term_string(E, Text, [quoted(true),max_depth(20)]),
    text_hex(Text, H), send(["DONE",Id,"error","0",H]).

dispatch(["CONSULT",Id,PathHex]) :- !,
    retractall(pending_reply(_,_,_)),
    text_hex(Path,PathHex),
    with_request(Id, guarded_consult(Path, Status, Detail)),
    text_hex(Detail,H), send(["DONE",Id,Status,"0",H]).
dispatch(["QUERY",Id,MsS,InfS,MaxS,GoalHex,FactsHex]) :- !,
    retractall(pending_reply(_,_,_)),
    number_string(Ms,MsS), number_string(Inferences,InfS), number_string(Max,MaxS),
    between(1,600000,Ms), between(1,1000000000,Inferences), between(1,10000,Max),
    number_string(S,Id), text_hex(GoalText,GoalHex), text_hex(FactText,FactsHex),
    with_request(Id,
      setup_call_cleanup(true,
        run_query(S,Ms,Inferences,Max,GoalText,FactText,Status,Count,Detail),
        wd:clear_snapshot)),
    text_hex(Detail,H), number_string(Count,CS), send(["DONE",Id,Status,CS,H]).
% An inference exception can unwind after an RPC has been sent. The host always
% sends that reply before its next query. Consume it without emitting a new DONE.
dispatch(["MEM",Id,Seq,_,_,_]) :- retract(pending_reply(Id,Seq,"MEM")), !.
dispatch(["ADDRESS",Id,Seq,_,_]) :- retract(pending_reply(Id,Seq,"ADDRESS")), !.
dispatch(_) :- domain_error(protocol_command, invalid).

with_request(Id, Goal) :-
    setup_call_cleanup(
        (assertz(active_request(Id)), nb_setval(pd_output,0), nb_setval(pd_rpc,0)),
        with_user_io(Goal),
        retractall(active_request(_))).

:- meta_predicate with_request(+,0), with_user_io(0).
with_user_io(Goal) :-
    current_input(OldIn), current_output(OldOut),
    stream_property(UserOut,alias(user_output)), stream_property(UserErr,alias(user_error)),
    stream_property(UserIn,alias(user_input)),
    setup_call_cleanup(
        (open_string("",NullIn), open_prolog_stream(prologdbg_worker,write,Redirect,[]),
         set_stream(Redirect,buffer(line)), set_stream(Redirect,buffer_size(4096))),
        setup_call_cleanup(
            (set_stream(NullIn,alias(user_input)), set_input(NullIn),
             set_stream(Redirect,alias(user_output)), set_stream(Redirect,alias(user_error)),
             set_output(Redirect)),
            Goal,
            (catch(flush_output(Redirect),_,true),
             set_stream(UserIn,alias(user_input)), set_stream(UserOut,alias(user_output)),
             set_stream(UserErr,alias(user_error)), set_input(OldIn), set_output(OldOut))),
        (close(NullIn),close(Redirect,[force(true)]))).

stream_read(_, "").
stream_close(_).
stream_write(_, Text) :-
    ( active_request(Id) ->
      string_length(Text,N), nb_getval(pd_output,Old), New is Old+N,
      ( New =< 1048576 ->
        nb_setval(pd_output,New), text_hex(Text,H), send(["OUTPUT",Id,H])
      ; throw(error(resource_error(output_limit),_)) )
    ; true ).

guarded_consult(Path,Status,Detail) :-
    setup_call_cleanup(
      (assertz(consulting),nb_setval(pd_load_errors,0)),
      catch((call_with_time_limit(15,load_files(user:Path,[if(true),silent(true)]))
             -> nb_getval(pd_load_errors,Errors),
                ( Errors =:= 0 -> Status="ok",Detail="File consulted"
                ; Status="error",Detail="Consult reported errors; some clauses may have loaded" )
             ; Status="error",Detail="Consult failed"),
            E, exception_result(E,Status,Detail)),
      retractall(consulting)).

run_query(S,Ms,Inferences,Max,GoalText,FactText,Status,Count,Detail) :-
    nb_setval(pd_answers,0),
    Seconds is Ms/1000,
    catch(
      call_with_time_limit(Seconds,
        ( read_term_from_atom(FactText,Facts,[syntax_errors(error)]),
          ( Facts == [] -> wd:clear_snapshot ; wd:install_snapshot(S,Facts) ),
          read_goal(GoalText,Goal,Vars),
          call_with_inference_limit(enumerate(Goal,Vars,Max),Inferences,Limit),
          ( Limit == inference_limit_exceeded ->
              Status="inference_limit",Detail="Inference budget exhausted; results are incomplete"
          ; nb_getval(pd_answers,C),
            ( C =:= 0 -> Status="false" ; Status="ok" ),
            Detail="Search exhausted for the captured evidence" ) )),
      E, exception_result(E,Status,Detail)),
    nb_getval(pd_answers,Count).

read_goal(Text,Goal,Vars) :-
    catch(read_goal_stream(Text,Goal,Vars), error(syntax_error(end_of_file),_),
          (string_concat(Text,"\n.",WithPeriod), read_goal_stream(WithPeriod,Goal,Vars))).
read_goal_stream(Text,Goal,Vars) :-
    setup_call_cleanup(open_string(Text,In),
      (read_term(In,Goal,[variable_names(Vars),module(user),syntax_errors(error)]),
       (Goal == end_of_file -> domain_error(prolog_goal,empty) ; true),
       read_term(In,Trailing,[module(user),syntax_errors(error)]),
       (Trailing == end_of_file -> true ; domain_error(single_prolog_goal,Trailing))),
      close(In)).

enumerate(Goal,Vars,Max) :-
    ( call(user:Goal),
      nb_getval(pd_answers,N), Next is N+1, nb_setval(pd_answers,Next),
      copy_term(Vars,Copy,Residual),
      ( Vars == [] -> Answer=true ; Answer=bindings(Copy,Residual) ),
      term_string(Answer,Text,[quoted(true),numbervars(true),max_depth(40),cycles(true)]),
      string_length(Text,Size),
      ( Size =< 65536 -> true ; throw(error(resource_error(answer_size),_)) ),
      active_request(Id), text_hex(Text,H), send(["ANSWER",Id,H]),
      ( Next >= Max -> throw(pd_answer_limit) ; fail )
    ; true ).

exception_result(pd_answer_limit,"answer_limit","Answer limit reached; additional answers were not sought") :- !.
exception_result(time_limit_exceeded,"timeout","Time budget exhausted; results are incomplete") :- !.
exception_result(E,"error",Text) :- term_string(E,Text,[quoted(true),max_depth(20)]).

read_memory(S,Address,Length,Result) :-
    require_main_thread,
    active_request(Id), number_string(S,Id), next_rpc(Seq),
    format(string(A), '~16r', [Address]), number_string(Length,L),
    assertz(pending_reply(Id,Seq,"MEM")),
    sig_atomic((send(["READ",Id,Seq,A,L]),
                receive(["MEM",Id,Seq,Status,DataHex,ReasonHex]))),
    retractall(pending_reply(Id,Seq,"MEM")),
    hex_bytes(DataHex,Bytes), text_hex(Reason,ReasonHex),
    ( Status == "ok", length(Bytes,Length) -> Result=ok(Bytes)
    ; Status == "partial", length(Bytes,Count), Count > 0, Count =< Length ->
        Result=partial(Bytes,Length,Reason)
    ; Status == "unavailable", Bytes == [] -> Result=unavailable(Reason)
    ; throw(error(protocol_memory_response,_)) ).

lookup_symbol(S,Name,Result) :-
    require_main_thread,
    active_request(Id), number_string(S,Id), next_rpc(Seq),
    atom_string(Name,Text), text_hex(Text,H),
    assertz(pending_reply(Id,Seq,"ADDRESS")),
    sig_atomic((send(["LOOKUP",Id,Seq,H]),
                receive(["ADDRESS",Id,Seq,Status,Value]))),
    retractall(pending_reply(Id,Seq,"ADDRESS")),
    ( Status == "ok" -> string_concat("0x",Value,Number), number_string(A,Number), Result=ok(A)
    ; Status == "unavailable" -> text_hex(Reason,Value), Result=unavailable(Reason)
    ; throw(error(protocol_lookup_response,_)) ).

require_main_thread :-
    thread_self(Self),
    (Self == main -> true ; permission_error(call,debugger_bridge_from_thread,Self)).

next_rpc(Seq) :- nb_getval(pd_rpc,N), Next is N+1, nb_setval(pd_rpc,Next), number_string(Next,Seq).
receive(Fields) :-
    transport(In,_), read_line_to_string(In,Line),
    ( Line == end_of_file -> throw(error(protocol_eof,_))
    ; split_string(Line,"\t","",Got),
      (Got=Fields -> true ; throw(error(protocol_response(Got),_))) ).
send(Fields) :-
    sig_atomic(send_line(Fields)).
send_line(Fields) :-
    transport(_,Out), atomics_to_string(Fields,"\t",Line),
    format(Out,'~s~n',[Line]), flush_output(Out).

text_hex(Text,Hex) :-
    ( nonvar(Text) -> string_codes(Text,Codes), phrase(utf8_codes(Codes),Bytes), hex_bytes(Hex,Bytes)
    ; hex_bytes(Hex,Bytes), phrase(utf8_codes(Codes),Bytes), string_codes(Text,Codes) ).
hex_bytes(Hex,Bytes) :-
    ( nonvar(Bytes) -> phrase(hex_list(Bytes),Codes), string_codes(Hex,Codes)
    ; string_codes(Hex,Codes), (phrase(hex_list(Bytes),Codes) -> true ; domain_error(hex,Hex)) ).
hex_list([]) --> [].
hex_list([B|Bs]) -->
    { nonvar(B) -> Hi is B>>4, Lo is B /\ 15 ; true },
    hex_char(Hi), hex_char(Lo), { B is (Hi<<4) \/ Lo }, hex_list(Bs).
hex_char(N) --> [C],
    { nonvar(N) -> (N<10 -> C is 0'0+N ; C is 0'a+N-10)
    ; (between(0'0,0'9,C) -> N is C-0'0
      ; between(0'a,0'f,C) -> N is C-0'a+10
      ; between(0'A,0'F,C) -> N is C-0'A+10) }.
