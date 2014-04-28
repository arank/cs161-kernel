#ifndef _H_LOG_H_
#define _H_LOG_H_

#include <synch.h>
#include <limits.h>

#define LOG_BUFFER_SIZE 4096
#define DISK_LOG_SIZE 524288
#define MARGIN 8192

// TODO possibly have two of these?
struct log_buffer{
	struct lock *lock;
	unsigned buffer_filled;
	char buffer [LOG_BUFFER_SIZE];
};

struct log_info{
	struct lock *lock;
	struct log_buffer *active_buffer;
	unsigned head;
	unsigned tail;
	unsigned len;
	uint16_t page_count;
	uint64_t last_id;
	uint64_t earliest_transaction;
}log_info;

enum operation{
	CHECKPOINT = 1,
	ABORT,
	COMMIT,
	ADD_DIRENTRY,
	MODIFY_DIRENTRY_SIZE,
	MODIFY_DIRENTRY,
	RENAME_DIRENTRY,
	REMOVE_DIRENTRY,
	ALLOC_INODE,
	FREE_INODE,
	// TODO removed truncate as we can log a bunch of free's instead of creating a completely new structure.
};

enum object_type{
	FILE = 1,
	DIR,
    INDIRECTION,
    USERBLOCK
};

struct record_header{
	uint64_t record_id; // Size of the structure after this. Not including the header
	uint16_t size;
	uint16_t op;    /* for partability; enums are just ints */
};

struct checkpoint{
	// record_id of new tail
	uint64_t new_tail;
};

struct commit{
	// unique for each transaction, based on the id of the first action in the transaction
	uint64_t transaction_id;
};

struct add_direntry{
	uint64_t transaction_id;
	unsigned dir_inode_id;
	unsigned target_inode_id;
	unsigned old_link_count;
	unsigned new_link_count;
	char name[NAME_MAX];
};

struct modify_direntry_size{
	uint64_t transaction_id;
	unsigned inode_id;
	uint32_t old_len;
	uint32_t new_len;
};

struct modify_direntry{
	uint64_t transaction_id;
	// TODO don't I need 2 inode id's?
	unsigned inode_id;
	unsigned new_inode_value;
	unsigned old_inode_value;
	unsigned inode1_old_link_count;
	unsigned inode1_new_link_count;
	unsigned inode2_old_link_count;
	unsigned inode2_new_link_count;
	char name[NAME_MAX];
};

struct rename_direntry{
	uint64_t transaction_id;
	unsigned dir_inode_id;
	unsigned target_inode_id;
	char old_name[NAME_MAX];
	char new_name[NAME_MAX];
};

struct remove_direntry{
	uint64_t transaction_id;
	unsigned dir_inode_id;
	unsigned target_inode_id;
	unsigned old_link_count;
	unsigned new_link_count;
	char name[NAME_MAX];
};

struct alloc_inode{
	uint64_t transaction_id;
	unsigned inode_id;
	uint32_t type;  /* enum object_type */
};

struct free_inode{
	uint64_t transaction_id;
	unsigned inode_id;
};

int log_buffer_bootstrap(void);
int disk_log_bootstrap(void);
int recover(void);
uint64_t log_write(enum operation op, uint16_t size, void *operation_struct);
int checkpoint(void);

#endif
