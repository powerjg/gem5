'''
Global configuration module which exposes two types of configuration
variables:

1. config
2. constants (Also attached to the config variable as an attribute)

The main motivation for this module is to have a centralized location for
defaults and configuration by command line and files for the test framework.

A secondary goal is to reduce programming errors by providing common constant
strings and values as python attributes to simplify detection of typos.
A simple typo in a string can take a lot of debugging to uncover the issue,
attribute errors are easier to notice and most autocompletion systems detect
them.

The config variable is initialzed by callling :func:`initialize_config`.
Before this point only ``constants`` will be availaible. This is to ensure
that library function writers never accidentally get stale config attributes.

Program arguments/flag arguments are available from the config as attributes.
If a attribute was not set by the command line or the optional config file,
then it will fallback to the `_defaults` value, if still the value is not
found an AttributeError will be raised.

:func define_defaults:
    Provided by the config if the attribute is not found in the config or
    commandline. For instance, if we are using the list command fixtures might
    not be able to count on the build_dir being provided since we aren't going
    to build anything.

:var constants:
    Values not directly exposed by the config, but are attached to the object
    for centralized access. I.E. you can reach them with
    :code:`config.constants.attribute`. These should be used for setting
    common string names used across the test framework.
    :code:`_defaults.build_dir = None` Once this module has been imported
    constants should not be modified and their base attributes are frozen.
'''
import abc
import argparse
import copy
import os
from pickle import HIGHEST_PROTOCOL as highest_pickle_protocol

from helper import absdirpath
from _util import AttrDict, FrozenAttrDict

class UninitialzedAttributeException(Exception):
    '''
    Signals that an attribute in the config file was not initialized.
    '''
    pass

class UninitializedConfigException(Exception):
    '''
    Signals that the config was not initialized before trying to access an
    attribute.
    '''
    pass

class _Config(object):
    _initialized = False

    __shared_dict = {}

    constants = AttrDict()
    _defaults = AttrDict()
    _config = {}

    _cli_args = {}
    _post_processors = {}

    def __init__(self):
        # This object will act as if it were a singleton.
        self.__dict__ = self.__shared_dict

    def _init(self, parser):
        self._parse_commandline_args(parser)
        self._run_post_processors()
        self._initialized = True

    def _add_post_processor(self, attr, post_processor):
        '''
        :param attr: Attribute to pass to and recieve from the
        :func:`post_processor`.

        :param post_processor: A callback functions called in a chain to
            perform additional setup for a config argument. Should return a tuple
            containing the new value for the config attr.
        '''
        if attr not in self._post_processors:
            self._post_processors[attr] = []
        self._post_processors[attr].append(post_processor)

    def _set(self, name, value):
        self._config[name] = value

    def _parse_commandline_args(self, parser):
        args = parser.parse_args()

        self._config_file_args = {}

        for attr in dir(args):
            # Ignore non-argument attributes.
            if not attr.startswith('_'):
                self._config_file_args[attr] = getattr(args, attr)
        self._config.update(self._config_file_args)

    def _run_post_processors(self):
        for attr, callbacks in self._post_processors.items():
            newval = self._lookup_val(attr)
            for callback in callbacks:
                newval = callback(newval)
            if newval is not None:
                newval = newval[0]
            self._set(attr, newval)


    def _lookup_val(self, attr):
        '''
        Get the attribute from the config or fallback to defaults.

        :returns: If the value is not stored return None. Otherwise a tuple
            containing the value.
        '''
        if attr in self._config:
            return (self._config[attr],)
        elif hasattr(self._defaults, attr):
            return (getattr(self._defaults, attr),)

    def __getattr__(self, attr):
        if attr in dir(super(_Config, self)):
            return getattr(super(_Config, self), attr)
        elif not self._initialized:
            raise UninitializedConfigException(
                'Cannot directly access elements from the config before it is'
                ' initialized')
        else:
            val = self._lookup_val(attr)
            if val is not None:
                return val[0]
            else:
                raise UninitialzedAttributeException(
                    '%s was not initialzed in the config.' % attr)


def define_defaults(defaults):
    '''
    Defaults are provided by the config if the attribute is not found in the
    config or commandline. For instance, if we are using the list command
    fixtures might not be able to count on the build_dir being provided since we
    aren't going to build anything.
    '''
    defaults.base_dir = os.path.abspath(os.path.join(absdirpath(__file__),
                                                      os.pardir,
                                                      os.pardir))
    defaults.result_path = os.path.join(os.getcwd(), '.testing-results')
    defaults.list_only_failed = False

