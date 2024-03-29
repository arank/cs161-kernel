/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2014
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * SFS filesystem
 *
 * Directory I/O
 */
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <uio.h>
#include <synch.h>
#include <buf.h>
#include <sfs.h>
#include "sfsprivate.h"

/*
 * Read the directory entry out of slot SLOT of a directory vnode.
 * The "slot" is the index of the directory entry, starting at 0.
 *
 * Locking: Must hold the vnode lock. May get/release sfs_bitlock.
 *
 * Requires up to 3 buffers.
 */
int
sfs_readdir(struct sfs_vnode *sv, int slot, struct sfs_dir *sd)
{
	struct iovec iov;
	struct uio ku;
	off_t actualpos;
	int result;

	KASSERT(lock_do_i_hold(sv->sv_lock));

	/* Compute the actual position in the directory to read. */
	actualpos = slot * sizeof(struct sfs_dir);

	/* Set up a uio to do the read */
	uio_kinit(&iov, &ku, sd, sizeof(struct sfs_dir), actualpos, UIO_READ);

	/* do it */
	result = sfs_io(sv, &ku);
	if (result) {
		return result;
	}

	/* We should not hit EOF in the middle of a directory entry */
	if (ku.uio_resid > 0) {
		panic("sfs: readdir: Short entry (inode %u)\n", sv->sv_ino);
	}

	/* Done */
	return 0;
}

/*
 * Write (overwrite) the directory entry in slot SLOT of a directory
 * vnode.
 *
 * Locking: must hold vnode lock.
 * 
 * Requires up to 3 buffers.
 */
int
sfs_writedir(struct sfs_vnode *sv, int slot, struct sfs_dir *sd)
{
	struct iovec iov;
	struct uio ku;
	off_t actualpos;
	int result;

	/* Compute the actual position in the directory. */

	KASSERT(lock_do_i_hold(sv->sv_lock));
	KASSERT(slot>=0);
	actualpos = slot * sizeof(struct sfs_dir);

	/* Set up a uio to do the write */
	uio_kinit(&iov, &ku, sd, sizeof(struct sfs_dir), actualpos, UIO_WRITE);

	/* do it */
	result = sfs_io(sv, &ku);
	if (result) {
		return result;
	}

	/* Should not end up with a partial entry! */
	if (ku.uio_resid > 0) {
		panic("sfs: writedir: Short write (ino %u)\n", sv->sv_ino);
	}

	/* Done */
	return 0;
}

/*
 * Compute the number of entries in a directory.
 * This actually computes the number of existing slots, and does not
 * account for empty slots.
 *
 * Locking: must hold vnode lock.
 * 
 * Requires 1 buffer.
 */
int
sfs_dir_nentries(struct sfs_vnode *sv, int *ret)
{
	off_t size;
	struct sfs_dinode *inodeptr;
	int result;

	KASSERT(lock_do_i_hold(sv->sv_lock));
	KASSERT(sv->sv_type == SFS_TYPE_DIR);

	result = sfs_dinode_load(sv);
	if (result) {
		return result;
	}
	inodeptr = sfs_dinode_map(sv);

	size = inodeptr->sfi_size;
	if (size % sizeof(struct sfs_dir) != 0) {
		panic("sfs: directory %u: Invalid size %llu\n",
		      sv->sv_ino, size);
	}

	sfs_dinode_unload(sv);

	*ret = size / sizeof(struct sfs_dir);
	return 0;
}

/*
 * Search a directory for a particular filename in a directory, and
 * return its inode number, its slot, and/or the slot number of an
 * empty directory slot if one is found.
 *
 * Locking: must hold vnode lock. May get/release sfs_bitlock.
 *
 * Requires up to 3 buffers.
 */
int
sfs_dir_findname(struct sfs_vnode *sv, const char *name,
		uint32_t *ino, int *slot, int *emptyslot)
{
	struct sfs_dir tsd;
	int found, nentries, i, result;

	KASSERT(lock_do_i_hold(sv->sv_lock));

	result = sfs_dir_nentries(sv, &nentries);
	if (result) {
		return result;
	}

	/* For each slot... */
	found = 0;
	for (i=0; i<nentries; i++) {

		/* Read the entry from that slot */
		result = sfs_readdir(sv, i, &tsd);
		if (result) {
			return result;
		}
		if (tsd.sfd_ino == SFS_NOINO) {
			/* Free slot - report it back if one was requested */
			if (emptyslot != NULL) {
				*emptyslot = i;
			}
		}
		else {
			/* Ensure null termination, just in case */
			tsd.sfd_name[sizeof(tsd.sfd_name)-1] = 0;
			if (!strcmp(tsd.sfd_name, name)) {

				/* Each name may legally appear only once... */
				KASSERT(found==0);

				found = 1;
				if (slot != NULL) {
					*slot = i;
				}
				if (ino != NULL) {
					*ino = tsd.sfd_ino;
				}
			}
		}
	}

	return found ? 0 : ENOENT;
}

/*
 * Search a directory for a particular inode number in a directory, and
 * return the directory entry and/or its slot.
 *
 * Locking: requires vnode lock
 *
 * Requires up to 3 buffers
 */
int
sfs_dir_findino(struct sfs_vnode *sv, uint32_t ino,
		struct sfs_dir *retsd, int *slot)
{
	struct sfs_dir tsd;
	int found = 0;
	int nentries;
	int i, result;

