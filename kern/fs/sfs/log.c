#include <types.h>
#include <current.h>
#include <lib.h>
#include <log.h>
#include <kern/errno.h>

static struct log_buffer *buf1, *buf2;

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

static void switch_buffer(){
	if(log_info.active_buffer == buf1){
		KASSERT(buf2->buffer_filled == 0);
		log_info.active_buffer = buf2;
	}else{
		KASSERT(buf1->buffer_filled == 0);
		log_info.active_buffer = buf1;
	}
}

uint64_t log_write(enum operation op, uint16_t size, char *operation_struct){
	// Lock to ensure that active never changes
	lock_acquire(log_info.lock);
	if(sizeof(struct record_header) + size + log_info.active_buffer->buffer_filled >= LOG_BUFFER_SIZE){
		switch_buffer();
		// TODO flush the old buffer and if it fails goto out
		goto out;
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

	lock_release(log_info.lock);

	return header.record_id;

out:
	lock_release(log_info.lock);
	return 0;
}


