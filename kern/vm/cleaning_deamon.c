#include <types.h>
#include <coremap.h>
#include <synch.h>
#include <cleaning_deamon.h>
#include <lib.h>
#include <thread.h>
#include <proc.h>
#include <current.h>


// Go through and clean all that you can
static void run_deamon(){
	for(unsigned i = 0; i < coremap.size; i++){
		// TODO we can probably wait here if this becomes an issue
		if(core_set_busy(i, NO_WAIT) == 0){
			if(coremap.cm[i].kern == 1 || coremap.cm[i].dirty == 0){
				core_set_free(i);
				continue;
			}

			clean_cme(i);

			core_set_free(i);
		}
	}
}


static void start_deamon_thread(void *ptr, unsigned long nargs){
	(void)nargs;
	(void)ptr;
	deamon.lock = lock_create("deamon lock");
	deamon.cv = cv_create("deamon cv");
	if(deamon.cv == NULL || deamon.lock == NULL)
		panic("deamon creation failed");

	while(1){
		// Less than or equal to as full mem used by kernel would result in an infinite loop otherwise
		while((coremap.modified*4)<=((coremap.used - coremap.kernel)*3)){
			lock_acquire(deamon.lock);
			cv_wait(deamon.cv, deamon.lock);
		}
		run_deamon();
		lock_release(deamon.lock);
	}
}

void cleaning_bootstrap(void){
	int result = thread_fork("Eviction Deamon" /* thread name */,
				kproc /* new process */,
				start_deamon_thread /* thread function */,
				NULL/* thread arg */,
				0 /* thread arg */);
	if (result)
		kprintf("deamon thread_fork failed: %s\n", strerror(result));
}
