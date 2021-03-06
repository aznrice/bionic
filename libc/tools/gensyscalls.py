#!/usr/bin/python

# This tool is used to generate the assembler system call stubs,
# the header files listing all available system calls, and the
# makefiles used to build all the stubs.

import sys, os.path, glob, re, commands, filecmp, shutil

from bionic_utils import *

bionic_libc_root = os.environ["ANDROID_BUILD_TOP"] + "/bionic/libc/"

# temp directory where we store all intermediate files
bionic_temp = "/tmp/bionic_gensyscalls/"

DRY_RUN = False

def make_dir(path):
    path = os.path.abspath(path)
    if not os.path.exists(path):
        parent = os.path.dirname(path)
        if parent:
            make_dir(parent)
        os.mkdir(path)


def create_file(relpath):
    dir = os.path.dirname(bionic_temp + relpath)
    make_dir(dir)
    return open(bionic_temp + relpath, "w")


syscall_stub_header = """/* autogenerated by gensyscalls.py */
#include <asm/unistd.h>
#include <linux/err.h>
#include <machine/asm.h>

ENTRY(%(func)s)
"""


function_alias = """
    .globl _C_LABEL(%(alias)s)
    .equ _C_LABEL(%(alias)s), _C_LABEL(%(func)s)
"""


arm_eabi_call_default = arm_header + """\
    ldr     ip, =%(__NR_name)s
    b       __bionic_syscall_eabi
END(%(func)s)
"""

arm_eabi_call_long = arm_header + """\
    ldr     ip, =%(__NR_name)s
    b       __bionic_long_syscall_eabi
END(%(func)s)
"""


#
# MIPS assembler templates for each syscall stub
#

mips_call = """/* autogenerated by gensyscalls.py */
#include <asm/unistd.h>
    .text
    .globl %(func)s
    .align 4
    .ent %(func)s

%(func)s:
    .set noreorder
    .cpload $t9
    li $v0, %(__NR_name)s
    syscall
    bnez $a3, 1f
    move $a0, $v0
    j $ra
    nop
1:
    la $t9,__set_errno
    j $t9
    nop
    .set reorder
    .end %(func)s
"""


#
# x86 assembler templates for each syscall stub
#

x86_registers = [ "%ebx", "%ecx", "%edx", "%esi", "%edi", "%ebp" ]

x86_call = """\
    movl    $%(__NR_name)s, %%eax
    int     $0x80
    cmpl    $-MAX_ERRNO, %%eax
    jb      1f
    negl    %%eax
    pushl   %%eax
    call    __set_errno
    addl    $4, %%esp
    orl     $-1, %%eax
1:
"""

x86_return = """\
    ret
END(%(func)s)
"""


#
# x86_64 assembler templates for each syscall stub
#

x86_64_call = """\
    movl    $%(__NR_name)s, %%eax
    syscall
    cmpq    $-MAX_ERRNO, %%rax
    jb      1f
    negl    %%eax
    movl    %%eax, %%edi
    call    __set_errno
    orq     $-1, %%rax
1:
    ret
END(%(func)s)
"""


def param_uses_64bits(param):
    """Returns True iff a syscall parameter description corresponds
       to a 64-bit type."""
    param = param.strip()
    # First, check that the param type begins with one of the known
    # 64-bit types.
    if not ( \
       param.startswith("int64_t") or param.startswith("uint64_t") or \
       param.startswith("loff_t") or param.startswith("off64_t") or \
       param.startswith("long long") or param.startswith("unsigned long long") or
       param.startswith("signed long long") ):
           return False

    # Second, check that there is no pointer type here
    if param.find("*") >= 0:
            return False

    # Ok
    return True


def count_arm_param_registers(params):
    """This function is used to count the number of register used
       to pass parameters when invoking an ARM system call.
       This is because the ARM EABI mandates that 64-bit quantities
       must be passed in an even+odd register pair. So, for example,
       something like:

             foo(int fd, off64_t pos)

       would actually need 4 registers:
             r0 -> int
             r1 -> unused
             r2-r3 -> pos
   """
    count = 0
    for param in params:
        if param_uses_64bits(param):
            if (count & 1) != 0:
                count += 1
            count += 2
        else:
            count += 1
    return count


def count_generic_param_registers(params):
    count = 0
    for param in params:
        if param_uses_64bits(param):
            count += 2
        else:
            count += 1
    return count


def count_generic_param_registers64(params):
    count = 0
    for param in params:
        count += 1
    return count


# This lets us support regular system calls like __NR_write and also weird
# ones like __ARM_NR_cacheflush, where the NR doesn't come at the start.
def make__NR_name(name):
    if name.startswith("__"):
        return name
    else:
        return "__NR_%s" % (name)


