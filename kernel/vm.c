#include "defs.h"
#include "elf.h"
#include "fs.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "riscv.h"
#include "spinlock.h"
#include "types.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[];  // trampoline.S

void kvm_map_pagetable(pagetable_t pgtbl) {
  // 1. 物理内存管理器的获取物理页，作为全局内核页表
  // 2.
  // 使用虚拟内存管理器的虚拟页的虚拟地址映射到物理页的物理地址，为全局内核页表添加映射，有uart、硬件、中断控制、text段、data段、跳板，这些映射都是直接映射，也就是虚拟地址和物理地址相同，不需要添加使用物理内存管理器的获取物理页，然后将虚拟地址映射到获取的物理页的物理地址。
  // 3.
  // 如果是所有进程只使用同一张全局内核页表，没有进程的内核页表，那么地址空间会有多个内核栈与多个空页交替出现，为每个内核栈使用物理内存管理的分配物理页，然后使用虚拟地址管理器的虚拟页的虚拟地址映射到物理页的物理地址，对全局内核页表的每个内核栈的虚拟地址映射到物理页的物理地址，但不需要为空页分配物理页和映射。如果进程拥有自己的内核页表，全局内核页表就不需要有内核栈的映射。

  // uart registers
  kvmmap(pgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(pgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  // local interrupt controller, used to configure timers. not needed after the
  // kernel boots up. no need to map to process-specific kernel page tables. it
  // also lies at 0x02000000, which is lower than PLIC's 0x0c000000 and will
  // conflict with process memory, which is located at the lower end of the
  // address space.

  // kvmmap(pgtbl, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(pgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(pgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(pgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
         PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

pagetable_t kvminit_newpgtbl() {
  // 1. 物理内存管理器的获取物理页，作为全局内核页表
  // 2.
  // 为页表添加虚拟地址到物理地址映射，有uart、硬件、中断控制、text段、data段、内核栈、跳板。

  pagetable_t pgtbl = (pagetable_t)kalloc();
  memset(pgtbl, 0, PGSIZE);

  kvm_map_pagetable(pgtbl);

  return pgtbl;
}

/*
 * create a direct-map page table for the kernel.
 */
void kvminit() {
  kernel_pagetable = kvminit_newpgtbl();
  // CLINT *is* however required during kernel boot up and
  // we should map it for the global kernel pagetable
  kvmmap(kernel_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart() {
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
  // 1. 对于当前虚拟地址和页表，递归
  // 2. 递归终点，如果当前深度大于页表级数，结束
  // 3. 利用当前深度取出虚拟地址的固定位数，作为当前页表的下标，找到页表项
  // 4. 如果页表项标记无效，返回false
  // 5.
  // 如果页表项标记可读可写可执行都是0，说明不是最后一级页表，利用页表项找到下一级页表，递归
  // 6.
  // 否则，遇到最后一级页表的页表项，利用虚拟地址和最后一级页表的页表项得到物理页的物理地址。

  if (va >= MAXVA) panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0) {
        if (alloc && pagetable == 0) {
          // printf("trace: failed kalloc, freelist: %p\n", kget_freelist());
        }
        return 0;
      }
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA) return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0) return 0;
  if ((*pte & PTE_V) == 0) return 0;
  if ((*pte & PTE_U) == 0) return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to a kernel page table. (lab3 enables standalone kernel page
// tables for each and every process) only used when booting. does not flush TLB
// or enable paging.
void kvmmap(pagetable_t pgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(pgtbl, va, sz, pa, perm) != 0) panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64 kvmpa(pagetable_t kernelpgtbl, uint64 va) {
  //
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;

  pte = walk(kernelpgtbl,
             va 0);  // read from the process-specific kernel pagetable instead
  if (pte == 0) panic("kvmpa");
  if ((*pte & PTE_V) == 0) panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa + off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa,
             int perm) {
  // 1. 对于当前虚拟地址和页表，递归
  // 2. 递归终点，如果当前深度大于页表级数，结束
  // 3. 利用当前深度取出虚拟地址的固定位数，作为当前页表的下标，找到页表项
  // 4.
  // 如果页表项标记无效，说明没有下一级页表，需要使用物理内存管理器的获得物理页，作为下一级页表，将物理页的地址放到当前页表项。
  // 5.
  // 如果页表项标记可读可写可执行都是0，说明不是最后一级页表，利用页表项找到下一级页表，递归
  // 6. 否则，遇到最后一级页表的页表项，将参数物理页的地址放到当前页表项。

  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0) return -1;
    if (*pte & PTE_V) panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last) break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0) panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    if ((pte = walk(pagetable, a, 0)) == 0) panic("uvmunmap: walk");
    if ((*pte & PTE_V) == 0) panic("uvmunmap: not mapped");
    if (PTE_FLAGS(*pte) == PTE_V) panic("uvmunmap: not a leaf");
    if (do_free) {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0) return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvminit(pagetable_t pagetable, uchar *src, uint sz) {
  char *mem;

  if (sz >= PGSIZE) panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  char *mem;
  uint64 a;

  if (newsz < oldsz) return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE) {
    mem = kalloc();
    if (mem == 0) {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem,
                 PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz) return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Just like uvmdealloc, but without freeing the memory.
// Used for syncing up kernel page-table's mapping of user memory.
uint64 kvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz) return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 0);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
  // 1. 遍历当前进程虚拟地址空间的每个虚拟页的虚拟地址
  // 2. 对于当前虚拟地址和页表，递归
  // 3. 递归终点，如果当前深度大于页表级数，级数
  // 4. 利用当前深度取出虚拟地址的固定位数，作为当前页表的下标，找到页表项
  // 5. 如果页表标记无效，说明没有下一级页表，返回
  // 6.
  // 如果页表标记可读可写可执行都是0，说明不是最后一级页表，利用页表项找到下一级页表，然后清空当前页表项
  // 7. 否则，遇到最后一级页表项，清空当前页表项，返回
  // 8.
  // 当循环结束，已经清除当前页表的所有页表项，使用物理内存管理器的归还物理页，将当前页表归还。

  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free a process-specific kernel page-table,
// without freeing the underlying physical memory
void kvm_free_kernelpgtbl(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    uint64 child = PTE2PA(pte);
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      kvm_free_kernelpgtbl((pagetable_t)child);
      pagetable[i] = 0;
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
  // 1. 遍历当前进程用户地址空间的每个虚拟页的虚拟地址
  // 2.
  // 利用虚拟地址管理器的虚拟地址找到物理页的物理地址，找到当前虚拟页对应的物理页，使用物理内存管理器的归还物理页，将物理页释放
  // 3. 释放完所有的物理页，然后使用虚拟内存管理器的释放页表

  if (sz > 0) uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  // 1. 遍历当前进程虚拟地址空间的每个虚拟页的虚拟地址
  // 2.
  // 利用虚拟地址管理器的虚拟页的虚拟地址找到物理页的物理地址，找到当前进程对应的物理页的物理地址
  // 3. 利用物理内存管理器的获取物理页，作为子进程的物理页
  // 4. 将当前进程的物理页数据拷贝到子进程的物理页
  // 5.
  // 使用虚拟地址管理器的虚拟页虚拟地址映射到物理页物理地址，将子进程对应当前进程的相同的虚拟页的虚拟地址映射到子进程的物理页的物理地址

  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE) {
    if ((pte = walk(old, i, 0)) == 0) panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0) panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0) goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// Copy some of the mappings from src into dst.
