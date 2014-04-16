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
 * Block mapping logic.
 */
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <vfs.h>
#include <buf.h>
#include <sfs.h>
#include "sfsprivate.h"

/*
 * Maximum block number that we can have in a file.
 */
static const uint32_t sfs_maxblock = 
	SFS_NDIRECT +
	SFS_NINDIRECT * SFS_DBPERIDB +
	SFS_NDINDIRECT * SFS_DBPERIDB * SFS_DBPERIDB +
	SFS_NTINDIRECT * SFS_DBPERIDB * SFS_DBPERIDB * SFS_DBPERIDB
;

/*
 * Find out the indirection level of a file block number; that is,
 * which block pointer in the inode one uses to get to it.
 *
 * FILEBLOCK is the file block number.
 *
 * INDIR_RET returns the indirection level.
 *
 * INDIRNUM_RET returns the index into the inode blocks at that
 * indirection level, e.g. for the 3rd direct block this would be 3.
 *
 * OFFSET_RET returns the block number offset within the tree
 * starting at the designated inode block pointer. For direct blocks
 * this will always be 0.
 *
 * This function has been written so it will continue to work even if
 * SFS_NINDIRECT, SFS_NDINDIRECT, and/or SFS_NTINDIRECT get changed
 * around, although if you do that much of the rest of the code will
 * still need attention.
 *
 * Fails with EFBIG if the requested offset is too large for the
 * filesystem.
 */
static
int
sfs_get_indirection(uint32_t fileblock, unsigned *indir_ret,
		    unsigned *indirnum_ret, uint32_t *offset_ret)
{
	static const struct {
		unsigned num;
		uint32_t blockseach;
	} info[4] = {
		{ SFS_NDIRECT,    1 },
		{ SFS_NINDIRECT,  SFS_DBPERIDB },
		{ SFS_NDINDIRECT, SFS_DBPERIDB * SFS_DBPERIDB },
		{ SFS_NTINDIRECT, SFS_DBPERIDB * SFS_DBPERIDB * SFS_DBPERIDB },
	};

	unsigned indir;
	uint32_t max;

	for (indir = 0; indir < 4; indir++) {
		max = info[indir].num * info[indir].blockseach;
		if (fileblock < max) {
			*indir_ret = indir;
			*indirnum_ret = fileblock / info[indir].blockseach;
			*offset_ret = fileblock % info[indir].blockseach;
			return 0;
		}
		fileblock -= max;
	}
	return EFBIG;
}


/*
 * Given a pointer to a block slot, return it, allocating a block
 * if necessary.
 */
static
int
sfs_bmap_get(struct sfs_fs *sfs, uint32_t *blockptr, bool *dirtyptr,
	     bool doalloc, daddr_t *diskblock_ret)
{
	daddr_t block;
	int result;

	/*
	 * Get the block number
	 */
	block = *blockptr;

	/*
	 * Do we need to allocate?
	 */
	if (block==0 && doalloc) {
		result = sfs_balloc(sfs, &block, NULL);
		if (result) {
			return result;
		}

		/* Remember what we allocated; mark storage dirty */
		*blockptr = block;
		*dirtyptr = true;
	}

	/*
	 * Hand back the block
	 */
	*diskblock_ret = block;
	return 0;
}

/*
 * Look up the disk block number in a subtree; that is, we've picked
 * one of the block pointers in the inode and we're now going to
 * look up in the tree it points to.
 *
 * BLOCKPTR is the address of the block pointer in the inode.
 * INDIR is its indirection level.
 * DIRTYPTR is set if we change the block pointer.
 *
 * FILEBLOCK is the block offset into the subtree.
 * DOALLOC is true if we're allocating blocks.
 *
 * DISKBLOCK_RET gets the resulting disk block number.
 */
