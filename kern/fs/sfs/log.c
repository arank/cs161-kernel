#include <types.h>
#include <current.h>
#include <lib.h>
#include <log.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/errno.h>
#include <kern/fcntl.h>

static struct log_buffer *buf1, *buf2;
static struct vnode *bs;

// Opens the disk object, and reads the data from it into the log_info object
// this should be called before recovery
int disk_log_bootstrap(){
	if(vfs_open(kstrdup("lhd1raw:"), O_RDWR, 0, &bs) != 0)
		panic ("vfs_open failed\n");

	// TODO define log region and meta data region

	// TODO read data into log_info (either here or in recover)

	return 0;
}

int recover(){
	// TODO checkpoint at the end
	return 0;
}

// Creates the log buffer global object and log info global object
int log_buffer_bootstrap(){
	// Head, tail and last_id will be set during pulling data from disk
	buf1 = kmalloc(sizeof(struct log_buffer));
	if(buf1 == NULL) goto out;

	buf1->lock = lock_create("buffer lock");
	if (buf1->lock == NULL) goto out;

	buf2 = kmalloc(sizeof(struct log_buffer));
	if(buf2 == NULL) goto out;

	buf2->lock = lock_create("buffer lock");
	if (buf2->lock == NULL) goto out;

	log_info.lock = lock_create("log info lock");
	if (log_info.lock == NULL) goto out;

	// Auto set to buf1
	log_info.active_buffer = buf1;

	return 0;

out:
	panic("Failed to create the log\n");
	return ENOMEM;
}

// Switches buffer and returns ptr to inactive buffer
static struct log_buffer* switch_buffer(){
	log_info.page_count++;
	if(log_info.active_buffer == buf1){
		KASSERT(buf2->buffer_filled == 0);
		log_info.active_buffer = buf2;
		return buf1;
	}else{
		KASSERT(buf1->buffer_filled == 0);
		log_info.active_buffer = buf1;
		return buf2;
	}
}

// Flushes all the meta data to disk
static void flush_meta_data_to_disk(){
	KASSERT(lock_do_i_hold(log_info.lock));

	// TODO write out meta data to disk
}

// Flushes buffer, if there is anything to flush, to disk at this point the len is up to date
static int flush_log_to_disk(struct log_buffer *buf){
	kprintf("Writing log to disk\n");
	KASSERT(lock_do_i_hold(log_info.lock));

	// move the head forward
	log_info.head = (log_info.head + buf->buffer_filled) % DISK_LOG_SIZE;

	// TODO write out log to disk, in circular fashion

	flush_meta_data_to_disk();

	return 0;
}

// Flushes current buffer after adding a checkpoint, blocking all writes to the log buffer and hence the cache
// TODO check this logic
int checkpoint(){
	kprintf("Checkpointing\n");
	KASSERT(lock_do_i_hold(log_info.lock));
	flush_log_to_disk(log_info.active_buffer);

	// TODO flush buffer cache to disk

	struct checkpoint ch;
	ch.new_tail = log_info.earliest_transaction;
	log_write(CHECKPOINT, sizeof(struct checkpoint), (char *)&ch);

	flush_log_to_disk(log_info.active_buffer);

	return 0;
}


uint64_t log_write(enum operation op, uint16_t size, char *operation_struct){
	// Lock to ensure that active never changes
	lock_acquire(log_info.lock);

	// Check if we blow the buffer
	if(sizeof(struct record_header) + size + log_info.active_buffer->buffer_filled >= LOG_BUFFER_SIZE){
		struct log_buffer *buf = switch_buffer();
		// TODO thread fork this to allow for non-quiesence
		// TODO will forking here mess up the locks?
		if(flush_log_to_disk(buf) != 0) goto out;
	}

	// TODO ensure if buffer is full then flushed, and the disk_log is full and needs to be checkpointed, behavior is well defined
	// Augment len and check if we will blow the log, if the op is a checkpoint, then don't infinitely recurse
	if(size+sizeof(struct record_header)+log_info.len > DISK_LOG_SIZE - MARGIN && op != CHECKPOINT){
		// TODO checkpoint and if it fails goto out, should we thread fork?
		if(checkpoint() != 0) goto out;
		// Ensure that there is space after checkpoint
		KASSERT(size+sizeof(struct record_header)+log_info.len <= DISK_LOG_SIZE - MARGIN);
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

	// TODO update log_info.earliest transaction

	lock_release(log_info.lock);

	return header.record_id;

out:
	lock_release(log_info.lock);
	return 0;
}


