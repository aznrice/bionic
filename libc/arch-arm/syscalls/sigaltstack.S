/* autogenerated by gensyscalls.py */
#include <asm/unistd.h>
#include <linux/err.h>
#include <machine/asm.h>

ENTRY(sigaltstack)
    ldr     ip, =__NR_sigaltstack
    b       __bionic_syscall_eabi
END(sigaltstack)
