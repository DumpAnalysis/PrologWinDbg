:- use_module(library(plunit)).
:- use_module('../prolog/wd').
:- use_module('../examples/patterns').
:- multifile wd:memory_provider/4.
wd:memory_provider(1,4096,8,ok([80,82,79,76,79,71,68,66])).
wd:memory_provider(1,8192,8,partial([80,82],8,"missing page")).
wd:memory_provider(1,9000,8,ok([0,0,0,0,0,0,0,128])).

fixture :- wd:install_snapshot(1,[target(1,dump,123,8),thread(1,0),thread(1,1),
  thread_id(1,0,100),thread_id(1,1,101),stack_frame(1,0,0,4096),stack_frame(1,1,0,4096),
  symbol(1,4096,myapp,'WaitForRelease',0),
  coverage(1,stack(0),captured_prefix(1,unverified_end)),
  coverage(1,stack(1),captured_prefix(1,unverified_end))]).
:- begin_tests(rules,[setup(fixture),cleanup(wd:clear_snapshot)]).
test(stack_match) :- stack_contains(1,0,myapp,'WaitForRelease').
test(shared,all(A-B==[0-1])) :- shared_function(1,A,B,myapp,'WaitForRelease').
test(magic) :- inspect_magic(1,4096,matched(prologdb,4096)).
test(partial_is_inconclusive) :- inspect_magic(1,8192,inconclusive(partial([80,82],8,"missing page"))).
test(exact_read_throws,[throws(error(incomplete_memory(_,_,_),_))]) :- bytes(1,8192,8,_).
test(unsigned_pointer) :- pointer(1,9000,9223372036854775808).
test(stale,[throws(error(existence_error(debugger_snapshot,7),_))]) :- memory(7,4096,1,_).
test(overflow,[throws(error(domain_error(memory_range,_),_))]) :- memory(1,0xffffffffffffffff,2,_).
test(too_large,[throws(error(domain_error(memory_range,_),_))]) :- memory(1,0,65537,_).
test(evidence) :- stack_evidence(1,0,evidence([frame(0,4096)],captured_prefix(1,unverified_end))).
test(no_provider) :- memory(1,12345,1,unavailable(no_memory_provider)).
test(reject_directive,[throws(error(domain_error(snapshot_fact,_),_))]) :- wd:install_snapshot(1,[(:-halt)]).
:- end_tests(rules).
