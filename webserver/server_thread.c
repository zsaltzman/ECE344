#include "request.h"
#include "server_thread.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include "common.h"

typedef struct node{
  char *word;
  int in_use; //used to ensure that the file is not deleted when it is being sent.
  struct file_data *cache_data;
  struct node *next;
  //used in our eviction algorithm (nodes must know which nodes are more or less recently used).
  struct node *more_recently_used; 
  struct node *less_recently_used;
}table_entry;

struct wc {
  table_entry **word_table;
  long table_size;
};

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
        pthread_t **worker_threads;
        int *request_buffer;
        pthread_mutex_t *lock;
        pthread_mutex_t *cache_lock;

        struct wc *cache;
  
        table_entry *least_recently_used; //marks the least recently used entry in the hash table
        table_entry *most_recently_used; //marks the most recently used entry in the hash table

        int cache_remaining; //how much space is left in the cache
        //used to track where we are in the circular buffer
        int index_low;
        int index_high;

        //used to synchronize request handling 
        pthread_cond_t *no_requests;
        pthread_cond_t *no_threads;
	/* add any other parameters you need */
};

struct wc *
wc_init(long size)
{
	struct wc *wc;
	//	printf("Beginning Operation size = %ld.\n",size);
	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);
	wc->word_table = (table_entry **)malloc(size * sizeof(table_entry *));
	assert(wc->word_table);
	wc->table_size = size;
	return wc;
}

static void file_data_free(struct file_data *data);
int cache_evict(struct server *sv, int bytes_to_evict);

//this is our hash function for the hash table.
//all credit to www.cse.yorku.ca/~oz/hash.html
int hash(char *str, long table_size)
{
  int hash = 5381;
  int c = 0;

  while ((c = *str++)!=0)
     {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
     }
   if(hash<0)
     hash *= -1;
   hash = hash%table_size;
   return hash;
}

//checks the cache for a given file, does the server request if the file isn't there. If the file is found, update the more/less recently used tables.
//this must be called within a lock to function properly
table_entry *cache_lookup(struct server *sv, struct wc *wc, char *str, int index)
{
  //if there's no valid entry for this hash, just return.
  if(wc->word_table[index] == NULL)
    {
      printf("%s not found, quitting\n",str);
      return NULL;
    }
  //otherwise we're going to have to check to see if our data is here.
  table_entry *current_index = wc->word_table[index]; //head of that list of table_entries
  table_entry *tmp;
  while(current_index != NULL)
    {
      //if we found the file
      if(strcmp(str,current_index->cache_data->file_name)==0)
	{
	  //we should update the most/least recently used fields.
	  //if the accessed node is the most recently used node we don't have to do anything.
	  if(sv->most_recently_used != current_index)
	  {
	    //this is safe because we're guaranteed that there is a more recently used file.
	    current_index->more_recently_used->less_recently_used = current_index->less_recently_used;
	    tmp = current_index->more_recently_used;
	    current_index->more_recently_used = NULL;
	    
	    //i.e. if we have something behind us, we have to account for it
	    if(sv->least_recently_used != current_index)
	      {
		current_index->less_recently_used->more_recently_used = tmp;
	      }
	    
	    //if the accessed node is the least recently used, we have to set a new least recently used node.
	    else
	      {
		sv->least_recently_used = tmp;
	      }

	    sv->most_recently_used->more_recently_used = current_index;
	    current_index->less_recently_used = sv->most_recently_used;
	    sv->most_recently_used = current_index;
	    
	  }
	  
	  pthread_mutex_unlock(sv->cache_lock);
	  return current_index;
	}
      current_index = current_index -> next;
    }
  //if we get here it means we didn't find it, so it's not cached.
  return NULL;
}

