#include <types.h>
#include <current.h>
#include <lib.h>
#include <log.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <uio.h>
#include <kern/iovec.h>
#include <vector.h>
#include <fs.h>
#include <sfs.h>
#include <buf.h>


static struct log_buffer *buf1, *buf2;
static struct vnode *bs;
static Vector tvector;

// Opens the disk object, this should be called before recovery
void disk_log_bootstrap(){
	if(vfs_open(kstrdup("lhd1raw:"), O_RDWR, 0, &bs) != 0)
		panic ("vfs_open failed\n");
}


// Creates the log buffer global object and log info global object, call this before recovery
int log_buffer_bootstrap(){
	// Head, tail and last_id will be set during pulling data from disk
	buf1 = kmalloc(sizeof(struct log_buffer));
	if(buf1 == NULL) goto out;

	buf1->lock = lock_create("buffer lock");
	if (buf1->lock == NULL) goto out;

	buf1->buffer_filled = 0;

	buf2 = kmalloc(sizeof(struct log_buffer));
	if(buf2 == NULL) goto out;

	buf2->lock = lock_create("buffer lock");
	if (buf2->lock == NULL) goto out;

	buf2->buffer_filled = 0;

	log_info.lock = lock_create("log info lock");
	if (log_info.lock == NULL) goto out;

    vector_init(&tvector);

	// Auto set to buf1
	log_info.active_buffer = buf1;


	return 0;

out:
	panic("Failed to create the log\n");
	return ENOMEM;
}


static int pull_meta_data(struct log_info *log_info){

	struct stored_info *st = kmalloc(sizeof(struct stored_info));

	struct iovec iov;
	struct uio uio;
	uio_kinit(&iov, &uio, st, sizeof(struct stored_info), 0, UIO_READ);

	if (VOP_READ(bs, &uio) != 0)
		panic("failed to read from disk");

	// Check if we have never written meta data to disk before
	if(st->magic_start != META_DATA_MAGIC || st->magic_end != META_DATA_MAGIC){
		kfree(st);

		// Initialize values to first time
		log_info->head = sizeof(struct stored_info);
		log_info->tail = log_info->head;
		log_info->last_id = 1;

		return -1;
	}

	log_info->head = st->head;
	log_info->last_id = st->last_id;
	log_info->tail = st->tail;
	log_info->len = st->len;

	kfree(st);

	return 0;
}


int recover(){
	// Read meta data from byte 0 checking if there is no data there
	if(pull_meta_data(&log_info) != 0){
		// TODO fix this
		// Zero disk to claim space for log and meta data (this loop will take a while but it is only for 1st time setup)
		for(unsigned i = 0; i < (DISK_LOG_SIZE + sizeof(struct stored_info)); i++){
			struct iovec iov;
			struct uio uio;
			unsigned filler = 0;
			uio_kinit(&iov, &uio, &filler, sizeof(unsigned), i*sizeof(unsigned), UIO_WRITE);
			if (VOP_WRITE(bs, &uio) != 0)
				return -1;
		}

		// Add checkpoint to ensure we don't redo this if we crash
		lock_acquire(log_info.lock);
		checkpoint();
		lock_release(log_info.lock);

		// This is the first time we have accessed the disk so skip recovery and return
		return 0;
	}

// TODO do recovery
//	switch(op){
//
//	case CHECKPOINT:
//	case ABORT:
//	case COMMIT:
//	case ADD_DIRENTRY:
//	case MODIFY_DIRENTRY_SIZE:
//	case MODIFY_DIRENTRY:
//	case RENAME_DIRENTRY:
//	case REMOVE_DIRENTRY:
//	case ALLOC_INODE:
//	case FREE_INODE:
//	default:
//		panic("Undefined log entry code\n");
//		break;
//	}

	lock_acquire(log_info.lock);
	checkpoint();
	lock_release(log_info.lock);
	return 0;
}


// Switches buffer and returns ptr to inactive buffer, blocking if the buffer is being flushed
static struct log_buffer* switch_buffer(){
	log_info.page_count++;
	if(log_info.active_buffer == buf1){
		lock_acquire(buf2->lock);
		KASSERT(buf2->buffer_filled == 0);
		log_info.active_buffer = buf2;
		lock_release(buf2->lock);
		return buf1;
	}else{
		lock_acquire(buf1->lock);
		KASSERT(buf1->buffer_filled == 0);
		log_info.active_buffer = buf1;
		lock_release(buf1->lock);
		return buf2;
	}
}

// TODO fix this
// Flushes all the meta data to disk
static int flush_meta_data_to_disk(struct log_info *info){

	struct stored_info cpy;

	cpy.magic_start =  META_DATA_MAGIC;
	cpy.head = info->head;
	cpy.last_id = info->last_id;
	cpy.tail = info->tail;
	cpy.len = info->len;
	cpy.magic_end =  META_DATA_MAGIC;

	// Writes out all metadata to disk
	struct iovec iov;
	struct uio uio;

	uio_kinit(&iov, &uio, &cpy, sizeof(struct stored_info), 0, UIO_WRITE);

	if (VOP_WRITE(bs, &uio) != 0)
		return -1;

	return 0;
}

