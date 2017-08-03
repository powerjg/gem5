'''
Test file for the util m5 exit assembly instruction.
'''
from testlib import *

ref_path = joinpath(getcwd(), 'ref', 'X86', 'linux', 'se-default')

verifiers = (
        verifier.MatchStdout(joinpath(ref_path, 'simout')),

        # The se.py script is dumb and sets a strange return code on success.
        verifier.VerifyReturncode(1),
)

hello_program = TestProgram('hello', 'X86', 'linux')

gem5_verify_config(
        name='test_hello',
        fixtures=(hello_program,),
        verifiers=verifiers,
        config=joinpath(config.base_dir, 'configs', 'example','se.py'),
        config_args=['--cmd', hello_program.path],
        valid_isas=('X86',)
)
