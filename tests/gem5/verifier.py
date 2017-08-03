'''
Built in test cases that verify particular details about a gem5 run.
'''
import re

from testlib import test
from testlib.config import constants
from testlib._util import diff_out_file
from testlib.helper import joinpath

class Verifier(test.TestFunction):
    def __init__(self, name=None, **kwargs):
        name = name if name is not None else self.__class__.__name__
        super(Verifier, self).__init__(self.test, name, **kwargs)


class MatchGoldStandard(Verifier):
    '''
    Compares a standard output to the test output and passes if they match,
    fails if they do not.
    '''
    def __init__(self, standard_filename, name=None, ignore_regex=None,
                 test_filename='simout'):
        '''
        :param standard_filename: The path of the standard file to compare
        output to.

        :param ignore_regex: A string, compiled regex, or iterable containing
        either which will be ignored in 'standard' and test output files when
        diffing.
        '''
        super(MatchGoldStandard, self).__init__(name)
        self.standard_filename = standard_filename
        self.test_filename = test_filename

        self.ignore_regex = _iterable_regex(ignore_regex)

    def test(self, fixtures):
        # We need a tempdir fixture from our parent verifier suite.

        # Get the file from the tempdir of the test.
        tempdir = fixtures[constants.tempdir_fixture_name].path
        self.test_filename = joinpath(tempdir, self.test_filename)

        diff = diff_out_file(self.standard_filename,
                                   self.test_filename,
                                   self.ignore_regex)
        if diff is not None:
            test.fail('Stdout did not match:\n%s' % diff)

    def _generic_instance_warning(self, kwargs):
        '''
        Method for helper classes to tell users to use this more generic class
        if they are going to manually override the test_filename param.
        '''
        if 'test_filename' in kwargs:
            raise ValueError('If you are setting test_filename use the more'
                             ' generic %s'
                             ' instead' % MatchGoldStandard.__name__)

class DerivedGoldStandard(MatchGoldStandard):
    __ignore_regex_sentinel = object()
    _file = None
    _default_ignore_regex = []

    def __init__(self, standard_filename,
                 ignore_regex=__ignore_regex_sentinel, **kwargs):

        if ignore_regex == self.__ignore_regex_sentinel:
            ignore_regex = self._default_ignore_regex

        self._generic_instance_warning(kwargs)

        super(DerivedGoldStandard, self).__init__(
            standard_filename,
            test_filename=self._file,
            ignore_regex=ignore_regex,
            **kwargs)

class MatchStdout(DerivedGoldStandard):
    _file = constants.gem5_simulation_stdout
    _default_ignore_regex = [
            re.compile('^Redirecting (stdout|stderr) to'),
            re.compile('^gem5 compiled '),
            re.compile('^gem5 started '),
            re.compile('^gem5 executing on '),
            re.compile('^command line:'),
            re.compile("^Couldn't import dot_parser,"),
            re.compile("^info: kernel located at:"),
            re.compile("^Couldn't unlink "),
            re.compile("^Using GPU kernel code file\(s\) "),
        ]

class MatchStderr(DerivedGoldStandard):
    _file = constants.gem5_simulation_stderr
    _default_ignore_regex = []

class MatchStats(DerivedGoldStandard):
    # TODO: Likely will want to change this verifier since we have the weird
    # perl script right now. A simple diff probably isn't going to work.
    _file = constants.gem5_simulation_stats
    _default_ignore_regex = []

class MatchConfigINI(DerivedGoldStandard):
    _file = constants.gem5_simulation_config_ini
    _default_ignore_regex = (
            re.compile("^(executable|readfile|kernel|image_file)="),
            re.compile("^(cwd|input|codefile)="),
            )

class MatchConfigJSON(DerivedGoldStandard):
    _file = constants.gem5_simulation_config_json
    _default_ignore_regex = (
            re.compile(r'''^\s*"(executable|readfile|kernel|image_file)":'''),
            re.compile(r'''^\s*"(cwd|input|codefile)":'''),
            )

class MatchRegex(Verifier):
    def __init__(self, regex, name=None, match_stderr=True, match_stdout=True):
        super(MatchRegex, self).__init__(name)
        self.regex = _iterable_regex(regex)
        self.match_stderr = match_stderr
        self.match_stdout = match_stdout

    def test(self, fixtures):
        # Get the file from the tempdir of the test.
        tempdir = fixtures[constants.tempdir_fixture_name].path

        def parse_file(fname):
            with open(fname, 'r') as file_:
                for line in file_:
                    for regex in self.regex:
                        if re.match(regex, line):
                            return True
        if self.match_stdout:
            if parse_file(joinpath(tempdir,
                                   constants.gem5_simulation_stdout)):
                return # Success
        if self.match_stderr:
            if parse_file(joinpath(tempdir,
                                   constants.gem5_simulation_stderr)):
                return # Success
        test.fail('Could not match regex.')


class VerifyReturncode(Verifier):
    def __init__(self, returncode, name=None):
        self.expected_returncode = returncode
        super(VerifyReturncode, self).__init__(name)

    def test(self, fixtures):
        test.assertEquals(self.expected_returncode,
                fixtures[constants.gem5_returncode_fixture_name].value)
        pass

_re_type = type(re.compile(''))
def _iterable_regex(regex):
    if isinstance(regex, _re_type) or isinstance(regex, str):
        regex = (regex,)
    return regex
