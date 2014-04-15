#include <types.h>
#include <pid_table.h>
#include <synch.h>
#include <proc.h>
#include <limits.h>
#include <thread.h>
#include <addrspace.h>
#include <current.h>
#include <kern/errno.h>
#include <syscall.h>
#include <pid_table.h>
#include <mips/trapframe.h>

struct child_data {
    struct trapframe *tf;
    struct semaphore *sem;
};

static
struct proc *
create_child(pid_t pid) {
    struct proc *child = proc_create(curproc->p_name);
    if (child == NULL) goto proc_out;

    procmap_add(pid, child);

    /* make a shared link between parent and child */
    child->parent = shared_link_create(pid);
    if (child->parent == NULL) goto parent_out;
    child->parent->ref_count++;

    /* copy address space */
    int rv = as_copy(curproc->p_addrspace, &child->p_addrspace);
    if (rv == ENOMEM) goto as_out;

    /* copy file descriptor pointers */
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->fd_table[i] != NULL) {
            child->fd_table[i] = curproc->fd_table[i];
            curproc->fd_table[i]->ref_count++;
        }
    }

    /* copy cwd */
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		child->p_cwd = curproc->p_cwd;
	}

    child->pid = pid;

    return child;

as_out:
    shared_link_destroy(child->pid, child);
parent_out:
    proc_destroy(child);
proc_out:
    return NULL;
}

static
int
get_next_child_index(struct proc *proc) {
    for (int i = 0; i < MAX_CLD; i++)
        if (proc->children[i] == NULL) return i;
    return -1;
}

static
void
child_fork(void *data1, unsigned long data2) {
    (void)data2;

    struct child_data child_data = *(struct child_data *)data1;
    struct trapframe tf = *child_data.tf;
    tf.tf_v0 = 0;
    tf.tf_a3 = 0;
    tf.tf_epc += 4;

    as_activate();

    V(child_data.sem);
    mips_usermode(&tf);
}

pid_t sys_fork(struct trapframe *tf, pid_t *child_pid) {
    pid_t new_pid = pid_get();
    if (new_pid == -1) return ENPROC;

    struct proc *child = create_child(new_pid);
    if (child == NULL) goto child_out;

    int index = get_next_child_index(curproc);
    if (index == -1) goto index_out;

    struct semaphore *sem = sem_create("wait for child", 0);
    if (sem == NULL) goto sem_out;

    struct child_data child_data;
    child_data.tf = tf;
    child_data.sem = sem;

    int rv = thread_fork("child", child, child_fork, &child_data, 0);
    if (rv) goto thread_out;

    curproc->children[index] = child->parent;
    curproc->children[index]->ref_count++;

    *child_pid = new_pid;

    P(sem);
    sem_destroy(sem);

    return 0;

thread_out:
    sem_destroy(sem);
sem_out:
index_out:
    proc_destroy(child);
child_out:
    pid_destroy(new_pid);
    return ENOMEM;
}

