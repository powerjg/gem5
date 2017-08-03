'''
Module which contains streaming result loggers. The main goal of these loggers
is to support large amounts of results and not create large standing pools of
strings.
'''
import abc
import pickle
from xml.sax.saxutils import escape as xml_escape
from string import maketrans

import terminal
from config import config
from helper import joinpath
from test import TestCase
from suite import TestSuite
from logger import log
from _util import Timer, Enum

class InvalidResultException(Exception):
    pass

Outcome = Enum(
    [
    'PASS',   # The test passed successfully.
    'XFAIL',  # The test ran and failed as expected.
    'SKIP',   # The test was skipped.
    'ERROR',  # There was an error during the setup of the test.
    'FAIL',   # The test failed to pass.
    ],
)
Outcome.__doc__ = \
    '''
    An :class:`Enum` which contains the different possible test outcomes.

    PASS    - The test passed successfully.
    XFAIL   - The test ran and failed as expected.
    SKIP    - The test was skipped.
    ERROR   - There was an error during the setup of the test.
    FAIL    - The test failed to pass.
    '''

# Add all result enums to this module's namespace.
for result in Outcome.enums:
    globals()[str(result)] = result

# Hard fail cases which mean a test contained in a failfast container should
# fail remaining test items.
Outcome.failfast = {ERROR, FAIL}

def test_results_output_path(test_case):
    '''
    Return the path which results for a specific test case should be
    stored.
    '''
    return joinpath(config.result_path, test_case.uid.replace('/','-'))


# TODO: I'd like to re-factor this interface into an explicit callback
# interface offered by the the Runner class. Right now the Runner needs to
# know specifics about this interface. Instead the Runner should offer
# generic enough hooks and loggers could do with them as they please.
class ResultLogger(object):
    '''
    Interface which allows writing of streaming results to a file stream.
    '''
    __metaclass__ = abc.ABCMeta

    @abc.abstractmethod
    def begin_testing(self):
        '''
        Signal the beginning of writing to the file stream. Indicates that
        results are about to be logged.
        '''
        pass

    @abc.abstractmethod
    def begin(self, item):
        '''Signal the beginning of the given item.'''
        pass

    @abc.abstractmethod
    def skip(self, item, **kwargs):
        # FIXME I reaally don't like this specific method being forced to be
        # known in the Runner. There should be a fail_fast callback offered
        # there instead whenever the refactor is done.
        '''
        Signal we are forcefully skipping the item due to some circumstance.
        '''

    @abc.abstractmethod
    def set_current_outcome(self, outcome, **kwargs):
        '''Set the outcome of the current item.'''

    @abc.abstractmethod
    def end_current(self):
        '''
        Signal the end of the current item.
        '''
        pass

    @abc.abstractmethod
    def end_testing(self):
        '''
        Signal the end of writing to the file stream. Indicates that
        results are done being logged.
        '''
        pass


