#include "testfs.h"
#include "list.h"
#include "super.h"
#include "block.h"
#include "inode.h"


const int MAX_BLOCK_NR = NR_DIRECT_BLOCKS + NR_INDIRECT_BLOCKS + (NR_INDIRECT_BLOCKS * NR_INDIRECT_BLOCKS);

/* given logical block number, read the corresponding physical block into block.
 * return physical block number.
 * returns 0 if physical block does not exist.
 * returns negative value on other errors. */
static int
testfs_read_block(struct inode *in, int log_block_nr, char *block)
{

        int phy_block_nr = 0;
	int temp_block_nr = 0;
	assert(log_block_nr >= 0);

	if (log_block_nr >= MAX_BLOCK_NR)
	    return -EFBIG;
 
	if (log_block_nr < NR_DIRECT_BLOCKS) {
		phy_block_nr = (int)in->in.i_block_nr[log_block_nr];
	}
	else {
		log_block_nr -= NR_DIRECT_BLOCKS;

		if (log_block_nr >= NR_INDIRECT_BLOCKS && in->in.i_dindirect > 0) {
		  
		  read_blocks(in->sb, block, in->in.i_dindirect, 1);
		  log_block_nr -= NR_INDIRECT_BLOCKS;
		  //each address in the dindirect array points to an indirect block.
		  
		  temp_block_nr = ((int *)block)[log_block_nr/NR_INDIRECT_BLOCKS];
		  if(temp_block_nr == 0)
		    return 0;
		  // printf("reading block %d in dindirect\n", log_block_nr/NR_INDIRECT_BLOCKS);
		  read_blocks(in->sb, block, temp_block_nr, 1);
		  
		  // printf("reading block %d in indirect\n", log_block_nr%NR_INDIRECT_BLOCKS);		  
		  phy_block_nr = ((int *)block)[log_block_nr%NR_INDIRECT_BLOCKS];
		  // printf("resulting block number: %d\n",phy_block_nr);
		}
		else if (in->in.i_indirect > 0) {
			read_blocks(in->sb, block, in->in.i_indirect, 1);
			phy_block_nr = ((int *)block)[log_block_nr];
		}
	}
	if (phy_block_nr > 0) {
		read_blocks(in->sb, block, phy_block_nr, 1);
	}
	else {
		/* we support sparse files by zeroing out a block that is not
		 * allocated on disk. */
		bzero(block, BLOCK_SIZE);
	}
	return phy_block_nr;
}

int
testfs_read_data(struct inode *in, char *buf, off_t start, size_t size)
{
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE;
	long block_ix = start % BLOCK_SIZE;
	int ret;
	int bytes_read = 0;
	
	assert(buf);
	if (start + (off_t) size > in->in.i_size) {
		size = in->in.i_size - start;
	}
	while (block_ix + ((int)size-bytes_read) > BLOCK_SIZE) {
	  if((ret = testfs_read_block(in, block_nr, block)) < 0)
	    return ret;
	  memcpy(buf + bytes_read,block + block_ix, BLOCK_SIZE-block_ix);
	  bytes_read += BLOCK_SIZE-block_ix;
	  block_ix = 0;
	  block_nr++;
	}
	if ((ret = testfs_read_block(in, block_nr, block)) < 0)
		return ret;
	memcpy(buf+bytes_read, block + block_ix, size-bytes_read);
	bytes_read += size-bytes_read;
	/* return the number of bytes read or any error */
	return bytes_read;
}

/* given logical block number, allocate a new physical block, if it does not
 * exist already, and return the physical block number that is allocated.
 * returns negative value on error. */