def define_constants(constants):
    '''
    'constants' are values not directly exposed by the config, but are attached
    to the object for centralized access. These should be used for setting
    common string names used across the test framework. A simple typo in
    a string can take a lot of debugging to uncover the issue, attribute errors
    are easier to notice and most autocompletion systems detect them.
    '''
    constants.system_out_name = 'system-out'
    constants.system_err_name = 'system-err'
    constants.x86_tag = 'X86'
    constants.sparc_tag = 'SPARC'
    constants.alpha_tag = 'ALPHA'
    constants.riscv_tag = 'RISCV'
    constants.arm_tag = 'ARM'
    constants.supported_isas = (
            constants.x86_tag,
            constants.sparc_tag,
            constants.alpha_tag,
            constants.riscv_tag,
            constants.arm_tag
    )
    constants.opt_tag = 'opt'
    constants.debug_tag = 'debug'
    constants.fast_tag = 'fast'
    constants.supported_optimizations = (
            constants.opt_tag,
            constants.debug_tag,
            constants.fast_tag
    )

    constants.tempdir_fixture_name = 'tempdir'
    constants.gem5_simulation_stderr = 'simerr'
    constants.gem5_simulation_stdout = 'simout'
    constants.gem5_simulation_stats = 'stats.txt'
    constants.gem5_simulation_config_ini = 'config.ini'
    constants.gem5_simulation_config_json = 'config.json'
    constants.gem5_returncode_fixture_name = 'gem5-returncode'
    constants.gem5_binary_fixture_name = 'gem5'
    constants.pickle_protocol = highest_pickle_protocol

def define_post_processors(config):
    '''
    post_processors are used to do final configuration of variables. This is
    useful if there is a dynamically set default, or some function that needs
    to be applied after parsing in order to set a configration value.

    Post processors must accept a single argument that will either be a tuple
    containing the already set config value or ``None`` if the config value
    has not been set to anything. They must return the modified value in the
    same format.
    '''

    def set_default_build_dir(build_dir):
        '''
        Post-processor to set the default build_dir based on the base_dir.

        .. seealso :func:`~_Config._add_post_processor`
        '''
        if not build_dir or build_dir[0] is None:
            base_dir = config._lookup_val('base_dir')[0]
            build_dir = (os.path.join(base_dir, 'build'),)
        return build_dir

    def fix_verbosity_hack(verbose):
        verbose = (verbose[0].val,)
        return verbose

    config._add_post_processor('build_dir', set_default_build_dir)
    config._add_post_processor('verbose', fix_verbosity_hack)

class Argument(object):
    '''
    Class represents a cli argument/flag for a argparse parser.

    :attr name: The long name of this object that will be stored in the arg
        output by the final parser.
    '''
    def __init__(self, *flags, **kwargs):
        self.flags = flags
        self.kwargs = kwargs

        if len(flags) == 0:
            raise ValueError("Need at least one argument.")
        elif 'dest' in kwargs:
            self.name = kwargs['dest']
        elif len(flags) > 1 or flags[0].startswith('-'):
            for flag in flags:
                if not flag.startswith('-'):
                    raise ValueError("invalid option string %s: must start"
                    "with a character '-'" % flag)

                if flag.startswith('--'):
                    if not hasattr(self, 'name'):
                        self.name = flag.lstrip('-')

        if not hasattr(self, 'name'):
            self.name = flags[0].lstrip('-')
        self.name = self.name.replace('-', '_')

    def add_to(self, parser):
        '''Add this argument to the given parser.'''
        parser.add_argument(*self.flags, **self.kwargs)

    def copy(self):
        '''Copy this argument so you might modify any of its kwargs.'''
        return copy.deepcopy(self)


class _StickyInt:
    '''
    A class that is used to cheat the verbosity count incrementer by
    pretending to be an int. This makes the int stay on the heap and eat other
    real numbers when they are added to it.

    We use this so we can allow the verbose flag to be provided before or after
    the subcommand. This likely has no utility outside of this use case.
    '''
    def __init__(self, val=0):
        self.val = val
        self.type = int
    def __add__(self, other):
        self.val += other
        return self

common_args = NotImplemented