def add_aliases(stub, syscall):
    aliases = syscall["aliases"]
    for alias in aliases:
        stub += function_alias % { "func" : syscall["func"], "alias" : alias }
    return stub


def arm_eabi_genstub(syscall):
    num_regs = count_arm_param_registers(syscall["params"])
    if num_regs > 4:
        return arm_eabi_call_long % syscall
    return arm_eabi_call_default % syscall


def mips_genstub(syscall):
    return mips_call % syscall


def x86_genstub(syscall):
    result     = syscall_stub_header % syscall
    stack_bias = 4

    numparams = count_generic_param_registers(syscall["params"])
    for r in range(numparams):
        result     += "    pushl   " + x86_registers[r] + "\n"
        stack_bias += 4

    for r in range(numparams):
        result += "    mov     %d(%%esp), %s" % (stack_bias+r*4, x86_registers[r]) + "\n"

    result += x86_call % syscall

    for r in range(numparams):
        result += "    popl    " + x86_registers[numparams-r-1] + "\n"

    result += x86_return % syscall
    return result

def x86_genstub_socketcall(syscall):
    #   %ebx <--- Argument 1 - The call id of the needed vectored
    #                          syscall (socket, bind, recv, etc)
    #   %ecx <--- Argument 2 - Pointer to the rest of the arguments
    #                          from the original function called (socket())

    result = syscall_stub_header % syscall
    stack_bias = 4

    # save the regs we need
    result += "    pushl   %ebx" + "\n"
    stack_bias += 4
    result += "    pushl   %ecx" + "\n"
    stack_bias += 4

    # set the call id (%ebx)
    result += "    mov     $%d, %%ebx" % syscall["socketcall_id"] + "\n"

    # set the pointer to the rest of the args into %ecx
    result += "    mov     %esp, %ecx" + "\n"
    result += "    addl    $%d, %%ecx" % (stack_bias) + "\n"

    # now do the syscall code itself
    result += x86_call % syscall

    # now restore the saved regs
    result += "    popl    %ecx" + "\n"
    result += "    popl    %ebx" + "\n"

    # epilog
    result += x86_return % syscall
    return result


def x86_64_genstub(syscall):
    result = syscall_stub_header % syscall
    num_regs = count_generic_param_registers64(syscall["params"])
    if (num_regs > 3):
        # rcx is used as 4th argument. Kernel wants it at r10.
        result += "    movq    %rcx, %r10\n"

    result += x86_64_call % syscall
    return result


