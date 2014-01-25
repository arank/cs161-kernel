The file system solution set as it currently stands was written by
David A. Holland.

(Various people contributed to the 1.x buffer cache code in a vain
attempt to fix it. It has been nuked from orbit and replaced with new
code for 2.x. I am not even including its old changelog entries.)

------------------------------------------------------------

(Also see CHANGES.vm, CHANGES.syscalls, CHANGES.locks, and CHANGES.)

20140123 dholland	Version 1.99.07 of the file system solutions released.
20140123 dholland	Revert the doom counter. System/161 has one now.
20140123 dholland	Added a "bufstats" menu command.
20140123 dholland	In buf.c, busy_buffers -> inuse_buffers for clarity.
20140114 dholland	Reorganize SFS sources and revise the solutions.

20130104 nmurphy	Merge doom-counter code into the buffer cache branch.
20100108 dholland	Print buffer cache size during kernel startup.
20090503 dholland	Fix double buffer_release on error branch.
			Found by Gideon Wald.
20090425 dholland	Fix embarrassing bug the buffer cache shipped with.

20090414 dholland	buffercache-1.99.04 released.
20090414 dholland	Make buffer_drop not fail.
20090414 dholland	More assorted fixes to the buffer cache.
20090413 dholland	Add the buffer cache to SFS.
20090413 dholland	Assorted fixes and adjustments to the buffer cache.
20090410 dholland	Implement buffer cache eviction. Add syncer.
20090410 dholland	Changes from a code review of the buffer cache.
20090410 dholland	Finish draft of new buffer cache.
20090409 dholland	Code up new buffer cache.