//add a file to our hash table (cache) based on name of file -- see hash function
table_entry *cache_add(struct server *sv, struct file_data *fd, struct wc *wc)
{
  //check immediately if the file is in our cache.
  table_entry *lookup_ret = cache_lookup(sv, sv->cache, fd->file_name, hash(fd->file_name,sv->max_cache_size));
  if(lookup_ret != NULL)
    {
      return lookup_ret;
    }
  int index = hash(fd->file_name, wc->table_size); //find the index our data should go to (based on our hash function)
  int evict_ret;
  if(fd->file_size > sv->max_cache_size) //if the file is bigger than our cache, don't even bother.
    {;
      return NULL;
    }
  if(fd->file_size > sv->cache_remaining)
    {
      printf("attempting to evict files to make room for %s\n",fd->file_name);
      evict_ret = cache_evict(sv,fd->file_size-sv->cache_remaining);
      if(evict_ret == -1) //if we cannot evict enough memory to add this to the cache, quit.
	{
	  return NULL;
	}
    }
  
  if(wc->word_table[index] == NULL)
   {
     wc->word_table[index] = (table_entry *)malloc(sizeof(table_entry));
     wc->word_table[index]->cache_data = fd;
     wc->word_table[index]->next = NULL;
     wc->word_table[index]->in_use = 0;
       
     //no file has been more recently used than the one that was just fetched from disk
     wc->word_table[index]->more_recently_used = NULL;
     
     //the next less recently used file will be the previous most recently used node
     wc->word_table[index]->less_recently_used = sv->most_recently_used;
     
     //by that token, we must also set the more recently used field of that node (if applicable)
     //if this is the first thing into the cache, it will also be the least recently used item.
     if(sv->most_recently_used != NULL)
       sv->most_recently_used->more_recently_used = wc->word_table[index];
     else
       sv->least_recently_used = wc->word_table[index];

     //finally, we set the newly-created node to be the most recently used node.
     sv->most_recently_used = wc->word_table[index];
   }
  else
   {
     table_entry *end_of_list = wc->word_table[index];
     while(end_of_list != NULL)
       {
	 if(strcmp(end_of_list->cache_data->file_name,fd->file_name) == 0) //if the data is already cached, someone beat us here. just return
	   {
	     printf("%s already cached, no add is performed\n",end_of_list->cache_data->file_name);
	     return end_of_list;
	   }
	 end_of_list = end_of_list->next;
       }

     table_entry *te = (table_entry *)malloc(sizeof(table_entry));
     te->cache_data = fd;
     te->next = NULL;
     //see above comments
     wc->word_table[index]->more_recently_used = NULL;
     wc->word_table[index]->less_recently_used = sv->most_recently_used;
     if(sv->most_recently_used != NULL)
       sv->most_recently_used->more_recently_used = wc->word_table[index];
     else
       sv->least_recently_used = wc->word_table[index];
     sv->most_recently_used = wc->word_table[index];

     end_of_list->next = te;
   }
  printf("added %s to the cache.\n",fd->file_name);
  
  //track how much space is left in the cache
  sv->cache_remaining = sv->cache_remaining - fd->file_size;
  return wc->word_table[index];
}

//evicts AT LEAST the specified number of bytes from the cache held by the server, if possible.
//Note that this will likely evict more than needed.
//Note that cache_evict is only called from cache_add, which is locked by default.

