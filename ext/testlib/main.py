#!/usr/bin/env python2
'''
The main source for whimsy. Discovers and loads sources using the
:class:`TestLoader` objects and runs tests using the :class:`Runner` object
passing the runner :class:`ResultLogger` instances which will stream output
data to the terminal and into various result files.

There are three commands which this program handles:

* run - By default will search for and run all tests in the current
    and children directories reporting the results through the terminal,
    saving them to a pickle file, and saving them to a junit file.

* rerun - Load all tests and then rerun the tests which failed in the previous
    run.

* list  - List tests with various querying options.
'''
import logger
import query
import result

import config
from helper import joinpath, mkdir_p
from loader import TestLoader
from logger import log
from runner import Runner
from terminal import separator

# TODO: Standardize separator usage.
# Probably make it the caller responsiblity to place separators and internal
# ones can be used to separate internal input.

def load_tests():
    '''
    Create a TestLoader and load tests for the directory given by the config.
    '''
    testloader = TestLoader()
    if not config.config.quiet:
        log.display(separator())
        log.bold('Loading Tests')
        log.display('')
    testloader.load_root(config.config.directory)
    return testloader

def dorun():
    '''
    Handle the `run` command.
    '''
    loader = load_tests()
    tags = config.config.get_tags()
    suites = [suite for suite in loader.suites if suite.match_tags(tags)]

    # Create directory to save junit and internal results in.
    mkdir_p(config.config.result_path)

    with open(joinpath(config.config.result_path, 'pickle'), 'w') as result_file,\
         open(joinpath(config.config.result_path, 'junit.xml'), 'w') as junit_f:

        junit_logger = result.JUnitLogger(junit_f, result_file)
        console_logger = result.ConsoleLogger()
        loggers = (junit_logger, console_logger)

        log.display(separator())
        log.bold('Running Tests')
        log.display('')
        if config.config.uid:
            test_item = loader.get_uid(config.config.uid)
            results = Runner.run_items(test_item)
        else:
            testrunner = Runner(suites, loggers)
            results = testrunner.run()

def dorerun():
    '''
    Handle the `rerun` command.
    '''
    # Load previous results
    # TODO Catch bad file path error or load error.
    with open(joinpath(config.config.result_path, 'pickle'), 'r') as old_fstream:
        old_formatter = result.InternalLogger.load(old_fstream)

    # Load tests
    loader = load_tests()

    # Get the self contained suites which hold tests that fail and run each.
    reruns = []
    for suite in old_formatter.suites:
        if suite.outcome in (result.Outcome.FAIL, result.Outcome.ERROR):
            suite = loader.get_uid(suite.uid)
            reruns.append(suite)

    # Run only the suites we need to rerun.
    testrunner = Runner(reruns)
    testrunner.run()

def dolist():
    '''
    Handle the `list` command.
    '''
    loader = load_tests()
    if config.config.suites:
        query.list_suites(loader)
    elif config.config.tests:
        query.list_tests(loader)
    elif config.config.fixtures:
        query.list_fixtures(loader)
    elif config.config.all_tags:
        query.list_tags()
    elif config.config.get_tags():
        query.list_suites_with_tags(loader, config.config.get_tags())

def main():
    # Start logging verbosity at its minimum
    logger.set_logging_verbosity(0)

    # Initialize the config
    config.initialize_config()

    # Then do parsing of the arguments to init config.
    logger.set_logging_verbosity(config.config.verbose)

    # 'do' the given command.
    globals()['do'+config.config.command]()

if __name__ == '__main__':
    main()
