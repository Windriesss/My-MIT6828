// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

    // LAB 4: Your code here.
    if (!((err & FEC_WR) && (uvpt[PGNUM(addr)] & PTE_COW)))
		panic("pgfault:not writabled or a COW page!\n");

	// LAB 4: Your code here.
	envid_t envid = sys_getenvid();
    //为PFTEMP分配一个物理页
	if ((r = sys_page_alloc(envid, (void *)PFTEMP, PTE_P | PTE_W | PTE_U)) < 0)
		panic("pgfault:page allocation failed: %e", r);
	addr = ROUNDDOWN(addr, PGSIZE);
    //将addr上的物理页内容拷贝到PFTEMP指向的物理页上
	memmove((void *)PFTEMP, (void *)addr, PGSIZE);
    //更改addr映射的物理页，改为与PFTEMP指向相同
	if ((r = sys_page_map(envid, PFTEMP, envid, addr, PTE_P | PTE_W | PTE_U)) < 0)
		panic("pgfault:page map failed: %e", r);
    //取消PFTEMP的映射
	if ((r = sys_page_unmap(envid, PFTEMP)) < 0)
		panic("pgfault: page unmap failed: %e", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
    int ret;

    void *va;
    pte_t pte;
    int perm;

    va = (void *)((uint32_t)pn * PGSIZE);
    
    if (uvpt[pn] & PTE_SHARE) {
        if ((ret = sys_page_map(thisenv->env_id, (void *)va, envid, (void *)va, uvpt[pn] & PTE_SYSCALL)) < 0)
            return ret;
    } else if ((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW)) {
        // 子进程标记
        if ((ret = sys_page_map(thisenv->env_id, (void *)va, envid, (void *)va, PTE_P | PTE_U | PTE_COW)) < 0)
            return ret;
        // 父进程标记
        if ((ret = sys_page_map(thisenv->env_id, (void *)va, thisenv->env_id, (void *)va, PTE_P | PTE_U | PTE_COW)) < 0)
            return ret;
    } else {
        // 简单映射
        if ((ret = sys_page_map(thisenv->env_id, (void *)va, envid, (void *)va, PTE_P | PTE_U)) < 0)
            return ret;
    }

    return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//


envid_t
fork(void)
{
    // LAB 4: Your code here.
    envid_t envid;
    int r;
    size_t i, j, pn;
    // Set up our page fault handler
    set_pgfault_handler(pgfault);

    envid = sys_exofork();

    if (envid < 0) {
        panic("sys_exofork failed: %e", envid);
    }

    if (envid == 0) {
        // child
        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
    }
    // here is parent !
    // Copy our address space and page fault handler setup to the child.

    for (pn = PGNUM(UTEXT); pn < PGNUM(USTACKTOP); pn++) {
        if ((uvpd[pn >> 10] & PTE_P) && (uvpt[pn] & PTE_P)) {
            // 页表
            if ((r = duppage(envid, pn)) < 0)
                return r;
        }
    }
    // alloc a page and map child exception stack
    if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W)) < 0)
        return r;
    extern void _pgfault_upcall(void);
    if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
        return r;

    // Start the child environment running
    if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
        panic("sys_env_set_status: %e", r);

    return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