class ConsoleLogger(ResultLogger):
    '''
    A logger implementing the streaming ResultLogger interface. This logger is
    used to stream testing result output to a user terminal.
    '''
    color = terminal.get_termcap()
    reset = color.Normal
    colormap = {
            FAIL: color.Red,
            ERROR: color.Red,
            PASS: color.Green,
            XFAIL: color.Cyan,
            SKIP: color.Cyan,
            }
    sep_fmtkey = 'separator'
    sep_fmtstr = '{%s}' % sep_fmtkey

    bad_item = ('Result formatter can only handle test cases'
            ' and test suites')

    def __init__(self):
        self.outcome_count = {outcome: 0 for outcome in Outcome.enums}
        self._item_list = []
        self._current_item = None
        self.timer = Timer()

        self._started = False

    def begin_testing(self):
        self.timer.start()
        self._started = True

    def begin(self, item):
        if isinstance(item, TestSuite):
            self._begin_testsuite(item)
        elif isinstance(item, TestCase):
            self._begin_testcase(item)
        elif __debug__:
            raise AssertionError(self.bad_item)
        self._item_list.append(self._current_item)
        self._current_item = item

    def _begin_testsuite(self, test_suite):
        log.info('Starting TestSuite: %s' % test_suite.name)
    def _begin_testcase(self, test_case):
        log.info('Starting TestCase: %s' % test_case.name)

    def set_current_outcome(self, outcome, **kwargs):
        '''Set the outcome of the current item.'''
        if isinstance(self._current_item, TestSuite):
            pass # TODO, for now we dont' do anything with this.
        elif isinstance(self._current_item, TestCase):
            self._set_testcase_outcome(self._current_item, outcome, **kwargs)
        elif __debug__:
            raise AssertionError(self.bad_item)

    def _set_testcase_outcome(self, test_case, outcome, reason=None, **kwargs):
        log.bold(
                self.colormap[outcome]
                + test_case.name
                + self.reset)
        self.outcome_count[outcome] += 1

        if reason is not None:
            log.info('')
            log.info('Reason:')
            log.info(reason)
            log.info(terminal.separator('-'))

    def _set_testsuite_outcome(self, test_suite, outcome, **kwargs):
        pass # Do nothing for test suites.

    def skip(self, item, reason):
        '''Set the outcome of the current item.'''
        if isinstance(item, TestSuite):
            pass # TODO, for now we dont' do anything with this.
        elif isinstance(item, TestCase):
            self._skip_testcase(item, reason)
        elif __debug__:
            raise AssertionError(self.bad_item)

    def _skip_testcase(self, test_case, reason):
        log.display('{color}Skipping: {name}{reset}'.format(
            color=self.colormap[Outcome.SKIP],
            name=test_case.name,
            reset=self.reset))
        self.outcome_count[Outcome.SKIP] += 1

    def end_current(self):
        if isinstance(self._current_item, TestSuite):
            self._end_testsuite(self._current_item)
        elif isinstance(self._current_item, TestCase):
            self._end_testcase(self._current_item)
        elif __debug__:
            raise AssertionError(self.bad_item)
        self._current_item = self._item_list.pop()

    def _end_testcase(self, test_case):
        pass
    def _end_testsuite(self, test_suite):
        pass

    def end_testing(self):
        if self._started:
            self.timer.stop()
            log.display(self._display_summary())
            self._started = False

    def _display_summary(self):
        most_severe_outcome = None
        outcome_fmt = ' {count} {outcome}'
        strings = []

        # Iterate over enums so they are in order of severity
        for outcome in Outcome.enums:
            count  = self.outcome_count[outcome]
            if count:
                strings.append(outcome_fmt.format(count=count,
                                                  outcome=outcome.name))
                most_severe_outcome = outcome
        string = ','.join(strings)
        if most_severe_outcome is None:
            string = ' No testing done'
            most_severe_outcome = Outcome.PASS
        string += ' in {time:.2} seconds '.format(time=self.timer.runtime())

        return terminal.insert_separator(
                string,
                color=self.colormap[most_severe_outcome] + self.color.Bold)


class TestResult(object):
    def __init__(self, testitem, outcome, runtime, **kwargs):
        self.name = testitem.name
        self.uid = testitem.uid
        self.outcome = outcome
        self.runtime = runtime


class TestCaseResult(TestResult):
    def __init__(self, testitem, outcome, runtime, fstdout_name,
                 fstderr_name, reason=None, **kwargs):

        self.fstdout_name = fstdout_name
        self.fstderr_name = fstderr_name
        self.reason = reason
        super(TestCaseResult, self).__init__(testitem, outcome,
                                             runtime,
                                             **kwargs)


class TestSuiteResult(TestResult):
    def __init__(self, testitem, outcome,
                 runtime, test_case_results,
                 **kwargs):

        super(TestSuiteResult, self).__init__(testitem, outcome, runtime)
        self.test_case_results = test_case_results


