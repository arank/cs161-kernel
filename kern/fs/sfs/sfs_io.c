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
 * I/O plumbing.
 */
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <uio.h>
#include <synch.h>
#include <vfs.h>
#include <buf.h>
#include <device.h>
#include <sfs.h>
#include "sfsprivate.h"
#include <log.h>

////////////////////////////////////////////////////////////
//
// Basic block-level I/O routines

/*
 * Note: sfs_readblock is used to read the superblock
 * early in mount, before sfs is fully (or even mostly)
 * initialized, and so may not use anything from sfs
 * except sfs_device.
 */

/*
 * Read or write a block, retrying I/O errors.
 */
static
int
sfs_rwblock(struct sfs_fs *sfs, struct uio *uio)
{
	int result;
	int tries=0;

	DEBUG(DB_SFS, "sfs: %s %llu\n",
	      uio->uio_rw == UIO_READ ? "read" : "write",
	      uio->uio_offset / SFS_BLOCKSIZE);

 retry:
	result = DEVOP_IO(sfs->sfs_device, uio);
	if (result == EINVAL) {
		/*
		 * This means the sector we requested was out of range,
		 * or the seek address we gave wasn't sector-aligned,
		 * or a couple of other things that are our fault.
		 */
		panic("sfs: d_io returned EINVAL\n");
	}
	if (result == EIO) {
		if (tries == 0) {
			tries++;
			kprintf("sfs: block %llu I/O error, retrying\n",
				uio->uio_offset / SFS_BLOCKSIZE);
			goto retry;
		}
		else if (tries < 10) {
			tries++;
			goto retry;
		}
		else {
			kprintf("sfs: block %llu I/O error, giving up after "
				"%d retries\n",
				uio->uio_offset / SFS_BLOCKSIZE, tries);
		}
	}
	return result;
}

/*
 * Read a block.
 */
int
sfs_readblock(struct fs *fs, daddr_t block, void *data, size_t len)
{
	struct sfs_fs *sfs = fs->fs_data;
	struct iovec iov;
	struct uio ku;

	KASSERT(len == SFS_BLOCKSIZE);

	SFSUIO(&iov, &ku, data, block, UIO_READ);
	return sfs_rwblock(sfs, &ku);
}

/*
 * Write a block.
 */
int
sfs_writeblock(struct fs *fs, daddr_t block, void *data, size_t len)
{
	struct sfs_fs *sfs = fs->fs_data;
	struct iovec iov;
	struct uio ku;

	KASSERT(len == SFS_BLOCKSIZE);

	SFSUIO(&iov, &ku, data, block, UIO_WRITE);
	return sfs_rwblock(sfs, &ku);
}

////////////////////////////////////////////////////////////
//
// File-level I/O

/*
 * Do I/O to a block of a file that doesn't cover the whole block.  We
 * need to read in the original block first, even if we're writing, so
 * we don't clobber the portion of the block we're not intending to
 * write over.
 *
 * SKIPSTART is the number of bytes to skip past at the beginning of
 * the sector; LEN is the number of bytes to actually read or write.
 * UIO is the area to do the I/O into.
 *
 * Requires up to 2 buffers.
 */
static
int
sfs_partialio(struct sfs_vnode *sv, struct uio *uio,
	      uint32_t skipstart, uint32_t len)
{
	struct sfs_fs *sfs = sv->sv_v.vn_fs->fs_data;
	struct buf *iobuffer;
	char *ioptr;
	daddr_t diskblock;
	uint32_t fileblock;
	int result;

	/* Allocate missing blocks if and only if we're writing */
	bool doalloc = (uio->uio_rw==UIO_WRITE);

	KASSERT(lock_do_i_hold(sv->sv_lock));
	KASSERT(skipstart + len <= SFS_BLOCKSIZE);

	/* Compute the block offset of this block in the file */
	fileblock = uio->uio_offset / SFS_BLOCKSIZE;

	/* Get the disk block number */
	result = sfs_bmap(sv, fileblock, doalloc, &diskblock);
	if (result) {
		return result;
	}

	if (diskblock == 0) {
		/*
		 * There was no block mapped at this point in the file.
		 *
		 * We must be reading, or sfs_bmap would have
		 * allocated a block for us.
		 */
		KASSERT(uio->uio_rw == UIO_READ);
		return uiomovezeros(len, uio);
	}
	else {
		/*
		 * Read the block.
		 */
		result = buffer_read(&sfs->sfs_absfs, diskblock, SFS_BLOCKSIZE,
				     &iobuffer);
		if (result) {
			return result;
		}
	}

	/*
	 * Now perform the requested operation into/out of the buffer.
	 */
	ioptr = buffer_map(iobuffer);
	result = uiomove(ioptr+skipstart, len, uio);
	if (result) {
		buffer_release(iobuffer);
		return result;
	}

	/*
	 * If it was a write, mark the modified block dirty.
	 */
	if (uio->uio_rw == UIO_WRITE) {
		buffer_mark_dirty(iobuffer);
	}

	buffer_release(iobuffer);
	return 0;
}

/*
 * Do I/O (either read or write) of a single whole block.
 *
 * Locking: must hold vnode lock. May get/release sfs_bitlock.
 * 
 * Requires up to 2 buffers.
 */
