#include "request.h"
#include "server_thread.h"
#include <pthread.h>
#include "common.h"


struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
        pthread_t **worker_threads;
        int *request_buffer;
        pthread_mutex_t *lock;

        //used to track where we are in the circular buffer
        int index_low;
        int index_high;

        //used to synchronize request handling 
        pthread_cond_t *no_requests;
        pthread_cond_t *no_threads;
	/* add any other parameters you need */
};

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
	ret = request_readfile(rq);
	if (!ret)
		goto out;
	/* sends file to client */
	request_sendfile(rq);
out:
	request_destroy(rq);
	file_data_free(data);
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

	sv->no_requests = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	sv->no_threads = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	pthread_cond_init(sv->no_requests,NULL);
	pthread_cond_init(sv->no_threads,NULL);

	sv->index_low = 0;
	sv->index_high = 0;
	
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
