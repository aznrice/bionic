/* autogenerated by gensyscalls.py */
#include <asm/unistd.h>
#include <linux/err.h>
#include <machine/asm.h>

ENTRY(sigpending)
    ldr     ip, =__NR_sigpending
    b       __bionic_syscall_eabi
END(sigpending)