def define_common_args(config):
    '''
    Common args are arguments which are likely to be simular between different
    subcommands, so they are available to all by placing their definitions
    here.
    '''
    global common_args

    # A list of common arguments/flags used across cli parsers.
    common_args = [
        Argument(
            'directory',
            nargs='?',
            default=os.getcwd(),
            help='Directory to start searching for tests in'),
        Argument(
            '--fail-fast',
            action='store_true',
            help='Stop running on the first instance of failure'),
        Argument(
            '--tags',
            action='append',
            default=[],
            help=None),
        Argument(
            '--uid',
            action='store',
            default=None,
            help='UID of a specific test item to run.'),
        Argument(
            '--build-dir',
            action='store',
            help='Build directory for SCons'),
        Argument(
            '--base-dir',
            action='store',
            default=config._defaults.base_dir,
            help='Directory to change to in order to exec scons.'),
        Argument(
            '-j', '--threads',
            action='store',
            default=1,
            help='Number of threads to run SCons with.'),
        Argument(
            '-v',
            action='count',
            dest='verbose',
            default=_StickyInt(),
            help='Increase verbosity'),
        Argument(
            '--config-path',
            action='store',
            default=os.getcwd(),
            help='Path to read a testing.ini config in'
        ),
        Argument(
            '--skip-build',
            action='store_true',
            default=False,
            help='Skip the building component of SCons targets.'
        ),
        Argument(
            '--result-path',
            action='store',
            help='The path to store results in.'
        ),
        Argument(
            '--list-only-failed',
            action='store_true',
            default=False,
            help='Only list tests that failed.'
        )
    ]

    # NOTE: There is a limitation which arises due to this format. If you have
    # multiple arguments with the same name only the final one in the list will be
    # saved.
    #
    # e.g. if you have a -v argument which increments verbosity level and
    # a separate --verbose flag which 'store's verbosity level. the final one in
    # the list will be saved.
    common_args = AttrDict({arg.name:arg for arg in common_args})


class ArgParser(object):
    __metaclass__ = abc.ABCMeta

    def __init__(self, parser):
        # Copy public methods of the parser.
        for attr in dir(parser):
            if not attr.startswith('_'):
                setattr(self, attr, getattr(parser, attr))
        self.parser = parser
        self.add_argument = self.parser.add_argument

        # Argument will be added to all parsers and subparsers.
        common_args.verbose.add_to(parser)


class CommandParser(ArgParser):
    '''
    Main parser which parses command strings and uses those to direct to
    a subparser.
    '''
    def __init__(self):
        parser = argparse.ArgumentParser()
        super(CommandParser, self).__init__(parser)
        self.subparser = self.add_subparsers(dest='command')


class RunParser(ArgParser):
    '''
    Parser for the \'run\' command.
    '''
    def __init__(self, subparser):
        parser = subparser.add_parser(
            'run',
            help='''Run Tests.'''
        )

        super(RunParser, self).__init__(parser)

        common_args.uid.add_to(parser)
        common_args.skip_build.add_to(parser)
        common_args.directory.add_to(parser)
        common_args.build_dir.add_to(parser)
        common_args.base_dir.add_to(parser)
        common_args.fail_fast.add_to(parser)
        common_args.threads.add_to(parser)
        common_args.list_only_failed.add_to(parser)

        # Modify the help statement for the tags common_arg
        mytags = common_args.tags.copy()
        mytags.kwargs['help'] = ('Only run items marked with one of the given'
                                 ' tags.')
        mytags.add_to(parser)


class ListParser(ArgParser):
    '''
    Parser for the \'list\' command.
    '''
    def __init__(self, subparser):
        parser = subparser.add_parser(
            'list',
            help='''List and query test metadata.'''
        )
        super(ListParser, self).__init__(parser)

        Argument(
            '--suites',
            action='store_true',
            default=False,
            help='List all test suites.'
        ).add_to(parser)
        Argument(
            '--tests',
            action='store_true',
            default=False,
            help='List all test cases.'
        ).add_to(parser)
        Argument(
            '--fixtures',
            action='store_true',
            default=False,
            help='List all fixtures.'
        ).add_to(parser)
        Argument(
            '--all-tags',
            action='store_true',
            default=False,
            help='List all tags.'
        ).add_to(parser)

        common_args.directory.add_to(parser)
        mytags = common_args.tags.copy()
        mytags.kwargs['help'] = ('Only list items marked with one of the'
                                 ' given tags.')
        mytags.add_to(parser)


class RerunParser(ArgParser):
    def __init__(self, subparser):
        parser = subparser.add_parser(
            'rerun',
            help='''Rerun failed tests.'''
        )
        super(RerunParser, self).__init__(parser)

        common_args.skip_build.add_to(parser)
        common_args.directory.add_to(parser)
        common_args.build_dir.add_to(parser)
        common_args.base_dir.add_to(parser)
        common_args.fail_fast.add_to(parser)
        common_args.threads.add_to(parser)
        common_args.list_only_failed.add_to(parser)

config = _Config()
define_constants(config.constants)

# Constants are directly exposed and available once this module is created.
# All constants MUST be defined before this point.
config.constants = FrozenAttrDict(config.constants.__dict__)
constants = config.constants

'''
This config object is the singleton config object available throughtout the
framework.
'''
def initialize_config():
    '''
    Parse the commandline arguments and setup the config varibles.
    '''
    global config

    # Setup constants and defaults
    define_defaults(config._defaults)
    define_post_processors(config)
    define_common_args(config)

    # Setup parser and subcommands
    baseparser = CommandParser()
    runparser = RunParser(baseparser.subparser)
    listparser = ListParser(baseparser.subparser)
    rerunparser = RerunParser(baseparser.subparser)

    # Initialize the config by parsing args and running callbacks.
    config._init(baseparser)
