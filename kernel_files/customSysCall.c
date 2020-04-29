#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/types.h>

asmlinkage void sys_fetchSysTime(long *sec, long *nsec) {
    struct timespec tSpec;
    getnstimeofday(&tSpec);
    copy_to_user(sec, &(tSpec.tv_sec), sizeof(tSpec.tv_sec));
    copy_to_user(nsec, &(tSpec.tv_nsec), sizeof(tSpec.tv_nsec));
    return;
}

asmlinkage void sys_printTaskInfo(pid_t pid, long *start_Sec, long *start_nSec, long *end_Sec, long *end_nSec) {
    printk("[Project1] %d %ld.%ld %ld.%ld\n", pid, *start_Sec, *start_nSec, *end_Sec, *end_nSec);
    return;
}