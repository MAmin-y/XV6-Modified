#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "prioritylock.h"

static int digital_root(int n)
{
    if (n < 0)
        return -1;
    
    while (n > 9)
    {
        int sum = 0;

        while (n != 0)
        {
            sum += n % 10;
            n /= 10;
        }

        n = sum;
    }
    return n;
}

int sys_find_digital_root(void)
{
    return digital_root(myproc()->tf->ebx);
}

struct prioritylock plk;

int sys_init_prioritylock(void)
{
    initprioritylock(&plk, "priority lock");
    return 0;
}

int sys_acquire_prioritylock(void)
{
    acquirepriority(&plk);
    return 0;
}

int sys_release_prioritylock(void)
{
    releasepriority(&plk);
    return 0;
}

int sys_print_cpu_syscalls_count(void)
{
    for(int i = 0; i < ncpu; i++)
    {
        cprintf("---CPU %d: %d\n", cpus[i].apicid, cpus[i].syscall_counter);
        cpus[i].syscall_counter = 0;
    }
    cprintf("---Total: %d\n", total_syscall_counter);
    total_syscall_counter = 0;
    return 0;
}
