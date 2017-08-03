'''
File which implements querying and display logic for metadata about loaded
items.
'''
from logger import log
from terminal import separator

def list_fixtures(loader):
    log.display(separator())
    log.display('Listing all Fixtures.')
    log.display(separator())
    for fixture in loader.fixtures:
        log.display(fixture.name)

def list_tests(loader):
    log.display(separator())
    log.display('Listing all TestCases.')
    log.display(separator())
    for test in loader.tests:
        log.display(test.uid)

def list_suites(loader):
    log.display(separator())
    log.display('Listing all TestSuites.')
    log.display(separator())
    for suite in loader.suites:
        log.display(suite.uid)

def list_tags(loader):
    log.display(separator())
    log.display('Listing all Tags.')
    log.display(separator())
    for tag in loader.tags:
        log.display(tag)

def list_tests_with_tags(loader, tags):
    log.display('Listing tests based on tags.')
    for tag in tags:
        log.display(separator())
        log.display("Tests marked with tag '%s':" % tag)
        log.display(separator())
        for test in loader.tag_index(tag):
            log.display(test.uid)
