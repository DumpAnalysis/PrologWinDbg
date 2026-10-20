"""End-to-end tests against the real SWI worker, using a simulated debugger peer."""
import pathlib
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SWIPL = sys.argv.pop(1) if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else shutil.which('swipl')

def hx(text):
    return text.encode('utf-8').hex()

def unhx(text):
    return bytes.fromhex(text).decode('utf-8')

class Peer:
    def __init__(self):
        self.process = subprocess.Popen([SWIPL, '-q', '-f', 'none', '--no-packs', '--stack-limit=512m',
            '-s', str(ROOT / 'prolog' / 'worker.pl'), '-g', 'prologdbg_worker:main', '-t', 'halt', '--'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding='utf-8', bufsize=1)
        self.queue = queue.Queue()
        self.errors = []
        self.reader = threading.Thread(target=self._reader, daemon=True)
        self.err_reader = threading.Thread(target=self._errors, daemon=True)
        self.reader.start(); self.err_reader.start()
        hello = self.read()
        if hello[:2] != ['HELLO', '1']:
            raise AssertionError((hello, self.errors))
        self.version = unhx(hello[2])
        self.counter = 0
        self.reads = 0
        self.read_delay = 0

    def _reader(self):
        for line in self.process.stdout:
            self.queue.put(line.rstrip('\r\n').split('\t'))
        self.queue.put(None)

    def _errors(self):
        self.errors.extend(self.process.stderr)

    def send(self, *fields):
        self.process.stdin.write('\t'.join(map(str, fields)) + '\n')
        self.process.stdin.flush()

    def read(self, timeout=8):
        item = self.queue.get(timeout=timeout)
        if item is None:
            raise AssertionError('Worker exited: ' + ''.join(self.errors))
        return item

    def close(self):
        if self.process.poll() is None:
            self.send('QUIT')
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill(); self.process.wait(timeout=2)
        self.reader.join(timeout=1); self.err_reader.join(timeout=1)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()

    def collect(self, ident):
        answers, output = [], []
        while True:
            fields = self.read()
            if fields[1] != str(ident):
                raise AssertionError(fields)
            tag = fields[0]
            if tag == 'ANSWER':
                answers.append(unhx(fields[2]))
            elif tag == 'OUTPUT':
                output.append(unhx(fields[2]))
            elif tag == 'READ':
                self.reads += 1
                if self.read_delay:
                    time.sleep(self.read_delay)
                address, count = int(fields[3], 16), int(fields[4])
                if address == 4096:
                    data = b'PROLOGDB'[:count]
                    state = 'ok' if len(data) == count else 'partial'
                elif address == 8192:
                    data, state = b'AB'[:count], 'partial' if count > 2 else 'ok'
                elif count == 0:
                    data, state = b'', 'ok'
                else:
                    data, state = b'', 'unavailable'
                self.send('MEM', ident, fields[2], state, data.hex(), hx('test memory result'))
            elif tag == 'LOOKUP':
                name = unhx(fields[3])
                if name == 'target!g_magic':
                    self.send('ADDRESS', ident, fields[2], 'ok', '1000')
                elif name == 'target!high':
                    self.send('ADDRESS', ident, fields[2], 'ok', 'fedcba9876543210')
                else:
                    self.send('ADDRESS', ident, fields[2], 'unavailable', hx('symbol missing'))
            elif tag == 'DONE':
                return fields[2], answers, ''.join(output), unhx(fields[4])
            else:
                raise AssertionError(fields)

    def query(self, goal, facts=None, ms=2000, inference=1000000, answers=100):
        self.counter += 1
        s = self.counter
        if facts is None:
            facts = (f'[target({s},dump,42,8),thread({s},0),thread_id({s},0,200),'
                     f'stack_frame({s},0,0,4096),symbol({s},4096,myapp,work,0),'
                     f'coverage({s},stack(0),captured_prefix(1,unverified_end))]')
        self.send('QUERY', s, ms, inference, answers, hx(goal), hx(facts))
        return self.collect(s)

    def consult(self, path):
        self.counter += 1
        self.send('CONSULT', self.counter, hx(str(path)))
        return self.collect(self.counter)

class WorkerTests(unittest.TestCase):
    def setUp(self):
        self.peer = Peer()

    def tearDown(self):
        self.peer.close()

    def test_arithmetic_and_multiple_solutions(self):
        status, answers, _, _ = self.peer.query('member(X,[1,2,3]), Y is X*X.')
        self.assertEqual(status, 'ok'); self.assertEqual(len(answers), 3)
        self.assertIn('9', answers[-1])

    def test_snapshot_and_stale_memory(self):
        self.assertEqual(self.peer.query('wd:current_snapshot(S), wd:thread(S,T).')[0], 'ok')
        self.assertEqual(self.peer.query('wd:memory(1,4096,8,_).')[0], 'error')
        self.assertEqual(self.peer.query('wd:current_snapshot(S), S = 3.')[0], 'ok')

    def test_memory_lookup_and_cache(self):
        q = ('wd:current_snapshot(S), wd:lookup(S,\'target!g_magic\',ok(A)), '
             'wd:memory(S,A,8,X), wd:memory(S,A,8,Y), X=Y.')
        status, answers, _, _ = self.peer.query(q)
        self.assertEqual(status, 'ok'); self.assertEqual(self.peer.reads, 1)
        self.assertIn('[80,82,79,76,79,71,68,66]', answers[0].replace(' ', ''))

    def test_unsigned_addresses(self):
        status, answers, _, _ = self.peer.query("wd:current_snapshot(S), wd:lookup(S,'target!high',ok(A)).")
        self.assertEqual(status,'ok'); self.assertIn(str(0xfedcba9876543210), answers[0])

    def test_native_architectures_and_snapshot_cleanup(self):
        for architecture in ('arm64', 'amd64'):
            with self.subTest(architecture=architecture):
                s = self.peer.counter + 1
                facts = f'[target({s},dump,42,8),architecture({s},{architecture})]'
                goal = (f'wd:current_snapshot(S), wd:architecture(S,{architecture}), '
                        'wd:pointer(S,4096,P), P = 4775019910827823696, '
                        "wd:lookup(S,'target!high',ok(18364758544493064720)).")
                self.assertEqual(self.peer.query(goal, facts=facts)[0], 'ok')
                # The following legacy snapshot deliberately omits architecture.
                self.assertEqual(self.peer.query('\\+ wd:architecture(_,_).')[0], 'ok')

    def test_reject_foreign_architecture_snapshot(self):
        self.assertEqual(self.peer.query('true.', facts='[architecture(99,arm64)]')[0], 'error')
        self.assertEqual(self.peer.query('X is 2+2.')[0], 'ok')

    def test_partial_and_unavailable(self):
        status, answers, _, _ = self.peer.query('wd:current_snapshot(S), wd:memory(S,8192,8,R).')
        self.assertEqual(status,'ok'); self.assertIn('partial',answers[0])
        status, answers, _, _ = self.peer.query('wd:current_snapshot(S), wd:memory(S,0,8,R).')
        self.assertEqual(status,'ok'); self.assertIn('unavailable',answers[0])

    def test_exact_partial_throws_and_worker_survives(self):
        self.assertEqual(self.peer.query('wd:current_snapshot(S), wd:bytes(S,8192,8,_).')[0],'error')
        self.assertEqual(self.peer.query('X is 2+2.')[0],'ok')

    def test_query_output_all_standard_streams(self):
        status, _, output, _ = self.peer.query("writeln(hello), format(user_output,'world~n',[]), format(user_error,'oops~n',[]).")
        self.assertEqual(status,'ok')
        for text in ('hello','world','oops'):
            self.assertIn(text,output)

    def test_unicode_and_quotes(self):
        text = "Dmitry — Пролог 日本語"
        status, answers, output, _ = self.peer.query(f"X = '{text}', writeln(X).")
        self.assertEqual(status,'ok'); self.assertIn(text,answers[0]); self.assertIn(text,output)

    def test_consult_reload_and_pattern(self):
        self.assertEqual(self.peer.consult(ROOT/'examples'/'patterns.pl')[0],'ok')
        self.assertEqual(self.peer.query('wd:current_snapshot(S), patterns:inspect_magic(S,4096,R).')[0],'ok')
        with tempfile.TemporaryDirectory() as directory:
            file = pathlib.Path(directory)/'space rules.pl'
            file.write_text('custom_value(17).\n',encoding='utf-8')
            self.assertEqual(self.peer.consult(file)[0],'ok')
            self.assertIn('17',self.peer.query('custom_value(X).')[1][0])
            file.write_text('custom_value(23).\n',encoding='utf-8')
            self.assertEqual(self.peer.consult(file)[0],'ok')
            self.assertIn('23',self.peer.query('custom_value(X).')[1][0])

    def test_syntax_error_and_false(self):
        self.assertEqual(self.peer.query('this is ( .')[0],'error')
        self.assertEqual(self.peer.query('fail.')[0],'false')
        self.assertEqual(self.peer.query('throw(my_error).')[0],'error')

    def test_optional_period_and_reject_trailing_goal(self):
        self.assertEqual(self.peer.query('X is 2+2 % comment')[0],'ok')
        status, _, output, _ = self.peer.query("writeln(first). writeln(second).")
        self.assertEqual(status,'error'); self.assertEqual(output,'')

    def test_consult_syntax_error_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            file = pathlib.Path(directory)/'broken.pl'
            file.write_text('broken( :- .\n',encoding='utf-8')
            self.assertEqual(self.peer.consult(file)[0],'error')
        self.assertEqual(self.peer.query('true.')[0],'ok')

    def test_background_memory_request_is_rejected(self):
        status, answers, _, _ = self.peer.query(
            'wd:current_snapshot(S), thread_create(wd:memory(S,4096,8,_),T,[]), '
            'thread_join(T,exception(error(permission_error(call,debugger_bridge_from_thread,_),_))).')
        self.assertEqual(status,'ok'); self.assertEqual(self.peer.reads,0)

    def test_answer_limit(self):
        status, answers, _, _ = self.peer.query('between(1,1000,X).',answers=3)
        self.assertEqual(status,'answer_limit'); self.assertEqual(len(answers),3)

    def test_inference_limit(self):
        status, _, _, _ = self.peer.query('repeat, fail.',inference=2000)
        self.assertEqual(status,'inference_limit')
        self.assertEqual(self.peer.query('true.')[0],'ok')

    def test_time_limit(self):
        start = time.monotonic()
        self.assertEqual(self.peer.query('sleep(2).',ms=100)[0],'timeout')
        self.assertLess(time.monotonic()-start,1.5)
        self.assertEqual(self.peer.query('true.')[0],'ok')

    def test_timeout_during_memory_reply_keeps_protocol_synchronized(self):
        self.peer.read_delay = 0.2
        self.assertEqual(self.peer.query('wd:current_snapshot(S), wd:memory(S,4096,8,_).',ms=50)[0],'timeout')
        self.assertEqual(self.peer.query('true.')[0],'ok')

    def test_inference_exhaustion_at_rpc_boundaries(self):
        for budget in range(20,501,20):
            with self.subTest(budget=budget):
                status, _, _, _ = self.peer.query('wd:current_snapshot(S), wd:memory(S,4096,8,_).',inference=budget)
                self.assertIn(status, ('inference_limit','ok'))
                self.assertEqual(self.peer.query('true.')[0],'ok')

    def test_empty_snapshot_has_no_debugger_access(self):
        self.assertEqual(self.peer.query('wd:current_snapshot(_).',facts='[]')[0],'error')

    def test_invalid_snapshot_is_rejected(self):
        self.assertEqual(self.peer.query('true.',facts='[(:-halt)]')[0],'error')
        self.assertEqual(self.peer.query('true.')[0],'ok')

    def test_zero_and_overflow_reads(self):
        self.assertEqual(self.peer.query('wd:current_snapshot(S), wd:memory(S,0,0,ok([])).')[0],'ok')
        before = self.peer.reads
        self.assertEqual(self.peer.query('wd:current_snapshot(S), wd:memory(S,18446744073709551615,2,_).')[0],'error')
        self.assertEqual(self.peer.reads,before)

    def test_query_cannot_consume_protocol_input(self):
        self.assertEqual(self.peer.query('read(X), X=end_of_file.')[0],'ok')
        self.assertEqual(self.peer.query('true.')[0],'ok')

    def test_worker_exit_can_be_restarted(self):
        self.peer.send('QUERY', 99, 2000, 100000, 10, hx('halt.'), hx('[]'))
        self.peer.process.wait(timeout=3)
        self.peer.close()
        self.peer = Peer()
        self.assertEqual(self.peer.query('true.')[0],'ok')

if __name__ == '__main__':
    if not SWIPL:
        raise SystemExit('Install SWI-Prolog or pass the path to swipl.exe')
    unittest.main(verbosity=2)
