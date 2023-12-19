#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE();

int init_module(void)
{
    printk(KERN_INFO "Group #10:\nMobina Mehrazar : 810100216\nAmin Yousefi    : 810100236\nMatin Nabizadeh : 810100223\n");
    return 0;
}

void cleanup_module(void) {}