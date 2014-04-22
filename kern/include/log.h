#include <synch.h>
#include <limits.h>

#define LOG_BUFFER_SIZE 4096

// TODO possibly have two of these?
struct log_buffer{
	char buffer [LOG_BUFFER_SIZE];
	struct lock *lock;
	unsigned head;
	unsigned tail;
	uint64_t last_id;
}log_buffer;

enum operation{
	CHECKPOINT = 1,
	ABORT,
	COMMIT,
	ADD_DIRENTRY,
	MODIFY_DIRENTRY,
	RENAME_DIRENTRY,
	REMOVE_DIRENTRY,
	ALLOC_INODE,
	FREE_INODE,
	DIRTY_INODE, // TODO do we need this? indicates that the inode has been written to and won't be 0'd
	// TODO removed truncate as we can log a bunch of free's instead of creating a completely new structure.
};

enum object_type{
	FILE = 1,
	DIR
};

struct record_header{
	uint64_t record_id;
	// Size of the structure after this. Not including the header
	uint16_t size;
	enum operation op;
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
	enum object_type type;
};

struct free_inode{
	uint64_t transaction_id;
	unsigned inode_id;
	uint32_t old_len;
	uint32_t new_len;
};

struct dirty_inode{
	uint64_t transaction_id;
	unsigned inode_id;
};

