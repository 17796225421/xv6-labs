#include "date.h"
#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "riscv.h"
#include "spinlock.h"
#include "types.h"

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  if (argaddr(0, &p) < 0) return -1;
  return wait(p);
}

uint64 sys_sbrk(void) {
  // 1. 如果是立即分配物理页
  // 2. 如果n大于0,
  //   a. 使用物理内存管理器的获取物理页
  //   b. 使用虚拟内存管理器的虚拟页虚拟地址映射到物理页物理地址
  //   c. 使用虚拟页表管理器的拷贝页表，将用户页表的改变同步到内核页表
  // 3. 如果n小于0，使用虚拟内存管理器的释放地址空间，也对内核页表使用。
  // 4. 如果是懒分配物理页
  // 5. 如果n小于0，使用虚拟内存管理器的释放地址空间，也对内核页表使用
  // 6.
  // 如果是n大于0，只增加进程的用户空间大小变量，不进行其他操作。交给陷阱管理器的选择陷阱类型来处理缺页异常

  int addr;
  int n;
  struct proc *p = myproc();
  if (argint(0, &n) < 0) return -1;
  // printf("sbrk: %d\n",n);
  addr = p->sz;
  // lazy allocation
  if (n < 0) {
    uvmdealloc(p->pagetable, p->sz, p->sz + n);  // dealloc immediately
  }
  p->sz += n;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