class InternalLogger(ResultLogger):
    '''
    An internal logger which writes streaming pickle items on completion of
    TestSuite items.

    This logger also offers some metadata methods to and can load back out
    previous results.

    .. seealso:: :func:`load` :func:`suites`
    '''
    def __init__(self, filestream):
        self._item_list = []
        self._current_item = None
        self.timer = Timer()
        self.filestream = filestream
        self.results = []

        self._current_suite_testcases = []

    def _write(self, obj):
        pickle.dump(obj, self.filestream)

    def begin_testing(self):
        self.timer.start()

    def begin(self, item):
        self._item_list.append(self._current_item)
        self._current_item = item

    def skip(self, item, **kwargs):
        if isinstance(self._current_item, TestSuite):
            result = TestSuiteResult(self._current_item, Outcome.SKIP, 0,
                                     self._current_suite_testcases, **kwargs)
            self._current_suite_testcases = []

        elif isinstance(self._current_item, TestCase):
            result = TestCaseResult(self._current_item, Outcome.SKIP, 0,
                                    **kwargs)
            self._current_suite_testcases.append(result)

        elif __debug__:
            raise AssertionError(self.bad_item)
        self._write(result)
        self.results.append(result)

    def set_current_outcome(self, outcome, runtime, **kwargs):
        '''Set the outcome of the current item.'''
        if isinstance(self._current_item, TestSuite):
            result = TestSuiteResult(self._current_item, outcome, runtime,
                                     self._current_suite_testcases)
            self._current_suite_testcases = []

        elif isinstance(self._current_item, TestCase):
            result = TestCaseResult(self._current_item, outcome,
                                    runtime, **kwargs)
            self._current_suite_testcases.append(result)

        elif __debug__:
            raise AssertionError(self.bad_item)

        self._write(result)
        self.results.append(result)

    def end_current(self):
        self._current_item = self._item_list.pop()

    def end_testing(self):
        self.timer.stop()

    @staticmethod
    def load(filestream):
        '''Load results out of a dumped file replacing our own results.'''
        loaded_results = []
        try:
            while True:
                item = pickle.load(filestream)
                loaded_results.append(item)
        except EOFError:
            pass

        new_logger = InternalLogger(filestream)
        new_logger.results = loaded_results
        return new_logger

    @property
    def suites(self):
        '''
        Return an iterator over all the test suite results loaded or collected.
        '''
        for result in self.results:
            if isinstance(result, TestSuiteResult):
                yield result


class JUnitLogger(InternalLogger):
    '''
    Logger which uses the internal logger to collect streaming results to the
    internal_fstream, and on completion of testing writes the results out to
    a junit_fstream.

    :param junit_fstream: File stream to write junit formatted results to.

    :param internal_fstream: File stream to write internal formatted results
        to.

    .. seealso:: :class:`~InternalLogger`
    '''
    # We use an internal logger to stream the output into a format we can
    # retrieve at the end and then format it into JUnit.
    def __init__(self, junit_fstream, internal_fstream):
        super(JUnitLogger, self).__init__(internal_fstream)
        self._junit_fstream = junit_fstream

    def end_testing(self):
        '''
        Signal the end of writing to the file stream. We will write all our
        results to our junit_fstream.
        '''
        super(JUnitLogger, self).end_testing()
        JUnitFormatter(self).dump(self._junit_fstream)