int cache_evict(struct server *sv, int bytes_to_evict)
{
  printf("entered cache_evict\n");
  //check to see if we are able to evict enough memory to make room for the new file.
  table_entry *in_use_index = sv->least_recently_used;
  int in_use_bytes = 0;
  while(in_use_index != NULL)
    {;
      if(in_use_index->in_use)
	{
	  printf("%s in use -- don't evict me!\n",in_use_index->cache_data->file_name);
	  in_use_bytes += in_use_index->cache_data->file_size;

	}
      in_use_index = in_use_index->more_recently_used;
    }
  if(sv->max_cache_size - in_use_bytes < bytes_to_evict) //if there is no possible way to evict enough memory with the files that are in-use, quit.
    {
      printf("not enough memory, quitting cache_evict\n");
      return -1;
    }
  int bytes_evicted = 0; //total number of bytes evicted so far.
  table_entry *tmp;

  printf("least recently used block: %s\n",sv->least_recently_used->cache_data->file_name);
  table_entry *index = sv->cache->word_table[hash(sv->least_recently_used->cache_data->file_name, sv->max_cache_size)];
  table_entry *prev = NULL;
  
  //used to track the current index we're at in the cache with respect to most/least recently used.
  table_entry *eviction_index = sv->least_recently_used;
  while(bytes_evicted < bytes_to_evict && eviction_index != NULL)
    {
      //printf("in eviction loop.\n");
      if(eviction_index->in_use == 0)
	{
	  tmp = eviction_index;
	  eviction_index = eviction_index->more_recently_used;
	  printf("evicting %s\n",tmp->cache_data->file_name);
	  //if there's only one node here we know that it is the one we should evict.
	  if(sv->cache->word_table[hash(tmp->cache_data->file_name, sv->max_cache_size)]->next == NULL)
	    { 
	      if(tmp == sv->least_recently_used)
		{
		sv->least_recently_used = sv->least_recently_used->more_recently_used;
		if(sv->least_recently_used == NULL) //evicting only member of cache
		  sv->most_recently_used = NULL;
		}
	      else //something before us
		{
		tmp->less_recently_used->more_recently_used = tmp->more_recently_used;
		if(tmp != sv->most_recently_used)
		  tmp->more_recently_used->less_recently_used = tmp->less_recently_used;
		else
		  sv->most_recently_used = tmp->less_recently_used;
		}
	      sv->cache_remaining += tmp->cache_data->file_size;
	      sv->cache->word_table[hash(tmp->cache_data->file_name,sv->max_cache_size)] = NULL;
	      file_data_free(tmp->cache_data);
	      free(tmp);
	    }
	  //otherwise we have to check which node should be removed.
	  else
	    {
	      while(index != NULL && strcmp(index->cache_data->file_name, tmp->cache_data->file_name)!= 0)
		{
		  prev = index;
		  index = index->next;
		}
	      if(prev == NULL)
		sv->cache->word_table[hash(tmp->cache_data->file_name,sv->max_cache_size)] = index->next;
	      else
		prev->next = index->next; //maintain ordering within hash table
	      if(tmp == sv->least_recently_used)
		{
		sv->least_recently_used = sv->least_recently_used->more_recently_used;
		if(sv->least_recently_used == NULL)
		  sv->most_recently_used = NULL;
		}
	      else
		{
		  tmp->less_recently_used->more_recently_used = tmp->more_recently_used;
		  if(tmp != sv->most_recently_used)
		    tmp->more_recently_used->less_recently_used = tmp->less_recently_used;
		  else
		    sv->most_recently_used = tmp->less_recently_used;
		}
	      sv->cache_remaining += tmp->cache_data->file_size;
	      file_data_free(tmp->cache_data);
	      free(tmp);
	    }	  
	}
      else
	eviction_index = eviction_index->more_recently_used;
    }
  printf("files successfully evicted\n");
  return 1;
}

//delete all records associated with the head of a table_entry list.
void table_entry_delete(table_entry *te)
{
  if(te->next != NULL)
    table_entry_delete(te->next);
  free(te->word);
  free(te);
}

void
wc_destroy(struct wc *wc)
{
  int i = 0;
  while(i < wc->table_size)
    {
      //if we have an entry here, do a recursive delete
      if(wc->word_table[i] != NULL)
	table_entry_delete(wc->word_table[i]);
      i++;
    }
  free(wc->word_table);
  free(wc);
}

void worker_request_loop(void *sv);

/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
        int ret;
	struct request *rq;
	struct file_data *data;
	table_entry *cache_fd; //the return value of the cache lookup. possibly NULL.
	data = file_data_init();

	/* fills data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
	/* reads file, 
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
	if(sv->max_cache_size > 0)
	  {
	    pthread_mutex_lock(sv->cache_lock);
	    cache_fd = cache_lookup(sv, sv->cache, data->file_name, hash(data->file_name,sv->max_cache_size));
	    if(cache_fd == NULL) //cache miss
	      {
		printf("cache miss, requesting file from disk.\n");
		pthread_mutex_unlock(sv->cache_lock);
	    
		ret = request_readfile(rq);

		pthread_mutex_lock(sv->cache_lock);
		cache_fd = cache_add(sv,data,sv->cache);
		if(cache_fd != NULL) //if the file can't be cached, then it shouldn't be marked in-use.
		  {
		    cache_fd->in_use++;
		  }
		pthread_mutex_unlock(sv->cache_lock);
	      }
	    else //cache hit
	      {
		printf("cache hit, sending %s.\n",cache_fd->cache_data->file_name);
		data->file_buf = cache_fd->cache_data->file_buf;
		data->file_size = cache_fd->cache_data->file_size;
		cache_fd->in_use++; //to indicate how many files are using the data, as there could be more than one.
		pthread_mutex_unlock(sv->cache_lock);
	    
		request_sendfile(rq);
	    
		pthread_mutex_lock(sv->cache_lock);
		cache_fd->in_use--; //no longer using the data -- safe to evict.
		printf("%s finished sending, %d other requests sending it now.\n",cache_fd->cache_data->file_name,cache_fd->in_use);
		pthread_mutex_unlock(sv->cache_lock);
		goto out;
	      }
	    if (!ret)
	      goto out;
	    /* sends file to client */
	    printf("cache miss, sending %s to client\n", data->file_name);
	    request_sendfile(rq);
	    if(cache_fd != NULL)
	      {
		pthread_mutex_lock(sv->cache_lock);
		cache_fd->in_use--;
		printf("%s finished sending, %d other requests sending it now.\n",cache_fd->cache_data->file_name,cache_fd->in_use);
		pthread_mutex_unlock(sv->cache_lock);
	      }
	    else
	      printf("%s finished sending. data was not cached.\n",data->file_name);
	  }
	else
	  {
	    ret = request_readfile(rq);
	    if (!ret)
	      goto out;
	    /* sends file to client */
	    request_sendfile(rq);
	  }
