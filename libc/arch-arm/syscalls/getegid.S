/* autogenerated by gensyscalls.py */
#include <asm/unistd.h>
#include <linux/err.h>
#include <machine/asm.h>

ENTRY(getegid)
    ldr     ip, =__NR_getegid32
    b       __bionic_syscall_eabi
END(getegid)