static
int
sfs_bmap_subtree(struct sfs_fs *sfs,
		 uint32_t *blockptr, unsigned indir, bool *dirtyptr,
		 uint32_t fileblock, bool doalloc,
		 daddr_t *diskblock_ret)
{
	daddr_t block;
	struct buf *idbuf;
	uint32_t *iddata;
	bool idbuf_dirty;
	uint32_t idoff;
	uint32_t fileblocks_per_entry;
	int result;

	COMPILE_ASSERT(SFS_DBPERIDB * sizeof(iddata[0]) == SFS_BLOCKSIZE);

	/* Get the block that blockptr points to (maybe allocating) */
	result = sfs_bmap_get(sfs, blockptr, dirtyptr, doalloc,
			      &block);
	if (result) {
		return result;
	}

	while (indir > 0) {

		/* If nothing here, we're done */
		if (block == 0) {
			KASSERT(doalloc == false);
			*diskblock_ret = 0;
			return 0;
		}

		/* Read the indirect block */
		result = buffer_read(&sfs->sfs_absfs, block,
				     SFS_BLOCKSIZE, &idbuf);
		if (result) {
			return result;
		}
		iddata = buffer_map(idbuf);
		idbuf_dirty = false;

		/*
		 * Compute the index into the indirect block.
		 * Leave the remainder in fileblock for the next pass.
		 */
		switch (indir) {
		    case 3:
			fileblocks_per_entry = SFS_DBPERIDB * SFS_DBPERIDB;
			break;
		    case 2:
			fileblocks_per_entry = SFS_DBPERIDB;
			break;
		    case 1:
			fileblocks_per_entry = 1;
			break;
		    default:
			panic("sfs_bmap_subtree: invalid indirect level %u\n",
			      indir);
		}
		idoff = fileblock / fileblocks_per_entry;
		fileblock = fileblock % fileblocks_per_entry;

		blockptr = &iddata[idoff];
		indir--;

		/* Get the address of the next layer down (maybe allocating) */
		result = sfs_bmap_get(sfs, blockptr, &idbuf_dirty, doalloc,
				      &block);
		if (result) {
			buffer_release(idbuf);
			return result;
		}
		if (idbuf_dirty) {
			buffer_mark_dirty(idbuf);
		}
		buffer_release(idbuf);
	}
	*diskblock_ret = block;
	return 0;
}

/*
 * Look up the disk block number (from 0 up to the number of blocks on
 * the disk) given a file and the logical block number within that
 * file. If DOALLOC is set, and no such block exists, one will be
 * allocated.
 *
 * Locking: must hold vnode lock. May get/release buffer cache locks and (via
 *    sfs_balloc) sfs_bitlock.
 *
 * Requires up to 2 buffers.
 */
int
sfs_bmap(struct sfs_vnode *sv, uint32_t fileblock, bool doalloc,
		daddr_t *diskblock)
{
	struct sfs_fs *sfs = sv->sv_v.vn_fs->fs_data;
	struct sfs_dinode *inodeptr;
	bool inode_dirty;
	unsigned indir, indirnum;
	uint32_t *blockptr;
	int result;

	KASSERT(lock_do_i_hold(sv->sv_lock));

	/* Figure out where to start */
	result = sfs_get_indirection(fileblock, &indir, &indirnum, &fileblock);
	if (result) {
		return result;
	}

	/* Load the inode */
	result = sfs_dinode_load(sv);
	if (result) {
		return result;
	}
	inodeptr = sfs_dinode_map(sv);

	/* Get the initial block pointer */
	switch (indir) {
	    case 0:
		KASSERT(fileblock == 0);
		blockptr = &inodeptr->sfi_direct[indirnum];
		break;
	    case 1:
		KASSERT(indirnum == 0);
		blockptr = &inodeptr->sfi_indirect;
		break;
	    case 2:
		KASSERT(indirnum == 0);
		blockptr = &inodeptr->sfi_dindirect;
		break;
	    case 3:
		KASSERT(indirnum == 0);
		blockptr = &inodeptr->sfi_tindirect;
		break;
	    default:
		panic("sfs_bmap: invalid indirection %u\n", indir);
	}

	/* Do the work in the indicated subtree */
	result = sfs_bmap_subtree(sfs,
				  blockptr, indir, &inode_dirty,
				  fileblock, doalloc,
				  diskblock);
	if (result) {
		sfs_dinode_unload(sv);
		return result;
	}

	if (inode_dirty) {
		sfs_dinode_mark_dirty(sv);
	}
	sfs_dinode_unload(sv);

	/* Hand back the result and return. */
	if (*diskblock != 0 && !sfs_bused(sfs, *diskblock)) {
		panic("sfs: Data block %u (block %u of file %u) "
		      "marked free\n",
		      *diskblock, fileblock, sv->sv_ino);
	}
	return 0;
}

