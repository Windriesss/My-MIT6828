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
    if (!((err & FEC_WR) && (uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_COW)))
        panic("Neither the fault is a write nor COW page. \n");
    // LAB 4: Your code here.
    envid_t envid = sys_getenvid();
    // cprintf("pgfault: envid: %d\n", ENVX(envid));
    // 临时页暂存
    if ((r = sys_page_alloc(envid, (void *)PFTEMP, PTE_P | PTE_W | PTE_U)) < 0)
        panic("pgfault: page allocation fault:%e\n", r);
    addr = ROUNDDOWN(addr, PGSIZE);
    memcpy((void *)PFTEMP, (const void *)addr, PGSIZE);
    if ((r = sys_page_map(envid, (void *)PFTEMP, envid, addr, PTE_P | PTE_W | PTE_U)) < 0)
        panic("pgfault: page map failed %e\n", r);

    if ((r = sys_page_unmap(envid, (void *)PFTEMP)) < 0)
        panic("pgfault: page unmap failed %e\n", r);
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
        int r;

        void *addr;
        pte_t pte;
        int perm;

        addr = (void *)((uint32_t)pn * PGSIZE);
        pte = uvpt[pn];
        perm = PTE_P | PTE_U;
        if ((pte & PTE_W) || (pte & PTE_COW))
                perm |= PTE_COW;
        if ((r = sys_page_map(thisenv->env_id, addr, envid, addr, perm)) < 0) {
                panic("duppage: page remapping failed %e", r);
                return r;
        }
        if (perm & PTE_COW) {
                if ((r = sys_page_map(thisenv->env_id, addr, thisenv->env_id, addr, perm)) < 0) {
                        panic("duppage: page remapping failed %e", r);
                        return r;
                }
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