static
int
sfs_blockio(struct sfs_vnode *sv, struct uio *uio)
{
	struct sfs_fs *sfs = sv->sv_v.vn_fs->fs_data;
	struct buf *iobuf;
	void *ioptr;
	daddr_t diskblock;
	uint32_t fileblock;
	int result;
	bool doalloc = (uio->uio_rw==UIO_WRITE);

	KASSERT(lock_do_i_hold(sv->sv_lock));

	/* Get the block number within the file */
	fileblock = uio->uio_offset / SFS_BLOCKSIZE;

	/* Look up the disk block number */
	result = sfs_bmap(sv, fileblock, doalloc, &diskblock);
	if (result) {
		return result;
	}

	if (diskblock == 0) {
		/*
		 * No block - fill with zeros.
		 *
		 * We must be reading, or sfs_bmap would have
		 * allocated a block for us.
		 */
		KASSERT(uio->uio_rw == UIO_READ);
		return uiomovezeros(SFS_BLOCKSIZE, uio);
	}

	if (uio->uio_rw == UIO_READ) {
		result = buffer_read(&sfs->sfs_absfs, diskblock, SFS_BLOCKSIZE,
				     &iobuf);
	}
	else {
		result = buffer_get(&sfs->sfs_absfs, diskblock, SFS_BLOCKSIZE,
				    &iobuf);
	}
	if (result) {
		return result;
	}

	/*
	 * Do the I/O into the buffer.
	 */
	ioptr = buffer_map(iobuf);
	result = uiomove(ioptr, SFS_BLOCKSIZE, uio);
	if (result) {
		buffer_release(iobuf);
		return result;
	}

	if (uio->uio_rw == UIO_WRITE) {
		buffer_mark_valid(iobuf);
		buffer_mark_dirty(iobuf);
	}

	buffer_release(iobuf);
	return 0;
}

/*
 * Do I/O of a whole region of data, whether or not it's block-aligned.
 *
 * Locking: must hold vnode lock. May get/release sfs_bitlock.
 * 
 * Requires up to 3 buffers.
 */
int
sfs_io(struct sfs_vnode *sv, struct uio *uio)
{
	uint32_t blkoff;
	uint32_t nblocks, i;
	int result = 0;
	uint32_t extraresid = 0;
	struct sfs_dinode *inodeptr;
    uint64_t old_size;

    KASSERT(lock_do_i_hold(sv->sv_lock));

	result = sfs_dinode_load(sv);
	if (result) {
		return result;
	}
	inodeptr = sfs_dinode_map(sv);

	struct modify_size op1;
	for (int i = 0; i < SFS_NDIRECT; i++)
		op1.old_sfi_direct[i] = inodeptr->sfi_direct[i];

	op1.old_sfi_indirect = inodeptr->sfi_indirect;
	op1.old_sfi_dindirect = inodeptr->sfi_dindirect;
	op1.old_sfi_tindirect = inodeptr->sfi_tindirect;


	/*
	 * If reading, check for EOF. If we can read a partial area,
	 * remember how much extra there was in EXTRARESID so we can
	 * add it back to uio_resid at the end.
	 */
	if (uio->uio_rw == UIO_READ) {
		off_t size, endpos;

		size = inodeptr->sfi_size;
		endpos = uio->uio_offset + uio->uio_resid;

		if (uio->uio_offset >= size) {
			/* At or past EOF - just return */
			sfs_dinode_unload(sv);
			return 0;
		}

		if (endpos > size) {
			extraresid = endpos - size;
			KASSERT(uio->uio_resid > extraresid);
			uio->uio_resid -= extraresid;
		}
	}

	/*
	 * First, do any leading partial block.
	 */
	blkoff = uio->uio_offset % SFS_BLOCKSIZE;
	if (blkoff != 0) {
		/* Number of bytes at beginning of block to skip */
		uint32_t skip = blkoff;

		/* Number of bytes to read/write after that point */
		uint32_t len = SFS_BLOCKSIZE - blkoff;

		/* ...which might be less than the rest of the block */
		if (len > uio->uio_resid) {
			len = uio->uio_resid;
		}

		/* Call sfs_partialio() to do it. */
		result = sfs_partialio(sv, uio, skip, len);
		if (result) {
			goto out;
		}
	}

	/* If we're done, quit. */
	if (uio->uio_resid==0) {
		goto out;
	}

	/*
	 * Now we should be block-aligned. Do the remaining whole blocks.
	 */
	KASSERT(uio->uio_offset % SFS_BLOCKSIZE == 0);
	nblocks = uio->uio_resid / SFS_BLOCKSIZE;
	for (i=0; i<nblocks; i++) {
		result = sfs_blockio(sv, uio);
		if (result) {
			goto out;
		}
	}

	/*
	 * Now do any remaining partial block at the end.
	 */
	KASSERT(uio->uio_resid < SFS_BLOCKSIZE);

	if (uio->uio_resid > 0) {
		result = sfs_partialio(sv, uio, 0, uio->uio_resid);
		if (result) {
			goto out;
		}
	}

 out:

    old_size = inodeptr->sfi_size;
	/* If writing, adjust file length */
	if (uio->uio_rw == UIO_WRITE &&
	    uio->uio_offset > (off_t)inodeptr->sfi_size) {
		inodeptr->sfi_size = uio->uio_offset;

        for (int i = 0; i < SFS_NDIRECT; i++)
            op1.new_sfi_direct[i] = inodeptr->sfi_direct[i];

        op1.new_sfi_indirect = inodeptr->sfi_indirect;
        op1.new_sfi_dindirect = inodeptr->sfi_dindirect;
        op1.new_sfi_tindirect = inodeptr->sfi_tindirect;

        op1.inode_id = sv->sv_ino;
        op1.old_len = old_size;
        op1.new_len = inodeptr->sfi_size;

        uint64_t tr_id = safe_log_write(MODIFY_SIZE, sizeof (struct modify_size), &op1, 0);

        safe_log_write(COMMIT, 0, NULL, tr_id);

		sfs_dinode_mark_dirty(sv);
	}
	sfs_dinode_unload(sv);

	/* Add in any extra amount we couldn't read because of EOF */
	uio->uio_resid += extraresid;

	/* Done */
	return result;
}
