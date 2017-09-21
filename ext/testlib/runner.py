'''
This module contains the implementation of the :class:`Runner` object. The
purpose of the runner is to run :class:`whimsy.suite.TestSuite` instances and
the collection of :class:`whimsy.test.TestCase` objects they contain.

The runner supports two outside methods of running items.

The first - :func:`Runner.run` - will run all TestSuite instances in the
runner's `suites` attribute.

The second - :func:`Runner.run_items` - takes in any number of items and will
attempt to run each of them in succession.

.. note:: When using this :func:`Runner.run_items` to run TestCase items not
    contained in a TestSuite a warning will be displayed since test cases are
    not garunteed/expected to be self-contained entities.  It is expected that
    they may fail due to not recieving fixtures from their containing
    TestSuite.

The :class:`Runner` also provides a callback interface through the
:class:`whimsy.result.ResultLogger` interface in :mod:`whimsy.result`. The
purpose of this interface is to allow loggers of different formats to recieve
test results in a streaming format without storing large pools of strings and
results.

.. seealso:: :mod:`result`

When either run function is used the :class:`Runner` object first calls
:func:`setup` on all :class:`Fixture` objects that are not marked `lazy_init`
and are required by at least one of the tests suites.  Once all these fixtures
have been set up the Runner begins to iterate through its test items.


The run of a suite takes the following steps:

1. Iterate through each TestCase passing suite level fixtures to them.

2. Before the run of the TestCase takes place, start capturing stdout and
   stderr to a directory named after the test case's uid.

3. Build any fixtures that the the test relies on, if
   a :func:`whimsy.fixture.Fixture.setup` call fails the test outcome will
   be marked as an ERROR and the test case will return.

4. The actual test execution takes place. If during the test an exeption is
   raised (other than the :class:`whimsy.test.TestSkipException`) then the
   test will be marked as a failure. Test writers can pass a test case early
   in their function by simply returning from the function. Test writers can
   skip their test while inside of it if they raise the TestSkipException by
   calling the :func:`whimsy.test.skip` function in :mod:`whimsy.test`.

Now returned from the test case with an outcome, we continue the same process
for the remaining tests in the test suite, however, there are a couple
remaining edge cases:

If the --fail-fast flag was given to us and a test fails. No more testing will
take place. All results will be written and fixtures torn down. But remaining
tests will be ignored.

If a test fails in a `fail_fast` TestList then all the remaining items in that
:class:`whimsy.suite.TestList` will be skipped.

.. note:: See the :class:`whimsy.suite.TestList` for details on the utility of
    this `fail_fast` feature or :func:`whimsy.gem5.suite.gem5_verify_config`
    for an example use case.

If a TestSuite is marked `fail_fast` and a test fails, then the remaining
TestCase instances in that TestSuite will be skipped.
'''
import traceback
import itertools

from terminal import separator
import test
import _util
from result import ConsoleLogger, Outcome, test_results_output_path
from config import config
from helper import mkdir_p, joinpath
from logger import log
from suite import TestSuite, SuiteList
from tee import tee
from test import TestCase