out:
	request_destroy(rq);
	
	//freeing this will cause memory corruption in the cache.
	//file_data_free(data);
}



/* entry point functions */


struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{

        struct server *sv;
	
	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests+1;
	sv->max_cache_size = max_cache_size;

	sv->lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(sv->lock, NULL);
        sv->cache_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(sv->cache_lock, NULL);
	
	sv->no_requests = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	sv->no_threads = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	pthread_cond_init(sv->no_requests,NULL);
	pthread_cond_init(sv->no_threads,NULL);

	sv->index_low = 0;
	sv->index_high = 0;
	sv->most_recently_used = NULL;
	sv->least_recently_used = NULL;
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
	  if (nr_threads > 0)
	    {
	    sv->worker_threads = (pthread_t **)malloc(sizeof(pthread_t *)*nr_threads); //allocate the worker threads we will use.
	    
	    int i = 0;
	    while (i<nr_threads)
	      {
	      sv->worker_threads[i] = (pthread_t *)malloc(sizeof(pthread_t));
	      pthread_create(sv->worker_threads[i], NULL, (void *)&worker_request_loop,(void *)sv);
	      i++;
	      }
	    }
	  if(max_requests > 0)
	    sv->request_buffer = (int *)malloc(sizeof(int) * (max_requests+1));
	  if(max_cache_size > 0)
	    {
	      sv->cache = wc_init(max_cache_size); //create a cache with a large number of buckets for the hash table.
	      sv->cache_remaining = max_cache_size;
	    }
	 }

	/* Lab 4: create queue of max_request size when max_requests > 0 */

	/* Lab 5: init server cache and limit its size to max_cache_size */

	/* Lab 4: create worker threads when nr_threads > 0 */

	return sv;
}

void 
worker_request_loop(void *sv_passed)
{
  int cfd = 0;
  struct server *sv;
  sv = (struct server *)sv_passed;
  while(1)
    {
      pthread_mutex_lock(sv->lock);
      //if there are no requests in the buffer, wait
      while((sv->index_high-sv->index_low+sv->max_requests)%sv->max_requests == 0)
	pthread_cond_wait(sv->no_requests,sv->lock);
      
      cfd = sv->request_buffer[sv->index_low];
      sv->request_buffer[sv->index_low] = 0;
      if (sv->index_low == sv->max_requests-1)
	sv->index_low = 0;
      else
	sv->index_low++;
      
      //wake up threads waiting on an empty buffer slot
      pthread_cond_signal(sv->no_threads);
      
      pthread_mutex_unlock(sv->lock);
      do_server_request(sv,cfd);
    }
}
void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
	  pthread_mutex_lock(sv->lock);
	  //if there are no threads to handle our requests and the buffer is full
	  if((sv->index_high-sv->index_low+sv->max_requests)%sv->max_requests == sv->max_requests-1)
	    pthread_cond_wait(sv->no_threads,sv->lock);
	  sv->request_buffer[sv->index_high] = connfd;
	  if(sv->index_high == sv->max_requests-1)
	    sv->index_high = 0;
	  else
	    sv->index_high++;
	  //signal threads waiting on requests
	  pthread_cond_signal(sv->no_requests);
	  
	  pthread_mutex_unlock(sv->lock);
	}
}
