/* autogenerated by gensyscalls.py */
#include <asm/unistd.h>
#include <linux/err.h>
#include <machine/asm.h>

ENTRY(mlockall)
    ldr     ip, =__NR_mlockall
    b       __bionic_syscall_eabi
END(mlockall)