class Runner(object):
    '''
    The default runner class used for running test suites and cases.
    '''
    def __init__(self, suites=tuple(), result_loggers=tuple()):
        '''
        :param suites: An iterable containing suites which are run when
        :func:`run` is called.

        :param result_loggers: Iterable containing items supporting the
        `ResultLogger` interface .
        '''
        if not isinstance(suites, SuiteList):
            suites = SuiteList(suites)
        self.suites = suites
        if not result_loggers:
            result_loggers = (ConsoleLogger(),)
        self.result_loggers = tuple(result_loggers)

    @staticmethod
    def run_items(*items, **kwargs):
        '''
        Run the given items.

        :param items: Items to be ran.
        :param result_loggers: See :func:`__init__`

        .. warning:: It's generally not a good idea to run a `TestCase` on its
            own since most test cases are not self-contained. (They rely on
            suite fixtures and previous tests.)
            '''
        self = None
        if 'result_loggers' in kwargs and len(kwargs) == 1 :
            self = Runner(**kwargs)
        elif len(kwargs) != 0:
            raise ValueError('Only accepts result_loggers as an optional'
                             ' kwarg')
        else:
            self = Runner()

        for logger in self.result_loggers:
            logger.begin_testing()

        for item in items:
            if isinstance(item, TestCase):
                log.warn("Running '%s' as a TestCase it is likely not self"
                         "-contained!" % item.name)
                log.warn('Recommend running its containing suite instead.')
                outcome = self.run_test(item)
            elif isinstance(item, TestSuite):
                outcome = self.run_suite(item)
            else:
                raise AssertionError(_util.unexpected_item_msg)

            if outcome in Outcome.failfast and config.fail_fast:
                break

        for logger in self.result_loggers:
            logger.end_testing()

    def run(self):
        '''
        Run our entire collection of suites.
        '''
        for logger in self.result_loggers:
            logger.begin_testing()

        log.info(separator())
        log.info("Building all non 'lazy_init' fixtures")

        failed_builds = self.setup_unbuilt(
                self.suites.iter_fixtures(),
                setup_lazy_init=False)

        if failed_builds:
            error_str = ''
            for fixture, error in failed_builds:
                error_str += 'Failed to build %s\n' % fixture
                error_str += '%s' % error
            log.warn('Error(s) while building non lazy_init fixtures.')
            log.warn(error_str)

        outcomes = set()
        for suite in self.suites:
            outcome = self.run_suite(suite)
            outcomes.add(outcome)
            if outcome in Outcome.failfast and config.fail_fast:
                break

        for logger in self.result_loggers:
            logger.end_testing()
        return self._suite_outcome(outcomes)


    def run_suite(self, test_suite):
        '''
        Run all tests/suites. From the given test_suite.

        1. Run child testcases passing them their required fixtures.
           - (We don't setup since the test case might override the fixture)
           - Collect results as tests are performed.
        2. Handle teardown for all fixtures in the test_suite.
        '''
        for logger in self.result_loggers:
            logger.begin(test_suite)

        suite_iterator = enumerate(test_suite.iter_testlists())

        outcomes = set()

        suite_timer = _util.Timer()
        suite_timer.start()
        for (idx, (testlist, testcase)) in suite_iterator:
            assert isinstance(testcase, TestCase)
            outcome = self.run_test(testcase, fixtures=test_suite.fixtures)
            outcomes.add(outcome)

            # If there was a chance we might need to skip the remaining
            # tests...
            if outcome in Outcome.failfast \
                    and idx < len(test_suite):
                if config.fail_fast:
                    log.bold('Test failed with the --fail-fast flag provided.')
                    log.bold('Ignoring remaining tests.')
                    break
                elif test_suite.fail_fast:
                    log.bold('Test failed in a fail_fast TestSuite. Skipping'
                             ' remaining tests.')
                    rem_iter = (testcase for _, (_, testcase) \
                                in suite_iterator)
                    self._generate_skips(testcase.name, rem_iter)
                elif testlist.fail_fast:
                    log.bold('Test failed in a fail_fast TestList. Skipping'
                             ' its remaining items.')
                    rem_iter = self._remaining_testlist_tests(testcase,
                                                              testlist,
                                                              suite_iterator)
                    # Iterate through the current testlist skipping its tests.
                    self._generate_skips(testcase.name, rem_iter)

        for fixture in test_suite.fixtures.values():
            fixture.teardown()
        suite_timer.stop()

        outcome = self._suite_outcome(outcomes)
        self._log_outcome(outcome, runtime=suite_timer.runtime())
        for logger in self.result_loggers:
            logger.end_current()

        return outcome

    def _suite_outcome(self, outcomes):
        '''
        A test suite can have the following results, they occur with the
        following priority/ordering.

        ERROR - Indicates that some error happened outside of a test case,
        likely in fixture setup.

        FAIL - Indicates that one or more tests failed.

        SKIP - Indicates that all contained tests and test suites were
        skipped.

        PASS - Indicates that all tests passed or EXFAIL'd
        '''
        if Outcome.ERROR in outcomes:
            return Outcome.ERROR
        elif Outcome.FAIL in outcomes:
            return Outcome.FAIL
        elif len(outcomes - {Outcome.PASS}):
            return Outcome.SKIP
        else:
            return Outcome.PASS

    def run_test(self, testobj, fixtures=None):
        '''
        Run the given test.

        This performs the bulkwork of the testing framework.

        1. Handle setup for all fixtures required for the specific test.

        2. Run the test.

        3. Teardown the fixtures for the test which are tied locally to the\
            test.
        '''
        outdir = test_results_output_path(testobj)
        mkdir_p(outdir)
        fstdout_name = joinpath(outdir, config.constants.system_err_name)
        fstderr_name = joinpath(outdir, config.constants.system_out_name)

        # Capture the output into a file.
        with tee(fstderr_name, stderr=True, stdout=False),\
                tee(fstdout_name, stderr=False, stdout=True):
            return self._run_test(testobj, fstdout_name,
                                  fstderr_name, fixtures)

    def _run_test(self, testobj, fstdout_name, fstderr_name, fixtures):
        if fixtures is None:
            fixtures = {}

        # We'll use a local shallow copy of fixtures to make it easier to
        # cleanup and override suite level fixtures with testcase level ones.
        fixtures = fixtures.copy()
        fixtures.update(testobj.fixtures)

        test_timer = _util.Timer()
        test_timer.start()

        for logger in self.result_loggers:
            logger.begin(testobj)

        def _run_test():
            reason = None
            try:
                testobj(fixtures=fixtures)
            except AssertionError as e:
                reason = e.message
                if not reason:
                    reason = traceback.format_exc()
                outcome = Outcome.FAIL
            except test.TestSkipException as e:
                reason = e.message
                outcome = Outcome.SKIP
            except test.TestFailException as e:
                reason = e.message
                outcome = Outcome.FAIL
            except Exception as e:
                reason = traceback.format_exc()
                outcome = Outcome.FAIL
            else:
                outcome = Outcome.PASS

            return (outcome, reason)

        # Build any fixtures that haven't been built yet.
        log.debug('Building fixtures for TestCase: %s' % testobj.name)
        failed_builds = self.setup_unbuilt(
                fixtures.values(),
                setup_lazy_init=True)

        if failed_builds:
            reason = ''
            for fixture, error in failed_builds:
                reason += 'Failed to build %s\n' % fixture
                reason += '%s' % error
            reason = reason
            outcome = Outcome.ERROR
        else:
            (outcome, reason) = _run_test()

        for fixture in testobj.fixtures.values():
            fixture.teardown()

        test_timer.stop()
        self._log_outcome(
                outcome,
                reason=reason,
                runtime=test_timer.runtime(),
                fstdout_name=fstdout_name,
                fstderr_name=fstderr_name)

        for logger in self.result_loggers:
            logger.end_current()

        return outcome

    def _remaining_testlist_tests(self, current_item, testlist,
                                  suite_iterator):
        '''
        Return an iterator which will advance the suite_iterator while
        returning just the remaining tests (after the current_item) in the
        testlist.

        :param current_item: The current test being iterated over.
        :param testlist: The current :class:`TestList` being iterated over.

        :param suite_iterator: The current iterator being used to iterate
        through :class:`TestCase` and :class:`TestList` objects.
        '''
        testlist_iter = testlist.iter_testlists()
        next_item = next(testlist_iter)

        try:
            while next_item != current_item:
                next_item = next(testlist)
        except StopIteration:
            next_item = None

        if next_item is not None:
            # place the next_item back into the iterator.
            testlist_remaining = itertools.chain((next_item,), testlist_iter)

        # Create an iterator that will iterator throut testlist and
        # suite_iterator in lock-step.
        combined_iterator = itertools.izip(testlist, suite_iterator)
        return (testcase for testcase, _ in combined_iterator)


    def _generate_skips(self, failed_test, remaining_iterator):
        '''
        Generate SKIP for all remaining tests (for use with the failfast
        suite option)
        '''
        for testcase in remaining_iterator:
            if isinstance(testcase, TestCase):
                reason = ("Previous test '%s' failed in a failfast"
                        " TestSuite." % failed_test)
                for logger in self.result_loggers:
                    logger.skip(testcase, reason=reason)
            elif __debug__:
                raise AssertionError(_util.unexpected_item_msg)

    def _log_outcome(self, outcome, **kwargs):
        for logger in self.result_loggers:
            logger.set_current_outcome(outcome, **kwargs)

    def setup_unbuilt(self, fixtures, setup_lazy_init=False):
        failures = []
        for fixture in fixtures:
            if not fixture.built:
                if fixture.lazy_init == setup_lazy_init:
                    try:
                        fixture.setup()
                    except Exception as e:
                        failures.append((fixture.name,
                                         traceback.format_exc()))
        return failures