static int
testfs_allocate_block(struct inode *in, int log_block_nr, char *block)
{
	int phy_block_nr;
	char indirect[BLOCK_SIZE];
	char dindirect[BLOCK_SIZE];
	int indirect_allocated = 0;
	int dindirect_allocated = 0;
	int dindirect_indirect_allocated = 0;

	
	assert(log_block_nr >= 0);
	phy_block_nr = testfs_read_block(in, log_block_nr, block);

	/* phy_block_nr > 0: block exists, so we don't need to allocate it, 
	   phy_block_nr < 0: some error */
	
	if (phy_block_nr != 0)
		return phy_block_nr;

	/* allocate a direct block */
	if (log_block_nr < NR_DIRECT_BLOCKS) {
		assert(in->in.i_block_nr[log_block_nr] == 0);
		phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr >= 0) {
			in->in.i_block_nr[log_block_nr] = phy_block_nr;
		}
		return phy_block_nr;
	}

	log_block_nr -= NR_DIRECT_BLOCKS;
	
	/*allocate a doubly indirect block*/
	if (log_block_nr >= NR_INDIRECT_BLOCKS){
	  log_block_nr -= NR_INDIRECT_BLOCKS;
	  
	  if(in->in.i_dindirect == 0){
	    bzero(dindirect, BLOCK_SIZE);
	    phy_block_nr = testfs_alloc_block_for_inode(in);
	    if(phy_block_nr < 0)
	      return phy_block_nr;
	    dindirect_allocated = 1;
	    in->in.i_dindirect = phy_block_nr;
	  }
	  else
	    read_blocks(in->sb, dindirect, in->in.i_dindirect, 1);

	  //allocate an indirect block if needed
	  // printf("%d at dindirect[%d]\n",((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS], log_block_nr/NR_INDIRECT_BLOCKS);
	  if (((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS] == 0){
	    bzero(indirect, BLOCK_SIZE);
	    phy_block_nr = testfs_alloc_block_for_inode(in);
	    if(phy_block_nr < 0)
	      {
		if (phy_block_nr == -ENOSPC)
		  {
		    if(dindirect_allocated)
		      testfs_free_block_from_inode(in, in->in.i_dindirect);
		  }
		return phy_block_nr;
	      }
	    ((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS] = phy_block_nr;
	    write_blocks(in->sb, dindirect, in->in.i_dindirect, 1);
	    dindirect_indirect_allocated = 1;
	   }
	  else
	    read_blocks(in->sb, indirect, ((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS], 1);
	  
	  log_block_nr += NR_INDIRECT_BLOCKS;
	}	
	else if (log_block_nr < NR_INDIRECT_BLOCKS)
	  {
	    if (in->in.i_indirect == 0) {	/* allocate an indirect block */
	      bzero(indirect, BLOCK_SIZE);
	      phy_block_nr = testfs_alloc_block_for_inode(in);
	      if (phy_block_nr < 0)
		return phy_block_nr;
	      indirect_allocated = 1;
	      in->in.i_indirect = phy_block_nr;
	    } else {	/* read indirect block */
	      read_blocks(in->sb, indirect, in->in.i_indirect, 1);
	    }
	  }
	/* allocate direct block */
	//assert(((int *)indirect)[log_block_nr] == 0);
	
	phy_block_nr = testfs_alloc_block_for_inode(in);
        if(phy_block_nr < 0)
	  {
	    if(indirect_allocated)
	      testfs_free_block_from_inode(in, in->in.i_indirect);
	    if(dindirect_indirect_allocated)
	      {
		log_block_nr -= NR_INDIRECT_BLOCKS;
		testfs_free_block_from_inode(in, ((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS]);
	      }
	    if(dindirect_allocated)
	      testfs_free_block_from_inode(in, in->in.i_dindirect);
	  }
	
	//block belongs in indirect block section
	if(log_block_nr < NR_INDIRECT_BLOCKS)
	  {
	    if (phy_block_nr >= 0) {
	      /* update indirect block */
	      ((int *)indirect)[log_block_nr] = phy_block_nr;
	      write_blocks(in->sb, indirect, in->in.i_indirect, 1);
	    } else if (indirect_allocated) {
	      /* free the indirect block that was allocated */
	      testfs_free_block_from_inode(in, in->in.i_indirect);
	    }
	  }
	else //block belongs in dindirect block section. 
	  {
	    log_block_nr -= NR_INDIRECT_BLOCKS;
	    if (phy_block_nr >= 0){
	      ((int *)indirect)[log_block_nr%NR_INDIRECT_BLOCKS] = phy_block_nr;
	      // printf("setting indirect[%d] to %d\n",log_block_nr%NR_INDIRECT_BLOCKS, phy_block_nr);
	      write_blocks(in->sb, indirect, ((int *)dindirect)[log_block_nr/NR_INDIRECT_BLOCKS], 1);
	     }
	      else if (dindirect_allocated){
		testfs_free_block_from_inode(in,in->in.i_dindirect); //not sure about this one
	      }
	  }
	return phy_block_nr;
}

int
testfs_write_data(struct inode *in, const char *buf, off_t start, size_t size)
{
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE;
	long block_ix = start % BLOCK_SIZE;
	int ret;
	int bytes_written = 0;

	
	
	while (block_ix + ((int)size-bytes_written) > BLOCK_SIZE) {
	  ret = testfs_allocate_block(in, block_nr, block);	 
	  if(ret<0)
	    {
	      //printf("start + bytes_written: %ld\n", start + (off_t)bytes_written);
	      if(ret == -EFBIG)
		in->in.i_size = MAX(in->in.i_size, start + (off_t)bytes_written);
	      return ret;
	    }
	  memcpy(block+block_ix, buf+bytes_written, BLOCK_SIZE-block_ix);
	  write_blocks(in->sb, block, ret, 1);
	  bytes_written += BLOCK_SIZE-block_ix;
	  block_ix = 0;
	  block_nr++;
	}
	
	/*	if(block_nr > MAX_BLOCK_NR)
	  {
	    in->in.i_size = MAX(in->in.i_size, start + (off_t)bytes_written);
	    return -EFBIG;
	    }*/
	
	/* ret is the newly allocated physical block number */
	ret = testfs_allocate_block(in, block_nr, block);
	if (ret < 0)
	  {
	      in->in.i_size = MAX(in->in.i_size, start + (off_t)bytes_written);
	      return ret;
	  }
	memcpy(block + block_ix, buf+bytes_written, (int)size-bytes_written);
	write_blocks(in->sb, block, ret, 1);
	bytes_written += (int)size-bytes_written;
	/* increment i_size by the number of bytes written. */
	if (size > 0)
		in->in.i_size = MAX(in->in.i_size, start + (off_t) size);
	in->i_flags |= I_FLAGS_DIRTY;
	/* return the number of bytes written or any error */
	return size;
}

int
testfs_free_blocks(struct inode *in)
{
	int i;
	int j;
	int e_block_nr;

	/* last block number */
	e_block_nr = DIVROUNDUP(in->in.i_size, BLOCK_SIZE);

	/* remove direct blocks */
	for (i = 0; i < e_block_nr && i < NR_DIRECT_BLOCKS; i++) {
		if (in->in.i_block_nr[i] == 0)
			continue;
		testfs_free_block_from_inode(in, in->in.i_block_nr[i]);
		in->in.i_block_nr[i] = 0;
	}
	e_block_nr -= NR_DIRECT_BLOCKS;

	/* remove indirect blocks */
	if (in->in.i_indirect > 0) {
		char block[BLOCK_SIZE];
		read_blocks(in->sb, block, in->in.i_indirect, 1);
		for (i = 0; i < e_block_nr && i < NR_INDIRECT_BLOCKS; i++) {
			testfs_free_block_from_inode(in, ((int *)block)[i]);
			((int *)block)[i] = 0;
		}
		testfs_free_block_from_inode(in, in->in.i_indirect);
		in->in.i_indirect = 0;
	}

	e_block_nr -= NR_INDIRECT_BLOCKS;
	if (e_block_nr >= 0) {
	  if(in->in.i_dindirect > 0){
	    char i_block[BLOCK_SIZE];
	    char d_block[BLOCK_SIZE];

	    read_blocks(in->sb, d_block, in->in.i_dindirect, 1);
	    for (i = 0; i <= e_block_nr/NR_INDIRECT_BLOCKS && i < NR_INDIRECT_BLOCKS; i++){
	      if (((int *)d_block)[i] > 0){
		read_blocks(in->sb, i_block, ((int *)d_block)[i], 1);
		for (j = 0; j + (NR_INDIRECT_BLOCKS * i) < e_block_nr && j < NR_INDIRECT_BLOCKS; j++) {
		  if(((int *)i_block)[j] > 0)
		    {
		      testfs_free_block_from_inode(in, ((int *)i_block)[j]);
		      ((int *)i_block)[j] = 0;
		    }
		}
		testfs_free_block_from_inode(in, ((int *)d_block)[i]);
		((int *)d_block)[i] = 0;
	      }
	    }
	    testfs_free_block_from_inode(in, in->in.i_dindirect);
	    in->in.i_dindirect = 0;
	  }
	}

	in->in.i_size = 0;
	in->i_flags |= I_FLAGS_DIRTY;
	//	printf("%i blocks still allocated\n",in->in.block_count);
	return 0;
}