/*
 * Do the work of truncating a file (or directory).
 *
 * Locking: must hold vnode lock. Acquires/releases buffer locks.
 * 
 * Requires up to 4 buffers.
 */
int
sfs_itrunc(struct sfs_vnode *sv, off_t len)
{
	struct sfs_fs *sfs = sv->sv_v.vn_fs->fs_data;

	/* Length in blocks (divide rounding up) */
	uint32_t blocklen = DIVROUNDUP(len, SFS_BLOCKSIZE);

	struct sfs_dinode *inodeptr;
	struct buf *idbuf, *didbuf, *tidbuf;
	uint32_t *iddata = NULL, *diddata = NULL, *tiddata = NULL ;
	uint32_t i;
	daddr_t block, idblock, didblock, tidblock;
	uint32_t baseblock, highblock;
	int result = 0, final_result = 0;
	int id_hasnonzero = 0, did_hasnonzero = 0, tid_hasnonzero = 0;

	COMPILE_ASSERT(SFS_DBPERIDB * sizeof(iddata[0]) == SFS_BLOCKSIZE);
	KASSERT(lock_do_i_hold(sv->sv_lock));

	result = sfs_dinode_load(sv);
	if (result) {
		return result;
	}
	inodeptr = sfs_dinode_map(sv);

	/*
	 * Go through the direct blocks. Discard any that are
	 * past the limit we're truncating to.
	 */
	for (i=0; i<SFS_NDIRECT; i++) {
		block = inodeptr->sfi_direct[i];
		if (i >= blocklen && block != 0) {
			sfs_bfree(sfs, block);
			inodeptr->sfi_direct[i] = 0;
		}
	}

	/* Indirect block number */
	idblock = inodeptr->sfi_indirect;

	/* Double indirect block number */
	didblock = inodeptr->sfi_dindirect;

	/* Triple indirect block number */
	tidblock = inodeptr->sfi_tindirect;

	/* The lowest block in the indirect block */
	baseblock = SFS_NDIRECT;

	/* The highest block in the file */
	highblock = baseblock + SFS_DBPERIDB + SFS_DBPERIDB* SFS_DBPERIDB +
			SFS_DBPERIDB* SFS_DBPERIDB* SFS_DBPERIDB - 1;


	/* We are going to cycle through all the blocks, changing levels
	 * of indirection. And free the ones that are past the new end
	 * of file.
	 */
	if (blocklen < highblock) {
		int indir = 1, level3 = 0, level2 = 0, level1 = 0;
		int id_modified = 0, did_modified = 0, tid_modified = 0;

		while (indir <= 3) {

			if (indir == 1) {
				baseblock = SFS_NDIRECT;
				if (idblock == 0) {
					indir++;
					continue;
				}
			}
			if (indir == 2) {
				baseblock = SFS_NDIRECT + SFS_DBPERIDB;
				if (didblock == 0) {
					indir++;
					continue;
				}
			}
			if (indir == 3) {
				baseblock = SFS_NDIRECT + SFS_DBPERIDB + 
					SFS_DBPERIDB * SFS_DBPERIDB;
				if (tidblock == 0) {
					indir++;
					continue;
				}
			}

			if (indir == 1) {
				/*
				 * If the level of indirection is 1,
				 * we are cycling through the blocks
				 * reachable from our indirect
				 * blocks. Read the indirect block
				 */

				// otherwise we would not be here
				KASSERT(idblock != 0);

				/* Read the indirect block */
				result = buffer_read(sv->sv_v.vn_fs, idblock,
						SFS_BLOCKSIZE, &idbuf);
				/* if there's an error, guess we just lose
				 * all the blocks referenced by this indirect
				 * block!
				 */
				if (result) {
					kprintf("sfs_dotruncate: error "
						"reading indirect block %u: "
						"%s\n",
						idblock, strerror(result));
					final_result = result;
					indir++;
					continue;
				}

				/*
				 * We do not need to execute the parts
				 * for double and triple levels of
				 * indirection.
				 */
				goto ilevel1;
			}
			if (indir == 2) {
				// otherwise we would not be here
				KASSERT(didblock != 0);

				/* Read the double indirect block */
				result = buffer_read(sv->sv_v.vn_fs, didblock,
						SFS_BLOCKSIZE, &didbuf);
				/*
				 * if there's an error, guess we just
				 * lose all the blocks referenced by
				 * this double-indirect block!
				 */
				if (result) {
					kprintf("sfs_dotruncate: error "
						"reading double indirect "
						"block %u: %s\n",
						didblock, strerror(result));
					final_result = result;
					indir++;
					continue;
				}

				/*
				 * We do not need to execute the parts
				 * for the triple level of indirection.
				 */
				goto ilevel2;
			}
			if (indir == 3) {

				// otherwise we would not be here
				KASSERT(tidblock != 0);

				/* Read the triple indirect block */
				result = buffer_read(sv->sv_v.vn_fs, tidblock,
						SFS_BLOCKSIZE, &tidbuf);
				/*
				 * if there's an error, guess we just
				 * lose all the blocks referenced by
				 * this triple-indirect block!
				 */
				if (result) {
					kprintf("sfs_dotruncate: error "
						"reading triple indirect "
						"block %u: %s\n",
						tidblock, strerror(result));
					final_result = result;
					indir++;
					continue;
				}

				goto ilevel3;
			}

			/*
			 * This is the loop for level of indirection 3
			 * Go through all double indirect blocks
			 * pointed to from this triple indirect block,
			 * discard the ones that are past the new end
			 * of file.
			 */

			ilevel3:
			tiddata = buffer_map(tidbuf);
			for (level3 = 0; level3 < SFS_DBPERIDB; level3++) {
				if (blocklen >= baseblock +
				    SFS_DBPERIDB * SFS_DBPERIDB * (level3)
				    || tiddata[level3] == 0) {
					if (tiddata[level3] != 0) {
						tid_hasnonzero = 1;
					}
					continue;
				}

				/*
				 * Read the double indirect block,
				 * hand it to the next inner loop.
				 */

				didblock = tiddata[level3];
				result = buffer_read(sv->sv_v.vn_fs, didblock,
						SFS_BLOCKSIZE, &didbuf);

				/*
				 * if there's an error, guess we just
				 * lose all the blocks referenced by
				 * this double-indirect block!
				 */
				if (result) {
					kprintf("sfs_dotruncate: error "
						"reading double indirect "
						"block %u: %s\n",
						didblock, strerror(result));
					final_result = result;
					continue;
				}

				/*
				 * This is the loop for level of
				 * indirection 2 Go through all
				 * indirect blocks pointed to from
				 * this double indirect block, discard
				 * the ones that are past the new end
				 * of file.
				 */
				ilevel2:
				diddata = buffer_map(didbuf);
				for (level2 = 0;
				     level2 < SFS_DBPERIDB; level2++) {
					/*
					 * Discard any blocks that are
					 * past the new EOF
					 */
					if (blocklen >= baseblock +
					    (level3) * 
					        SFS_DBPERIDB * SFS_DBPERIDB +
					    (level2) * SFS_DBPERIDB
					    || diddata[level2] == 0) {
						if (diddata[level2] != 0) {
							did_hasnonzero = 1;
						}
						continue;
					}

					/*
					 * Read the indirect block,
					 * hand it to the next inner
					 * loop.
					 */

					idblock = diddata[level2];
					result = buffer_read(sv->sv_v.vn_fs,
							     idblock,
							     SFS_BLOCKSIZE,
							     &idbuf);
					/*
					 * if there's an error, guess
					 * we just lose all the blocks
					 * referenced by this indirect
					 * block!
					 */
					if (result) {
						kprintf("sfs_dotruncate: "
							"error reading "
							"indirect block "
							"%u: %s\n",
							idblock,
							strerror(result));
						final_result = result;
						continue;
					}


					/*
					 * This is the loop for level
					 * of indirection 1
					 * Go through all direct
					 * blocks pointed to from this
					 * indirect block, discard the
					 * ones that are past the new
					 * end of file.
					 */
					ilevel1:
					iddata = buffer_map(idbuf);
					for (level1 = 0;
					     level1<SFS_DBPERIDB; level1++) {
						/*
						 * Discard any blocks
						 * that are past the
						 * new EOF
						 */
						if (blocklen < baseblock +
						    (level3) * SFS_DBPERIDB * 
						       SFS_DBPERIDB +
						    (level2) * SFS_DBPERIDB +
						    level1
						    && iddata[level1] != 0) {

							int block =
								iddata[level1];
							iddata[level1] = 0;
							id_modified = 1;

							sfs_bfree(sfs, block);
						}

						/*
						 * Remember if we see
						 * any nonzero blocks
						 * in here
						 */
						if (iddata[level1]!=0) {
							id_hasnonzero=1;
						}
					}
					/* end for level 1*/

					if (!id_hasnonzero) {
						/*
						 * The whole indirect
						 * block is empty now;
						 * free it
						 */
						sfs_bfree(sfs, idblock);
						if (indir == 1) {
							inodeptr->sfi_indirect
								= 0;
						}
						if (indir != 1) {
							did_modified = 1;
							diddata[level2] = 0;
						}
					}
					else if (id_modified) {
						/*
						 * The indirect block
						 * has been modified
						 */
						buffer_mark_dirty(idbuf);
						if (indir != 1) {
							did_hasnonzero = 1;
						}
					}

					buffer_release(idbuf);

					/*
					 * If we are just doing 1
					 * level of indirection, break
					 * out of the loop
					 */
					if (indir == 1) {
						break;
					}
				}
				/* end for level2 */

				/*
				 * If we are just doing 1 level of
				 * indirection, break out of the loop
				 */
				if (indir == 1) {
					break;
				}

				if (!did_hasnonzero) {
					/*
					 * The whole double indirect
					 * block is empty now; free it
					 */
					sfs_bfree(sfs, didblock);
					if (indir == 2) {
						inodeptr->sfi_dindirect = 0;
					}
					if (indir == 3) {
						tid_modified = 1;
						tiddata[level3] = 0;
					}
				}
				else if (did_modified) {
					/*
					 * The double indirect block
					 * has been modified
					 */
					buffer_mark_dirty(didbuf);
					if (indir == 3) {
						tid_hasnonzero = 1;
					}
				}

				buffer_release(didbuf);
				if (indir < 3) {
					break;
				}
			}
			/* end for level 3 */
			if (indir < 3) {
				indir++;
				continue;  /* while */
			}
			if (!tid_hasnonzero) {
				/*
				 * The whole triple indirect block is
				 * empty now; free it
				 */
				sfs_bfree(sfs, tidblock);
				inodeptr->sfi_tindirect = 0;
			}
			else if (tid_modified) {
				/*
				 * The triple indirect block has been
				 * modified
				 */
				buffer_mark_dirty(tidbuf);
			}
			buffer_release(tidbuf);
			indir++;
		}
	}


	/* Set the file size */
	inodeptr->sfi_size = len;

	/* Mark the inode dirty */
	sfs_dinode_mark_dirty(sv);

	/* release the inode buffer */
	sfs_dinode_unload(sv);

	return final_result;
}

