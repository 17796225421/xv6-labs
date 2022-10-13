#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "riscv.h"
#include "spinlock.h"
#include "types.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void) { initlock(&tickslock, "time"); }

// set up to take exceptions and traps while in the kernel.
void trapinithart(void) { w_stvec((uint64)kernelvec); }

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void) {
  // 1. 如果是时钟中断
  //   a. 如果进程有alarm中断
  //   b. 进程alarm中断剩余时间--
  //   c. 如果alarm中断剩余时间小于等于0
  //   d. 要求alarm中断标记已结束
  //   e. 开启alarm中断标记未结束
  //   f. 进程alarm中断上下文保存进程中断上下文
  //   g. 进程中断上下文的通用寄存器存放进程alarm中断函数指针
  //   h. 使用进程管理器的系统调用yield
  //   i. 返回后，使用陷阱管理器的从内核态转为用户态。
  // 2. 如果是缺页异常
  //   a.
  //   比较进程虚拟地址空间大小和缺失的页的虚拟地址，如果小于进程虚拟地址空间大小比较小，说明没有懒分配，中止进程
  //   b.
  //   使用物理内存管理器的获取物理页，使用虚拟内存管理器的虚拟页虚拟地址映射到物理页物理地址，将缺失的虚拟页映射到物理页。

  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();

  if (r_scause() == 8) {
    // system call

    if (p->killed) exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if ((which_dev = devintr()) != 0) {
    // ok
  } else {
    uint64 va = r_stval();
    if ((r_scause() == 13 || r_scause() == 15) && uvmshouldtouch(va)) {
      uvmlazytouch(va);  // lazy page allocation
    } else {
      printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      p->killed = 1;
    }
  }

  if (p->killed) exit(-1);

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2) yield();

  usertrapret();
}

//
// return to user space
//
void usertrapret(void) {
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();          // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE;  // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();  // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE;  // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap() {
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0) panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr()) == 0) {
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING) yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void clockintr() {
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr() {
  uint64 scause = r_scause();

  if ((scause & 0x8000000000000000L) && (scause & 0xff) == 9) {
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if (irq == UART0_IRQ) {
      uartintr();
    } else if (irq == VIRTIO0_IRQ) {
      virtio_disk_intr();
    } else if (irq) {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if (irq) plic_complete(irq);

    return 1;
  } else if (scause == 0x8000000000000001L) {
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if (cpuid() == 0) {
      clockintr();
    }

    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}
