#!/usr/bin/env python3
# northlight-test:
"""Real runner planning/reports reject missing coverage, skips, stale inputs and reused evidence."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.SCRIPTS))
import check_layout
import run_tests as runner
REAL_GIT_FILES=check_layout.git_files


class ProjectHarness(unittest.TestCase):
    def setUp(self):
        self.stack=contextlib.ExitStack();self.addCleanup(self.stack.close)
        self.root=Path(self.stack.enter_context(tempfile.TemporaryDirectory(prefix='northlight-harness-')))
        self.tests=self.root/'tests';self.tests.mkdir()
        self.test=self.tests/'test_sample.py';self.test.write_text('# northlight-test:\n',encoding='utf-8')
        self.inventory=self.tests/'discovery.txt';self.inventory.write_text(self.test.name+'\n',encoding='utf-8')
        self.suites=self.tests/'suites.json'
        self.suites.write_text(json.dumps({'source':{'description':'Fixture contract','tests':[self.test.name]}}),encoding='utf-8')
        self.source=self.root/'src/core/input.h';self.source.parent.mkdir(parents=True);self.source.write_text('before\n')
        for name,value in [('REPO',self.root),('TESTS',self.tests),('RECORDS',self.root/'renderer/records')]:
            self.stack.enter_context(mock.patch.object(runner.fp,name,value))
        self.stack.enter_context(mock.patch.object(runner.fp,'out',return_value=self.root/'out'))
        self.stack.enter_context(mock.patch.object(runner.fp,'protect_output',side_effect=Path))
        self.stack.enter_context(mock.patch.object(runner,'DISCOVERY',self.inventory))
        self.stack.enter_context(mock.patch.object(runner,'SUITES',self.suites))
        self.stack.enter_context(mock.patch.object(runner.check_layout,'check',return_value=[]))
        self.stack.enter_context(mock.patch.object(runner.check_layout,'git_files',return_value=[
            'tests/test_sample.py','tests/discovery.txt','tests/suites.json','src/core/input.h']))
        self.stack.enter_context(mock.patch.object(runner,'git_state',return_value={'commit':'abc','branch':'current','status':[]}))
        self.probe=self.stack.enter_context(mock.patch.object(runner,'probe',return_value=None))
        self.run=self.stack.enter_context(mock.patch.object(runner.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'passed','')))
        self.output=self.root/'out/run'

    def invoke(self,*arguments):
        self.stdout=io.StringIO()
        with contextlib.redirect_stdout(self.stdout),contextlib.redirect_stderr(io.StringIO()):
            return runner.main(['--no-nice','--output',str(self.output),*arguments])

    def report(self):
        return json.loads((self.output/'results.json').read_text())

    def test_pass_report_binds_selection_commands_and_inputs(self):
        self.assertEqual(self.invoke('--suite','source','--strict'),0)
        report=self.report()
        self.assertEqual(report['status'],'PASS');self.assertTrue(report['all_passed'])
        self.assertEqual(report['input_identity'],report['input_identity_after'])
        self.assertIn('src/core/input.h',report['inputs'])
        self.assertEqual(report['tests'][self.test.name]['command'],[sys.executable,str(self.test)])
        self.assertTrue((self.output/'logs/test_sample.log').is_file())

    def test_unmatched_pattern_cannot_hide_behind_a_valid_match(self):
        with self.assertRaises(SystemExit) as error:self.invoke(self.test.name,'test_typo')
        self.assertEqual(error.exception.code,2);self.run.assert_not_called()

    def test_selection_still_checks_the_complete_inventory(self):
        (self.tests/'test_unlisted.py').write_text('# northlight-test:\n')
        self.assertEqual(self.invoke(self.test.name),2);self.run.assert_not_called()

    def test_list_never_probes_or_runs_native_tools(self):
        self.test.write_text('# northlight-test: requires=windows-gpu,zig\n')
        self.assertEqual(self.invoke('--suite','source','--list'),0)
        self.probe.assert_not_called();self.run.assert_not_called();self.assertFalse(self.output.exists())

    def test_strict_skip_is_incomplete_not_pass(self):
        self.test.write_text('# northlight-test: requires=zig\n');self.probe.return_value='missing compiler'
        self.assertEqual(self.invoke('--strict'),1)
        self.assertEqual(self.report()['status'],'INCOMPLETE');self.assertFalse(self.report()['all_passed'])
        self.assertTrue(self.stdout.getvalue().splitlines()[-1].startswith('INCOMPLETE:'))
        self.run.assert_not_called()

    def test_failed_prerequisite_probe_is_recorded(self):
        self.test.write_text('# northlight-test: requires=zig\n')
        self.probe.side_effect=subprocess.CalledProcessError(1,['zig'])
        self.assertEqual(self.invoke('--strict'),1)
        self.assertIn('prerequisite probe failed',self.report()['requirements']['zig'])
        self.assertEqual(self.report()['status'],'INCOMPLETE')

    def test_optional_skip_is_still_reported_incomplete(self):
        self.test.write_text('# northlight-test: requires=zig\n');self.probe.return_value='missing compiler'
        self.assertEqual(self.invoke(),0);self.assertEqual(self.report()['status'],'INCOMPLETE')

    def test_known_failure_does_not_satisfy_a_strict_gate(self):
        self.test.write_text('# northlight-test: known-fail=fixture\n');self.run.return_value.returncode=1
        self.assertEqual(self.invoke('--strict'),1)
        self.assertEqual(self.report()['tests'][self.test.name]['status'],'XFAIL')

    def test_failed_test_and_timeout_keep_diagnostics(self):
        self.run.return_value=subprocess.CompletedProcess([],7,'','failure details')
        self.assertEqual(self.invoke(),1);self.assertEqual(self.report()['status'],'FAIL')
        self.assertIn('failure details',(self.output/'logs/test_sample.log').read_text())

    def test_timeout_preserves_partial_output(self):
        self.run.side_effect=subprocess.TimeoutExpired(['python'],1,output=b'partial output',stderr=b'error')
        self.assertEqual(self.invoke(),1)
        self.assertEqual(self.report()['counts']['TIMEOUT'],1)
        self.assertIn('partial output',(self.output/'logs/test_sample.log').read_text())

    def test_source_drift_invalidates_otherwise_passing_results(self):
        def change(*args,**kwargs):
            self.source.write_text('after\n');return subprocess.CompletedProcess([],0,'passed','')
        self.run.side_effect=change
        self.assertEqual(self.invoke('--strict'),1)
        self.assertEqual(self.report()['status'],'STALE')
        self.assertEqual(self.report()['changed_inputs'],['src/core/input.h'])

    def test_head_drift_invalidates_unchanged_file_hashes(self):
        with mock.patch.object(runner,'git_state',side_effect=[{'commit':'abc'},{'commit':'def'}]):
            self.assertEqual(self.invoke('--strict'),1)
        self.assertEqual(self.report()['status'],'STALE');self.assertEqual(self.report()['changed_inputs'],[])

    def test_git_failure_cannot_become_an_empty_inventory(self):
        self.run.return_value=subprocess.CompletedProcess([],128,b'',b'broken index')
        with self.assertRaisesRegex(RuntimeError,'Cannot inventory the checkout'):REAL_GIT_FILES()

    def test_existing_output_is_never_overwritten(self):
        self.output.mkdir(parents=True);sentinel=self.output/'previous.json';sentinel.write_text('old evidence')
        with self.assertRaises(SystemExit) as error:self.invoke()
        self.assertEqual(error.exception.code,2);self.assertEqual(sentinel.read_text(),'old evidence')
        self.run.assert_not_called()

    def test_saved_pass_is_rejected_after_later_source_changes(self):
        self.assertEqual(self.invoke('--strict'),0)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(runner.verify_report(self.output/'results.json'),0)
            self.source.write_text('changed after the run\n')
            self.assertEqual(runner.verify_report(self.output/'results.json'),1)

    def test_saved_incomplete_report_never_verifies(self):
        self.test.write_text('# northlight-test: requires=zig\n');self.probe.return_value='not installed'
        self.assertEqual(self.invoke(),0)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(runner.verify_report(self.output/'results.json'),1)

    def test_update_inventory_does_not_execute_the_suite(self):
        self.inventory.write_text('')
        self.assertEqual(self.invoke('--update-discovery'),0)
        self.assertEqual(self.inventory.read_text(),self.test.name+'\n');self.run.assert_not_called()
        self.assertEqual(self.inventory.read_bytes(),(self.test.name+'\n').encode('utf-8'))

    def test_root_layout_rejects_ignored_sources_and_new_root_commands(self):
        (self.root/'northlight_paths.py').write_text('')
        (self.root/'performance_monitor.h').write_text('stale copy')
        (self.root/'new_development_command.py').write_text('')
        problems=check_layout.root_problems(self.root)
        self.assertEqual(len(problems),2)
        self.assertTrue(any('including ignored copies' in p for p in problems))

    def test_broken_suite_definition_fails_before_execution(self):
        self.suites.write_text(json.dumps({'source':{'description':'bad','tests':['test_missing.py']}}))
        with self.assertRaises(SystemExit) as error:self.invoke('--suite','source')
        self.assertEqual(error.exception.code,2);self.run.assert_not_called()


if __name__=='__main__':unittest.main()