class State:
    def __init__(self):
        self.old_stubs = []
        self.new_stubs = []
        self.other_files = []
        self.syscalls = []


    def process_file(self, input):
        parser = SysCallsTxtParser()
        parser.parse_file(input)
        self.syscalls = parser.syscalls
        parser = None

        for syscall in self.syscalls:
            syscall["__NR_name"] = make__NR_name(syscall["name"])

            if syscall.has_key("arm"):
                syscall["asm-arm"] = add_aliases(arm_eabi_genstub(syscall), syscall)

            if syscall.has_key("x86"):
                if syscall["socketcall_id"] >= 0:
                    syscall["asm-x86"] = add_aliases(x86_genstub_socketcall(syscall), syscall)
                else:
                    syscall["asm-x86"] = add_aliases(x86_genstub(syscall), syscall)
            elif syscall["socketcall_id"] >= 0:
                E("socketcall_id for dispatch syscalls is only supported for x86 in '%s'" % t)
                return

            if syscall.has_key("mips"):
                syscall["asm-mips"] = add_aliases(mips_genstub(syscall), syscall)

            if syscall.has_key("x86_64"):
                syscall["asm-x86_64"] = add_aliases(x86_64_genstub(syscall), syscall)


    # Scan a Linux kernel asm/unistd.h file containing __NR_* constants
    # and write out equivalent SYS_* constants for glibc source compatibility.
    def scan_linux_unistd_h(self, fp, path):
        pattern = re.compile(r'^#define __NR_([a-z]\S+) .*')
        syscalls = set() # MIPS defines everything three times; work around that.
        for line in open(path):
            m = re.search(pattern, line)
            if m:
                syscalls.add(m.group(1))
        for syscall in sorted(syscalls):
            fp.write("#define SYS_%s %s\n" % (syscall, make__NR_name(syscall)))


    def gen_glibc_syscalls_h(self):
        # TODO: generate a separate file for each architecture, like glibc's bits/syscall.h.
        glibc_syscalls_h_path = "include/sys/glibc-syscalls.h"
        D("generating " + glibc_syscalls_h_path)
        glibc_fp = create_file(glibc_syscalls_h_path)
        glibc_fp.write("/* Auto-generated by gensyscalls.py; do not edit. */\n")
        glibc_fp.write("#ifndef _BIONIC_GLIBC_SYSCALLS_H_\n")
        glibc_fp.write("#define _BIONIC_GLIBC_SYSCALLS_H_\n")

        glibc_fp.write("#if defined(__arm__)\n")
        self.scan_linux_unistd_h(glibc_fp, bionic_libc_root + "/kernel/arch-arm/asm/unistd.h")
        glibc_fp.write("#elif defined(__mips__)\n")
        self.scan_linux_unistd_h(glibc_fp, bionic_libc_root + "/kernel/arch-mips/asm/unistd.h")
        glibc_fp.write("#elif defined(__i386__)\n")
        self.scan_linux_unistd_h(glibc_fp, bionic_libc_root + "/kernel/arch-x86/asm/unistd_32.h")
        glibc_fp.write("#elif defined(__x86_64__)\n")
        self.scan_linux_unistd_h(glibc_fp, bionic_libc_root + "/kernel/arch-x86/asm/unistd_64.h")
        glibc_fp.write("#endif\n")

        glibc_fp.write("#endif /* _BIONIC_GLIBC_SYSCALLS_H_ */\n")
        glibc_fp.close()
        self.other_files.append(glibc_syscalls_h_path)


    # Write the contents of syscalls.mk.
    def gen_arch_syscalls_mk(self, arch):
        path = "arch-%s/syscalls.mk" % arch
        D("generating " + path)
        fp = create_file(path)
        fp.write("# Auto-generated by gensyscalls.py. Do not edit.\n")
        fp.write("syscall_src :=\n")
        for syscall in self.syscalls:
            if syscall.has_key("asm-%s" % arch):
                fp.write("syscall_src += arch-%s/syscalls/%s.S\n" % (arch, syscall["func"]))
        fp.close()
        self.other_files.append(path)


    # Write each syscall stub.
    def gen_syscall_stubs(self):
        for syscall in self.syscalls:
            for arch in all_arches:
                if syscall.has_key("asm-%s" % arch):
                    filename = "arch-%s/syscalls/%s.S" % (arch, syscall["func"])
                    D2(">>> generating " + filename)
                    fp = create_file(filename)
                    fp.write(syscall["asm-%s" % arch])
                    fp.close()
                    self.new_stubs.append(filename)


    def regenerate(self):
        D("scanning for existing architecture-specific stub files...")

        bionic_libc_root_len = len(bionic_libc_root)

        for arch in all_arches:
            arch_path = bionic_libc_root + "arch-" + arch
            D("scanning " + arch_path)
            files = glob.glob(arch_path + "/syscalls/*.S")
            for f in files:
                self.old_stubs.append(f[bionic_libc_root_len:])

        D("found %d stub files" % len(self.old_stubs))

        if not os.path.exists(bionic_temp):
            D("creating %s..." % bionic_temp)
            make_dir(bionic_temp)

        D("re-generating stubs and support files...")

        self.gen_glibc_syscalls_h()
        for arch in all_arches:
            self.gen_arch_syscalls_mk(arch)
        self.gen_syscall_stubs()

        D("comparing files...")
        adds    = []
        edits   = []

        for stub in self.new_stubs + self.other_files:
            if not os.path.exists(bionic_libc_root + stub):
                # new file, git add it
                D("new file:     " + stub)
                adds.append(bionic_libc_root + stub)
                shutil.copyfile(bionic_temp + stub, bionic_libc_root + stub)

            elif not filecmp.cmp(bionic_temp + stub, bionic_libc_root + stub):
                D("changed file: " + stub)
                edits.append(stub)

        deletes = []
        for stub in self.old_stubs:
            if not stub in self.new_stubs:
                D("deleted file: " + stub)
                deletes.append(bionic_libc_root + stub)

        if not DRY_RUN:
            if adds:
                commands.getoutput("git add " + " ".join(adds))
            if deletes:
                commands.getoutput("git rm " + " ".join(deletes))
            if edits:
                for file in edits:
                    shutil.copyfile(bionic_temp + file, bionic_libc_root + file)
                commands.getoutput("git add " + " ".join((bionic_libc_root + file) for file in edits))

            commands.getoutput("git add %s%s" % (bionic_libc_root,"SYSCALLS.TXT"))

        if (not adds) and (not deletes) and (not edits):
            D("no changes detected!")
        else:
            D("ready to go!!")

D_setlevel(1)

state = State()
state.process_file(bionic_libc_root+"SYSCALLS.TXT")
state.regenerate()
