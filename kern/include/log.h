#ifndef _H_LOG_H_
#define _H_LOG_H_

#include <synch.h>
#include <limits.h>

#define LOG_BUFFER_SIZE 4096
#define DISK_LOG_SIZE (512*512)
#define MARGIN ((512*512)/10)
#define META_DATA_MAGIC 0xB16B00B5

#define UNDO 1
#define REDO 2

struct log_buffer{
	struct lock *lock; // lock to ensure mutual exclusion during flushing
	unsigned buffer_filled; // bytes of the buffer filled
	char buffer [LOG_BUFFER_SIZE]; // bytes array
};



struct log_info{
	struct lock *lock; // lock to ensure up to data meta data
	struct log_buffer *active_buffer; // the active buffer of the two that we switch between
	struct fs *fs;
	// These are only updated before flushing to disk
	unsigned head; // byte index of the head
	unsigned tail; // byte index of the tail
	// These are constantly updated in memory
	unsigned len; // len in bytes of the on disk + in memory log
	uint16_t page_count; // pages stored on disk without checkpointing
	uint64_t last_id; // id of last entry written
	unsigned earliest_transaction; // index in bytes of first entry of uncommitted transaction farthest in the past
}log_info;

struct stored_info{
	unsigned magic_start;
	unsigned head; // byte index of the head
	unsigned tail; // byte index of the tail
	unsigned len; // len in bytes of the on disk + in memory log
	uint64_t last_id; // id of last entry written
	unsigned magic_end;
};

// Macro to define where meta data ends and log begins
//#define LOG_START (sizeof(struct log_info));

enum operation{
	CHECKPOINT = 1,
	ABORT,
	COMMIT,
	ADD_DIRENTRY,
	MODIFY_SIZE,
    MODIFY_LINKCOUNT,
	REMOVE_DIRENTRY,
	ALLOC_INODE,

	RENAME_DIRENTRY,
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
	uint64_t record_id; // Size of the structure after this. Not including the header
	uint16_t size;
	uint16_t op;    /* for partability; enums are just ints */
	uint64_t transaction_id;
};

struct checkpoint{
	// offset in bytes into the array where the new tail is
	unsigned new_tail;
};

struct add_direntry{
    uint32_t inode_type;
	unsigned inode_id;
	unsigned target_inode_id;
	char name[NAME_MAX];
};

struct modify_linkcount {   /* for for files and dirs */
    unsigned inode_id;
	unsigned old_linkcount;
	unsigned new_linkcount;
};

struct modify_size{
	unsigned inode_id;
	uint32_t old_len;
	uint32_t new_len;
};

struct rename_direntry{
	unsigned dir_inode_id;
	unsigned target_inode_id;
	char old_name[NAME_MAX];
	char new_name[NAME_MAX];
};

struct remove_direntry{
    int slot;
	unsigned dir_inode_id;
    unsigned victim_inode;
    char victim_name[NAME_MAX];
};

struct alloc_inode{
	unsigned inode_id;
	uint32_t type;  /* enum object_type */
};

struct free_inode{
	unsigned inode_id;
};

int log_buffer_bootstrap(void);
int recover(void);
uint64_t log_write(enum operation op, uint16_t size, void *operation_struct, uint64_t txn_id);
uint64_t safe_log_write(enum operation op, uint16_t size, void *operation_struct, uint64_t txn_id);
int checkpoint(void);
int test_read_write(int nargs, char **args);

#endif
