#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>


// 头插法
#define LL_ADD(item, list) do { 	\
	item->prev = NULL;				\
	item->next = list;				\
	list = item;					\
} while(0)

#define LL_REMOVE(item, list) do {						\
	if (item->prev != NULL) item->prev->next = item->next;	\
	if (item->next != NULL) item->next->prev = item->prev;	\
	if (list == item) list = item->next;					\
	item->prev = item->next = NULL;							\
} while(0)


// 表示一个工作线程的结构
typedef struct NWORKER {
	pthread_t thread;    // 工作线程id
	int terminate;    // 这个标志表示的是当前线程的状态
	struct NWORKQUEUE *workqueue;    // 工作队列的指针
	struct NWORKER *prev;    // 指向链表的前一个
	struct NWORKER *next;    // 指向链表的后一个
} nWorker;

// 表示执行的任务的结构
typedef struct NJOB {
	void (*job_function)(struct NJOB *job);    // job的执行函数
	void *user_data;  // 参数
	struct NJOB *prev;  // job链表的前一个
	struct NJOB *next;    // job链表的后一个
} nJob;

// 表示管理工作线程的结构
typedef struct NWORKQUEUE {
	struct NWORKER *workers;    // 工作的线程
	struct NJOB *waiting_jobs;  //  等待执行的任务
	pthread_mutex_t jobs_mtx;  // 任务的锁
	pthread_cond_t jobs_cond;  // 条件变量
} nWorkQueue;

typedef nWorkQueue nThreadPool;

// 参数为线程对应的nWorker
static void *ntyWorkerThread(void *ptr) {
	nWorker *worker = (nWorker*)ptr;

	while (1) {
	    // 条件变量wait内部有解锁和加锁，所以要先加锁
		pthread_mutex_lock(&worker->workqueue->jobs_mtx);
        // 等待工作任务的到来
		while (worker->workqueue->waiting_jobs == NULL) {
		    // 表示当前的工作线程已经结束
			if (worker->terminate) break;
			pthread_cond_wait(&worker->workqueue->jobs_cond, &worker->workqueue->jobs_mtx);
		}
		if (worker->terminate) {
			pthread_mutex_unlock(&worker->workqueue->jobs_mtx);
			break;
		}
		// 如果等到了，就取出当前的任务，并将其从job的队列中移除
		nJob *job = worker->workqueue->waiting_jobs;
		if (job != NULL) {
			LL_REMOVE(job, worker->workqueue->waiting_jobs);
		}
		
		pthread_mutex_unlock(&worker->workqueue->jobs_mtx);

		if (job == NULL) continue;
        // 执行工作的任务
		job->job_function(job);
	}
    // 执行完线程退出的时候，释放worker
	free(worker);
	pthread_exit(NULL);
}

// 传参数：工作队列的指针，以及需要启动的线程的个数
int ntyThreadPoolCreate(nThreadPool *workqueue, int numWorkers) {

	if (numWorkers < 1) numWorkers = 1;
	memset(workqueue, 0, sizeof(nThreadPool));
	// 初始化条件变量
	pthread_cond_t blank_cond = PTHREAD_COND_INITIALIZER;
	memcpy(&workqueue->jobs_cond, &blank_cond, sizeof(workqueue->jobs_cond));
	// 初始化互斥锁
	pthread_mutex_t blank_mutex = PTHREAD_MUTEX_INITIALIZER;
	memcpy(&workqueue->jobs_mtx, &blank_mutex, sizeof(workqueue->jobs_mtx));

    // 创建numworkers个线程，首先每一个线程对应一个nWorker的结构，创建线程，初始化每一个对应的nWorker
	int i = 0;
	for (i = 0;i < numWorkers;i ++) {
		nWorker *worker = (nWorker*)malloc(sizeof(nWorker));
		if (worker == NULL) {
			perror("malloc");
			return 1;
		}

		memset(worker, 0, sizeof(nWorker));
		worker->workqueue = workqueue;

		int ret = pthread_create(&worker->thread, NULL, ntyWorkerThread, (void *)worker);
		if (ret) {
			
			perror("pthread_create");
			free(worker);

			return 1;
		}
        // 将worker加入到对应的worker队列中
		LL_ADD(worker, worker->workqueue->workers);
	}

	return 0;
}

void ntyThreadPoolQueue(nThreadPool *workqueue, nJob *job) {

	pthread_mutex_lock(&workqueue->jobs_mtx);
    // 将job加入到job队列中，并且唤醒信号
	LL_ADD(job, workqueue->waiting_jobs);
	
	pthread_cond_signal(&workqueue->jobs_cond);
	pthread_mutex_unlock(&workqueue->jobs_mtx);
}

void ntyThreadPoolDestory(nThreadPool *workqueue) {
	nWorker *worker = workqueue->workers;
    // 表示所有的线程终止
	for (;worker != NULL;worker = worker->next) {
		worker->terminate = 1;
	}

	pthread_mutex_lock(&workqueue->jobs_mtx);
    // worker是队列和waiting_jobs队列都置为NULL
	workqueue->workers = NULL;
	workqueue->waiting_jobs = NULL;
    // 广播给所有的线程，唤醒，这样的话都会执行到ternimate，退出线程
	pthread_cond_broadcast(&workqueue->jobs_cond);

	pthread_mutex_unlock(&workqueue->jobs_mtx);
	
}

/************************** debug thread pool **************************/

#define KING_MAX_THREAD			80
#define KING_COUNTER_SIZE		1000

void king_counter(nJob *job) {

	int index = *(int*)job->user_data;

	printf("index : %d, selfid : %lu\n", index, pthread_self());
	
	free(job->user_data);
	free(job);
}



int main(int argc, char *argv[]) {

	nThreadPool pool;

	ntyThreadPoolCreate(&pool, KING_MAX_THREAD);
	
	int i = 0;
	for (i = 0;i < KING_COUNTER_SIZE;i ++) {
		nJob *job = (nJob*)malloc(sizeof(nJob));
		if (job == NULL) {
			perror("malloc");
			exit(1);
		}
		
		job->job_function = king_counter;
		job->user_data = malloc(sizeof(int));
		*(int*)job->user_data = i;

		ntyThreadPoolQueue(&pool, job);
		
	}

	getchar();
	printf("\n");

	
}