/* autogenerated by gensyscalls.py */
#include <asm/unistd.h>
#include <linux/err.h>
#include <machine/asm.h>

ENTRY(lgetxattr)
    ldr     ip, =__NR_lgetxattr
    b       __bionic_syscall_eabi
END(lgetxattr)