// TODO fix this
// Flushes buffer, if there is anything to flush, to disk at this point the len is up to date
static int flush_log_to_disk(struct log_buffer *buf, struct log_info *info){
	kprintf("Writing log to disk\n");

	// Lock the buffer so it can't be switched to active during the flush
	lock_acquire(buf->lock);

	if(buf->buffer_filled == 0){
		lock_release(buf->lock);
		return 0;
	}

	unsigned remainder = DISK_LOG_SIZE - info->head;

	// Flush out to disk
	// TODO is this math correct
	if(remainder >= buf->buffer_filled){

		struct iovec iov;
		struct uio uio;

		uio_kinit(&iov, &uio, buf->buffer, buf->buffer_filled, info->head, UIO_WRITE);

		if (VOP_WRITE(bs, &uio) != 0)
			goto out;
	}
	// In this case we split to 2 seperate writes to wrap around the buffer
	else{
		struct iovec iov1;
		struct uio uio1;

		uio_kinit(&iov1, &uio1, buf->buffer, remainder, info->head, UIO_WRITE);

		if (VOP_WRITE(bs, &uio1) != 0)
			goto out;

		struct iovec iov2;
		struct uio uio2;

		// Start at the offset after the stored info
		uio_kinit(&iov2, &uio2, buf->buffer+remainder, buf->buffer_filled-remainder, sizeof(struct log_info), UIO_WRITE);

		if (VOP_WRITE(bs, &uio2) != 0)
			goto out;
	}

	// Move the head forward (length is already tracked and updated)
	info->head = (info->head + buf->buffer_filled) % DISK_LOG_SIZE;

	// Flush the meta data associated with the log to disk to ensure consistent state
	if(flush_meta_data_to_disk(info) != 0)
		goto out;

	buf->buffer_filled = 0;
	lock_release(buf->lock);

	return 0;

out:
	lock_release(buf->lock);
	return -1;
}

// TODO flush buffer cache to disk
static int flush_buffer_cache_to_disk(){
//	sync_fs_buffers();
	return 0;
}

// Flushes current buffer after adding a checkpoint, blocking all writes to the log buffer and hence the cache
// TODO check this logic
int checkpoint(){
	kprintf("Checkpointing\n");
	KASSERT(lock_do_i_hold(log_info.lock));

	flush_log_to_disk(log_info.active_buffer, &log_info);

	flush_buffer_cache_to_disk();

	// Create a new checkpoint entry
	struct checkpoint ch;
	if(log_info.earliest_transaction)
		ch.new_tail = log_info.head;
	else
		ch.new_tail = log_info.earliest_transaction;

	// Updates the meta data of the log
	log_info.tail = ch.new_tail;

	// TODO is this math correct?
	log_info.len = 0;
	for(unsigned ptr = log_info.tail; ptr != log_info.head;){
		ptr =  (ptr + 1) % DISK_LOG_SIZE;
		log_info.len++;
	}

	log_write(CHECKPOINT, sizeof(struct checkpoint), (char *)&ch);

	flush_log_to_disk(log_info.active_buffer, &log_info);

	log_info.page_count = 0;

	return 0;
}


uint64_t log_write(enum operation op, uint16_t size, void *operation_struct){
	// Lock to ensure that active never changes
	lock_acquire(log_info.lock);

	// Check if we blow the buffer
	if(sizeof(struct record_header) + size + log_info.active_buffer->buffer_filled >= LOG_BUFFER_SIZE){
		struct log_buffer *buf = switch_buffer();

		// Copy current state of meta data as it will be updated during flushing
		struct log_info *cpy = kmalloc(sizeof(struct log_info));
		memcpy(cpy, &log_info, sizeof(struct log_info));

		// TODO thread fork this to allow for non-quiesence
		// TODO will forking here mess up the locks?
		if(flush_log_to_disk(buf, cpy) != 0) goto out;

		// TODO Clean up the copied structure after thread fork is done
		kfree(cpy);
	}

	// Augment len and check if we will blow the log, if the op is a checkpoint, then don't infinitely recurse
	if(size+sizeof(struct record_header)+log_info.len > DISK_LOG_SIZE - MARGIN){
		if(op == CHECKPOINT){
			// check that we don't run the head into the tail (in case we can't clear enough memory with checkpointing)
			KASSERT(size+sizeof(struct record_header)+log_info.len < DISK_LOG_SIZE);
		}else{
			// TODO checkpoint and if it fails goto out, should we thread fork?
			if(checkpoint() != 0) goto out;
			// Ensure that there is space after checkpoint
			KASSERT(size+sizeof(struct record_header)+log_info.len <= DISK_LOG_SIZE - MARGIN);
		}
	}

	struct record_header header;
	header.size = size;
	header.op = op;
	header.record_id = log_info.last_id++;

	// Copy onto the buffer
	// TODO is this pointer math right?
	memcpy(&log_info.active_buffer->buffer[log_info.active_buffer->buffer_filled], &header, sizeof(struct record_header));
	log_info.active_buffer->buffer_filled += sizeof(struct record_header);
	memcpy(&log_info.active_buffer->buffer[log_info.active_buffer->buffer_filled], operation_struct, size);
	log_info.active_buffer->buffer_filled += size;

	// Augment the log info data but don't augment the head till its flushed, so we can know where to flush to
	log_info.len += size+sizeof(struct record_header);

	// TODO update log_info.earliest_transaction
    // TODO need to check
    struct commit *oper = operation_struct;
    if (op == COMMIT) { // remove it from the queue
        int index = vector_find(&tvector, oper->transaction_id);
        KASSERT (index != -1);  // if it's a commit, the transaction must be in the vector
        vector_set(&tvector, index, 0);  // invalidate
        // TODO: earliest_transaction should get offset instead of trascation_id
        log_info.earliest_transaction = vector_get_min(&tvector);  // get the next mininun offset
    } else  // new transaction or another operation of the yet uncommited transaction
        // if it's the first == not in the list
        // info->head = (info->head + buf->buffer_filled) % DISK_LOG_SIZE;
        vector_insert(&tvector, oper->transaction_id);

	lock_release(log_info.lock);

	return header.record_id;

out:
	lock_release(log_info.lock);
	return 0;
}