	KASSERT(lock_do_i_hold(sv->sv_lock));

	result = sfs_dir_nentries(sv, &nentries);
	if (result) {
		return result;
	}

	/* For each slot... */
	for (i=0; i<nentries && !found; i++) {

		/* Read the entry from that slot */
		result = sfs_readdir(sv, i, &tsd);
		if (result) {
			return result;
		}
		if (tsd.sfd_ino == ino) {
			found = 1;
			if (slot != NULL) {
				*slot = i;
			}
			if (retsd != NULL) {
				/* Ensure null termination, just in case */
				tsd.sfd_name[sizeof(tsd.sfd_name)-1] = 0;
				*retsd = tsd;
			}
		}
	}

	return found ? 0 : ENOENT;
}

/*
 * Create a link in a directory to the specified inode by number, with
 * the specified name, and optionally hand back the slot.
 *
 * Locking: must hold vnode lock. May get/release sfs_bitlock.
 *
 * Requires up to 3 buffers.
 */
int
sfs_dir_link(struct sfs_vnode *sv, const char *name, uint32_t ino, int *slot)
{
	int emptyslot = -1;
	int result;
	struct sfs_dir sd;

	KASSERT(lock_do_i_hold(sv->sv_lock));

	/* Look up the name. We want to make sure it *doesn't* exist. */
	result = sfs_dir_findname(sv, name, NULL, NULL, &emptyslot);
	if (result!=0 && result!=ENOENT) {
		return result;
	}
	if (result==0) {
		return EEXIST;
	}

	if (strlen(name)+1 > sizeof(sd.sfd_name)) {
		return ENAMETOOLONG;
	}

	/* If we didn't get an empty slot, add the entry at the end. */
	if (emptyslot < 0) {
		result = sfs_dir_nentries(sv, &emptyslot);
		if (result) {
			return result;
		}
	}

	/* Set up the entry. */
	bzero(&sd, sizeof(sd));
	sd.sfd_ino = ino;
	strcpy(sd.sfd_name, name);

	/* Hand back the slot, if so requested. */
	if (slot) {
		*slot = emptyslot;
	}
    
	/* Write the entry. */
	return sfs_writedir(sv, emptyslot, &sd);
}

/*
 * Unlink a name in a directory, by slot number.
 *
 * Locking: must hold vnode lock. May get/release sfs_bitlock.
 *
 * Requires up to 3 buffers.
 */
int
sfs_dir_unlink(struct sfs_vnode *sv, int slot)
{
	struct sfs_dir sd;

	KASSERT(lock_do_i_hold(sv->sv_lock));

	/* Initialize a suitable directory entry... */
	bzero(&sd, sizeof(sd));
	sd.sfd_ino = SFS_NOINO;

	/* ... and write it */
	return sfs_writedir(sv, slot, &sd);
}

/*
 * Check if a directory is empty.
 *
 * Locking: must hold vnode lock.
 *
 * Returns the vnode with its inode unloaded.
 *
 * Locking: must hold vnode lock. May get/release sfs_bitlock.
 *    Also gets/releases sfs_vnlock.
 *    Returns the result vnode locked.
 *
 * Requires up to 3 buffers.
 */
int
sfs_dir_checkempty(struct sfs_vnode *sv)
{
	struct sfs_dir sd;
	int nentries;
	int i, result;

	KASSERT(lock_do_i_hold(sv->sv_lock));

	result = sfs_dir_nentries(sv, &nentries);
	if (result) {
		return result;
	}

	for (i=0; i<nentries; i++) {
		result = sfs_readdir(sv, i, &sd);
		if (result) {
			return result;
		}
		if (sd.sfd_ino == SFS_NOINO) {
			/* empty slot */
			continue;
		}

		/* Ensure null termination, just in case */
		sd.sfd_name[sizeof(sd.sfd_name)-1] = 0;

		if (!strcmp(sd.sfd_name, ".") || !strcmp(sd.sfd_name, "..")) {
			continue;
		}

		/* Non-empty slot containing other than . or .. -> not empty */
		return ENOTEMPTY;
	}

	return 0;
}

/*
 * Look for a name in a directory and hand back a vnode for the
 * file, if there is one.
 *
 * Locking: must hold vnode lock. May get/release sfs_bitlock.
 *    Also gets/releases sfs_vnlock.
 *
 * Requires up to 3 buffers.
 */
int
sfs_lookonce(struct sfs_vnode *sv, const char *name, struct sfs_vnode **ret,
		int *slot)
{
	struct sfs_fs *sfs = sv->sv_v.vn_fs->fs_data;
	uint32_t ino;
	int result, result2;
	int emptyslot = -1;

	KASSERT(lock_do_i_hold(sv->sv_lock));
	
	result = sfs_dir_findname(sv, name, &ino, slot, &emptyslot);
	if (result == ENOENT) {
		*ret = NULL;
		if (slot != NULL) {
			if (emptyslot < 0) {
				result2 = sfs_dir_nentries(sv, &emptyslot);
				if (result2) {
					return result2;
				}
			}
			*slot = emptyslot;
		}
		return result;
	}
	else if (result) {
		return result;
	}

	result = sfs_loadvnode(sfs, ino, SFS_TYPE_INVAL, ret);
	if (result) {
		return result;
	}

	return 0;
}