// Only copies the page table and not the physical memory.
// returns 0 on success, -1 on failure.
int kvmcopymappings(pagetable_t src, pagetable_t dst, uint64 start, uint64 sz) {
  // 1. 遍历原页表虚拟地址空间的每个虚拟页的虚拟地址
  // 2.
  // 利用虚拟地址管理器的虚拟页的虚拟地址找到物理页的物理地址，找到原页表的虚拟页虚拟地址对应的物理页的物理地址
  // 3.
  // 使用虚拟地址管理器的虚拟页虚拟地址映射到物理页物理地址，将目标页表的相同的虚拟页的虚拟地址映射到原页表的的物理页的物理地址。

  pte_t *pte;
  uint64 pa, i;
  uint flags;

  // PGROUNDUP: prevent re-mapping already mapped pages
  for (i = PGROUNDUP(start); i < start + sz; i += PGSIZE) {
    if ((pte = walk(src, i, 0)) == 0)
      panic("kvmcopymappings: pte should exist");
    if ((*pte & PTE_V) == 0) panic("kvmcopymappings: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    // `& ~PTE_U` marks the page as kernel page and not user page.
    // Required or else kernel can not access these pages.
    if (mappages(dst, i, PGSIZE, pa, flags & ~PTE_U) != 0) {
      goto err;
    }
  }

  return 0;

err:
  uvmunmap(dst, 0, i / PGSIZE, 0);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0) panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
// 1. 对用户地址空间的虚拟地址，利用进程用户页表，使用虚拟内存管理器的虚拟页虚拟地址找到物理页物理地址，获得用户地址空间的物理页的物理地址
// 2. 使用memmove或者strcpy将内核地址空间的数据拷贝到物理页的物理地址。
// 3. 如果内核页表同步用户页表，也就是内核页表既有映射内核地址空间，又映射用户地址空间，则不需要使用虚拟内存管理器的虚拟页虚拟地址找到物理页物理地址，寻找用户地址的物理页物理地址的时候可以mmu自动使用使用内核页表。
// 4. 内核页表同步用户页表，也就是从内核页表对应的内核地址空间中，找到一段无用的内核地址空间，作为用户地址空间，用户页表的任何变化都应该同步到内核页表的用户地址空间中。我们选择内核地址空间的0到PLIC不包含PLIC，作为用户地址空间，这段内核地址空间仅有CLINT的虚拟页映射到物理页，而且这个映射只在内核启动起作用，也就是全局内核页表的CLIINT的虚拟页映射到物理页才有用，进程的内核页表并不需要映射CLINT。然后exec检查程序内存不能超过PLIC。然后将每个修改到进程用户页表的，将修改同步到进程内核页表，比如fork将当前进程的地址空间拷贝到子进程的地址空间时，会修改子进程的用户地址空间，需要将修改同步到子进程的内核页表，使用虚拟页表管理器的拷贝页表。比如exec在构建好新进程的用户页表后，需要使用虚拟内存管理器的拷贝课表，同步到新进程的内核页表。比如进程管理器的增加用户地址空间，修改完用户页表后，需要使用虚拟内存管理器的拷贝页表将修改同步到内核页表。

  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PGSIZE - (dstva - va0);
    if (n > len) n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

int copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
int copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  // 1.
  // 对用户地址空间的虚拟地址，利用进程用户页表，使用虚拟内存管理器的虚拟页虚拟地址找到物理页物理地址，获得用户地址空间的物理页的物理地址
  // 2. 使用memmove或者strcpy将内核地址空间的数据拷贝到物理页的物理地址。
  // 3.
  // 如果内核页表同步用户页表，也就是内核页表既有映射内核地址空间，又映射用户地址空间，则不需要使用虚拟内存管理器的虚拟页虚拟地址找到物理页物理地址，寻找用户地址的物理页物理地址的时候可以mmu自动使用使用内核页表。

  // printf("trace: copyin1 %p\n", *walk(pagetable, srcva, 0));
  // printf("trace: copyin2 %p\n", *walk(myproc()->kernelpgtbl, srcva, 0));
  // printf("trace: copyin3 %p\n", *(uint64*)srcva);
  return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  // printf("trace: copyinstr %p\n", walk(pagetable, srcva, 0));
  return copyinstr_new(pagetable, dst, srcva, max);
}

int pgtblprint(pagetable_t pagetable, int depth) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if (pte & PTE_V) {
      // print
      printf("..");
      for (int j = 0; j < depth; j++) {
        printf(" ..");
      }
      printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));

      // if not a leaf page table, recursively print out the child table
      if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
        // this PTE points to a lower-level page table.
        uint64 child = PTE2PA(pte);
        pgtblprint((pagetable_t)child, depth + 1);
      }
    }
  }
  return 0;
}

int vmprint(pagetable_t pagetable) {
  // 1. 遍历当前进程虚拟地址空间的每个虚拟页的虚拟地址
  // 2. 对于当前虚拟地址，递归
  // 3. 递归终点，如果当前深度大于页表级数，结束
  // 4. 利用当前深度取出虚拟地址的固定位数，作为当前页表的下标，页表项
  // 5. 如果页表项标记无效，返回
  // 6.
  // 如果页表项标记可读可写可执行都是0，说明不是最后一级页表，利用页表项找到下一级页表，递归
  // 7. 否则，遇到最后一级页表的页表项，打印页表项。

  printf("page table %p\n", pagetable);
  return pgtblprint(pagetable, 0);
}