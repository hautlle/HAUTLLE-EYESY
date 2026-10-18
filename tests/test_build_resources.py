"""Small local files and fake processes/privileges; never allocates or enables swap."""
import importlib.util
import json
from pathlib import Path
import signal
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('resource_recipe', ROOT/'build-support/build_resources.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class Process:
    pid = 12345
    def __init__(self, owner): self.owner, self.polls = owner, 0
    def poll(self):
        self.polls += 1
        return None if self.polls <= self.owner.running_polls else self.owner.exit_code
    def wait(self, timeout=None): return self.owner.exit_code


class Operations:
    def __init__(self):
        self.ram = 600*r.MIB
        self.swap_free = 1024*r.MIB
        self.space = 4096*r.MIB
        self.entries, self.calls, self.signals = {}, [], []
        self.tick, self.alive, self.running_polls, self.exit_code = 0, False, 2, 0
        self.fail = None
    def memory(self): return dict(MemAvailable=self.ram, SwapTotal=1024*r.MIB, SwapFree=self.swap_free)
    def swaps(self): return dict(self.entries)
    def free(self, path): return self.space
    def privileged(self, args):
        self.calls.append(list(args))
        if args[0] == self.fail: raise r.ResourceError('fixture privilege failure')
        if args[0] == '/sbin/swapon': self.entries[args[-1]] = 8*r.MIB
        if args[0] == '/sbin/swapoff': self.entries.pop(args[-1])
    def group_exists(self, group): return self.alive
    def signal_group(self, group, sig): self.signals.append((group,sig));self.alive = False
    def clock(self): return self.tick
    def sleep(self, seconds): self.tick += seconds
    def launch(self, command, cwd, log, env):
        self.alive = True
        self.command, self.env = command, env
        return Process(self)


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        self.ops = Operations()
        self.job = r.BuildResources(self.path,self.path,self.path,self.ops)
        small = mock.patch.object(r,'SWAP_BYTES',4096)
        small.start();self.addCleanup(small.stop)

    def test_success_removes_only_own_file_and_records_commands(self):
        other = self.path/'keep.txt';other.write_text('keep')
        with self.job.session() as job: job.run(['python3','build.py'],'engine',100)
        state = json.loads(self.job.record.read_text())
        self.assertTrue(state['complete'] and state['swap_cleanup_verified'])
        self.assertFalse(self.job.path.exists())
        self.assertEqual(self.ops.entries,{})
        self.assertEqual(other.read_text(),'keep')
        self.assertEqual(state['stages'][0]['command'],['python3','build.py'])
        self.assertEqual(self.ops.env['TMPDIR'],str(self.path/'compiler-tmp'))
        self.assertEqual([c[0] for c in self.ops.calls],['true','chown','/sbin/mkswap','/sbin/swapon','/sbin/swapoff'])

    def test_unauthorized_privilege_fails_before_file_creation(self):
        self.ops.fail='true'
        with self.assertRaisesRegex(r.ResourceError,'privilege'):
            with self.job.session(): self.fail('must not build')
        self.assertEqual(list(self.path.iterdir()),[])

    def test_existing_swap_is_never_changed(self):
        self.ops.entries['/unrelated.swap']=0
        with self.assertRaisesRegex(r.ResourceError,'Existing swap'):
            with self.job.session(): self.fail('must not build')
        self.assertEqual(list(self.path.iterdir()),[])
        self.assertEqual(self.ops.calls,[])

    def test_insufficient_initial_ram_or_space_rejected(self):
        for field,value in [('ram',511*r.MIB),('space',799*r.MIB)]:
            ops=Operations();setattr(ops,field,value)
            with self.assertRaises(r.ResourceError): r.preflight(self.path,self.path,ops)
            self.assertEqual(ops.calls,[])

    def test_failed_swap_setup_cleans_unactivated_file(self):
        for operation in ('chown','/sbin/mkswap','/sbin/swapon'):
            with self.subTest(operation=operation):
                self.ops.fail=operation
                with self.assertRaisesRegex(r.ResourceError,'privilege'):
                    with self.job.session(): self.fail('must not build')
                self.assertFalse(self.job.path.exists())
                self.assertTrue(json.loads(self.job.record.read_text())['swap_cleanup_verified'])
                self.job.record.unlink()
                self.job=r.BuildResources(self.path,self.path,self.path,self.ops)

    def test_failed_compiler_terminates_group_and_cleans_swap(self):
        self.ops.exit_code=7
        with self.assertRaisesRegex(r.ResourceError,'exit 7'):
            with self.job.session() as job: job.run(['compiler'],'engine',100)
        self.assertTrue(self.ops.signals)
        self.assertFalse(self.job.path.exists())
        self.assertFalse(self.job.state['complete'])

    def test_group_cleanup_even_when_leader_has_exited(self):
        self.ops.running_polls=0
        with self.job.session() as job: job.run(['compiler'],'engine',100)
        self.assertEqual(self.ops.signals,[(12345,signal.SIGTERM)])

    def test_guard_failure_cleans_resources(self):
        with self.assertRaisesRegex(r.ResourceError,'RAM below 32'):
            with self.job.session() as job:
                self.ops.ram=31*r.MIB
                # Return memory after stop so swapoff can proceed safely.
                original=self.ops.signal_group
                def stop(group,sig): original(group,sig);self.ops.ram=600*r.MIB
                self.ops.signal_group=stop
                job.run(['compiler'],'engine',100)
        self.assertTrue(self.job.state['swap_cleanup_verified'])

    def test_insufficient_swapoff_ram_keeps_active_file_for_recovery(self):
        self.job.allocate();self.ops.ram=100*r.MIB
        with self.assertRaisesRegex(r.ResourceError,'Insufficient RAM'): self.job.cleanup()
        self.assertTrue(self.job.path.exists())
        self.assertIn(str(self.job.path),self.ops.entries)
        self.assertNotIn('/sbin/swapoff',[c[0] for c in self.ops.calls])
        self.ops.ram=600*r.MIB
        r.BuildResources(self.path,self.path,self.path,self.ops).cleanup(recovering=True)
        self.assertFalse(self.job.path.exists())

    def test_changed_inode_never_removed_or_swapoff_attempted(self):
        self.job.allocate()
        original=self.path/'original.swap'
        self.job.path.rename(original);self.job.path.write_text('replacement')
        with self.assertRaisesRegex(r.ResourceError,'identity changed'): self.job.cleanup()
        self.assertEqual(self.job.path.read_text(),'replacement')
        self.assertTrue(original.exists())
        self.assertNotIn('/sbin/swapoff',[c[0] for c in self.ops.calls])

    def test_recovery_live_or_invalid_group_refuses_without_signals(self):
        self.job.allocate()
        for group in (12345,1,'12345'):
            self.job.state['child_group']=group;self.job.save();self.ops.alive=True
            with self.assertRaisesRegex(r.ResourceError,'may still be running'):
                r.BuildResources(self.path,self.path,self.path,self.ops).cleanup(recovering=True)
        self.assertEqual(self.ops.signals,[])
        self.assertTrue(self.job.path.exists())
        self.ops.alive=False;self.job.state['child_group']=None;self.job.save();self.job.cleanup()

    def test_signal_handler_restored_after_interrupt_and_cleanup(self):
        previous=signal.getsignal(signal.SIGINT)
        with self.assertRaisesRegex(r.ResourceError,'interrupted'):
            with self.job.session(): signal.getsignal(signal.SIGINT)(signal.SIGINT,None)
        self.assertIs(signal.getsignal(signal.SIGINT),previous)
        self.assertFalse(self.job.path.exists())
        self.assertTrue(self.job.state['swap_cleanup_verified'])

    def test_existing_journal_is_preserved(self):
        self.job.record.write_text('previous journal')
        with self.assertRaisesRegex(r.ResourceError,'journal already exists'):
            with self.job.session(): self.fail('must not build')
        self.assertEqual(self.job.record.read_text(),'previous journal')
        self.assertEqual(self.ops.calls,[])

    def test_swapoff_failure_retains_file_and_reports_recovery(self):
        self.ops.fail='/sbin/swapoff'
        with self.assertRaisesRegex(r.ResourceError,'cleanup-build'):
            with self.job.session(): pass
        self.assertTrue(self.job.path.exists())
        self.assertFalse(json.loads(self.job.record.read_text())['swap_cleanup_verified'])


class GuardTests(unittest.TestCase):
    def test_exact_thresholds_and_sustained_timer(self):
        def reason(ram=512,swap=1024,data=800,root=400,elapsed=0,low=0):
            return r.guard_reason(dict(MemAvailable=ram*r.MIB,SwapFree=swap*r.MIB),
                                  data*r.MIB,root*r.MIB,elapsed,100,low)
        self.assertIsNone(reason(ram=32,low=2.99))
        self.assertIsNone(reason(ram=64,swap=320,data=640,root=320,elapsed=100,low=3))
        for args,text in [({'ram':31},'RAM below 32'),({'ram':63,'low':3},'for 3 seconds'),
                          ({'swap':255},'Swap free'),({'ram':64,'swap':319},'Combined'),
                          ({'data':639},'Data free'),({'root':319},'Root free'),
                          ({'elapsed':101},'time limit')]:
            self.assertIn(text,reason(**args))


if __name__=='__main__': unittest.main()
