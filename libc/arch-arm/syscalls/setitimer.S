/* autogenerated by gensyscalls.py */
#include <asm/unistd.h>
#include <linux/err.h>
#include <machine/asm.h>

ENTRY(setitimer)
    ldr     ip, =__NR_setitimer
    b       __bionic_syscall_eabi
END(setitimer)
