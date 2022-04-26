# Copyright (c) 2021 The Regents of the University of California
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.objects import RubyProtocols

from ..runtime import get_supported_isas, get_supported_ruby_protocols
from ..isas import ISA
from typing import Optional
import os
import inspect


def _get_exception_str(msg: str):
    # The inspect module allows us to get information about live objects,
    # modules, classes, etc. Here was use it to generate an exception string
    # that incorporates information on what file or class this requirement was
    # stated. `inspect.stack()[1]` is the `requires` caller method. One above
    # this on the stack, `inspect.stack()[2]` should be where `requires` is
    # called.
    if inspect.stack()[2].function == "<module>":
        # If the caller is a Python module, we use the filename. This is for
        # the case where the `requires` function is called outside of a class.
        name = inspect.stack()[2].filename
    else:
        # Otherwise we assume the `requires` is being called by a class, in
        # which case we label the exception message with the class name.
        name = inspect.stack()[2].frame.f_locals["self"].__class__.__name__
    return f"[{name}] {msg}"


def requires(
    isa_required: Optional[ISA] = None,
    coherence_protocol_required: Optional[str] = None,
    kvm_required: bool = False,
) -> None:
    """
    Ensures the ISA/Coherence protocol/KVM requirements are met. An exception
    will be raise if they are not.

    :param isa_required: The ISA(s) gem5 must be compiled to.
    :param coherence_protocol_required: The coherence protocol gem5 must have
        compiled.
    :param kvm_required: The host system must have the Kernel-based Virtual
        Machine available.
    :raises Exception: Raises an exception if the required ISA or coherence
        protocol do not match that of the current gem5 binary.
    """

    supported_isas = get_supported_isas()
    supported_protocols = get_supported_ruby_protocols()
    kvm_available = os.access("/dev/kvm", mode=os.R_OK | os.W_OK)

    # Note, previously I had the following code here:
    #
    # `if isa_required != None and isa_required not in supported_isas:`
    #
    # However, for reasons I do not currently understand, I frequently
    # encountered errors such as the following:
    #
    # ```
    # Exception: The required ISA is 'RISCV'. Supported ISAs:
    # SPARC
    # RISCV
    # ARM
    # X86
    # POWER
    # MIPS
    # ```
    #
    # I do not know why this happens and my various attempts at tracking down
    # why the enum did not compare correctly yielded no results. The following
    # code works, even though it is verbose and appears functionally equivalent
    # to the original code.
    if isa_required != None and isa_required.value not in (
        isa.value for isa in supported_isas
    ):
        msg = f"The required ISA is '{isa_required.name}'. Supported ISAs: "
        for isa in supported_isas:
            msg += f"{os.linesep}{isa.name}"
        raise Exception(_get_exception_str(msg=msg))

    if (
        coherence_protocol_required != None
        and RubyProtocols(coherence_protocol_required) not in
            supported_protocols
    ):
        msg = f"The required Ruby protocol is '{coherence_protocol_required}'."
        msg += " Supported protocols: "
        for protocol in supported_protocols:
            msg += f"{os.linesep}{protocol}"
        raise Exception(_get_exception_str(msg=msg))

    if kvm_required and not kvm_available:
        raise Exception(
            _get_exception_str(
                msg="KVM is required but is unavailable on this system"
            )
        )