class JUnitFormatter(object):
    '''
    Formats TestResults into the JUnit XML format.
    '''
    # NOTE: We manually build the tags so we can place possibly large
    # system-out system-err logs without storing them in memory.

    xml_header = '<?xml version="1.0" encoding="UTF-8"?>\n'
    passing_results = {PASS, XFAIL}
    # Testcase stuff
    testcase_opening = ('<testcase name="{name}" classname="{classname}"'
                        ' status="{status}" time="{time}">\n')
    # Indicates test skipped
    skipped_tag = '<skipped/>'
    error_tag = '<error message="{message}"></error>\n'
    fail_tag = '<failure message="{message}"></error>\n'
    system_out_opening = '<system-out>'
    system_err_opening = '<system-err>'

    # Testsuite stuff
    testsuite_opening = ('<testsuite name="{name}" tests="{numtests}"'
                         ' errors="{errors}" failures="{failures}"'
                         ' skipped="{skipped}" id={suitenum}'
                         ' time="{time}">\n'
                         )
    # Testsuites stuff
    testsuites_opening = ('<testsuites errors="{errors}" failures="{failures}"'
                          ' tests="{tests}"' # total number of sucessful tests.
                          ' time="{time}">\n')
    # Generic closing tag for any opening tag.
    generic_closing = '</{tag}>\n'


    def __init__(self, internal_results, translate_names=True):
        self.results = internal_results.results
        self.runtime = internal_results.timer.runtime()

        if translate_names:
            self.name_table = maketrans('/.', '.-')
        else:
            self.name_table = maketrans('', '')

    def dump_testcase(self, fstream, testcase):

        tag = ''
        if testcase.outcome in self.passing_results:
            outcome = PASS
            status = 'passed'
        elif testcase.outcome == SKIP:
            outcome = SKIP
            status = 'skipped'
            tag = self.skipped_tag
        elif testcase.outcome == FAIL:
            outcome = SKIP
            status = 'failed'
            tag = self.fail_tag.format(message=testcase.reason)
        elif testcase.outcome == ERROR:
            outcome = SKIP
            status = 'errored'
            tag = self.error_tag.format(message=testcase.reason)
        elif __debug__:
            raise AssertionError('Unknown test state')

        fstream.write(self.testcase_opening.format(
                name=testcase.name,
                classname=testcase.name,
                time=testcase.runtime,
                status=status))

        fstream.write(tag)

        # Write out systemout and systemerr from their containing files.
        fstream.write(self.system_out_opening)
        with open(testcase.fstdout_name, 'r') as testout_stdout:
            for line in testout_stdout:
                fstream.write(xml_escape(line))
        fstream.write(self.generic_closing.format(tag='system-out'))

        fstream.write(self.system_err_opening)
        with open(testcase.fstderr_name, 'r') as testout_stderr:
            for line in testout_stderr:
                fstream.write(xml_escape(line))
        fstream.write(self.generic_closing.format(tag='system-err'))

        fstream.write(self.generic_closing.format(tag='testcase'))

    def dump_testsuite(self, fstream, suite, idx):
        # Tally results first.
        outcome_tally = dict.fromkeys((PASS, SKIP, FAIL, ERROR), 0)
        for testcase in suite.test_case_results:
            if testcase.outcome in self.passing_results:
                outcome_tally[PASS] += 1
            else:
                outcome_tally[testcase.outcome] += 1

        fstream.write(
                self.testsuite_opening.format(
                    name=suite.name,
                    numtests=outcome_tally[PASS],
                    errors=outcome_tally[ERROR],
                    failures=outcome_tally[FAIL],
                    skipped=outcome_tally[SKIP],
                    suitenum=idx,
                    time=suite.runtime))

        for testcase in suite.test_case_results:
            self.dump_testcase(fstream, testcase)

        fstream.write(self.generic_closing.format(tag='testsuite'))

    def dump(self, dumpfile):
        idx = 0

        # First tally results.
        outcome_tally = dict.fromkeys((PASS, SKIP, FAIL, ERROR), 0)
        for item in self.results:
            if isinstance(item, TestCaseResult):
                if item.outcome in self.passing_results:
                    outcome_tally[PASS] += 1
                else:
                    outcome_tally[item.outcome] += 1

        dumpfile.write(self.testsuites_opening.format(
            tests=outcome_tally[PASS],
            errors=outcome_tally[ERROR],
            failures=outcome_tally[FAIL],
            time=self.runtime))

        for item in self.results:
            # NOTE: We assume that all tests are contained within a testsuite,
            # although as far as junit is concerned this isn't neccesary.
            if isinstance(item, TestSuiteResult):
                self.dump_testsuite(dumpfile, item, idx)
                idx += 1

        dumpfile.write(self.generic_closing.format(tag='testsuites'))
