//
// gl3_Jobs.c
//
// Multi-threaded job system implementation using SDL3 threads.
//

#include "gl3_Jobs.h"
#include "gl3_Local.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JOBS 1024
#define MAX_WORKER_THREADS 16

// Job queue (ring buffer).
typedef struct job_queue_s
{
	job_t jobs[MAX_JOBS];
	volatile int head;
	volatile int tail;
	SDL_Mutex* mutex;
	SDL_Semaphore* semaphore;
} job_queue_t;

// Worker thread context.
typedef struct worker_thread_s
{
	SDL_Thread* thread;
	int thread_id;
	volatile qboolean running;
} worker_thread_t;

// Global job system state.
static struct
{
	qboolean initialized;
	int num_threads;
	worker_thread_t workers[MAX_WORKER_THREADS];
	job_queue_t queue;
	volatile int next_job_id;
} job_system;

// ============================================================
// Job Queue Operations.
// ============================================================

static qboolean JobQueue_Init(job_queue_t* queue)
{
	memset(queue, 0, sizeof(*queue));
	queue->mutex = SDL_CreateMutex();
	queue->semaphore = SDL_CreateSemaphore(0);
	
	if (!queue->mutex || !queue->semaphore)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitJobs: Failed to create synchronization primitives\n");
		if (queue->mutex) SDL_DestroyMutex(queue->mutex);
		if (queue->semaphore) SDL_DestroySemaphore(queue->semaphore);
		return false;
	}
	
	return true;
}

static void JobQueue_Shutdown(job_queue_t* queue)
{
	if (queue->mutex)
	{
		SDL_DestroyMutex(queue->mutex);
		queue->mutex = NULL;
	}
	if (queue->semaphore)
	{
		SDL_DestroySemaphore(queue->semaphore);
		queue->semaphore = NULL;
	}
}

static qboolean JobQueue_Push(job_queue_t* queue, const job_t* job)
{
	SDL_LockMutex(queue->mutex);
	
	const int next_tail = (queue->tail + 1) % MAX_JOBS;
	if (next_tail == queue->head)
	{
		SDL_UnlockMutex(queue->mutex);
		ri.Con_Printf(PRINT_ALL, "JobQueue_Push: Queue full!\n");
		return false;
	}
	
	queue->jobs[queue->tail] = *job;
	queue->tail = next_tail;
	
	SDL_UnlockMutex(queue->mutex);
	SDL_SignalSemaphore(queue->semaphore);
	
	return true;
}

static qboolean JobQueue_Pop(job_queue_t* queue, job_t* out_job)
{
	SDL_LockMutex(queue->mutex);
	
	if (queue->head == queue->tail)
	{
		SDL_UnlockMutex(queue->mutex);
		return false;
	}
	
	*out_job = queue->jobs[queue->head];
	queue->head = (queue->head + 1) % MAX_JOBS;
	
	SDL_UnlockMutex(queue->mutex);
	
	return true;
}

// ============================================================
// Worker Thread.
// ============================================================

static int WorkerThread_Main(void* data)
{
	worker_thread_t* worker = (worker_thread_t*)data;

	while (1)
	{
		// Wait for a job to be available or shutdown signal.
		SDL_WaitSemaphore(job_system.queue.semaphore);

		// Check if we should exit (check this AFTER waking up).
		if (!worker->running)
			break;

		// Try to pop a job from the queue.
		job_t job;
		if (JobQueue_Pop(&job_system.queue, &job))
		{
			// Execute the job function.
			job.func(job.data);

			// Mark job as completed.
			if (job.completion_flag)
				*job.completion_flag = 1;
		}
		// If queue was empty, we just loop and wait again.
	}

	return 0;
}

// ============================================================
// Public API.
// ============================================================

qboolean GL3_InitJobs(int num_threads)
{
	if (job_system.initialized)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitJobs: Already initialized\n");
		return false;
	}
	
	memset(&job_system, 0, sizeof(job_system));
	
	// Determine number of threads.
	if (num_threads <= 0)
	{
		const int cpu_count = SDL_GetNumLogicalCPUCores();
		num_threads = max(1, cpu_count - 1); // Leave one core for main thread.
	}
	
	num_threads = min(num_threads, MAX_WORKER_THREADS);
	job_system.num_threads = num_threads;
	
	// Initialize job queue.
	if (!JobQueue_Init(&job_system.queue))
		return false;
	
	// Create worker threads.
	for (int i = 0; i < num_threads; i++)
	{
		worker_thread_t* worker = &job_system.workers[i];
		worker->thread_id = i;
		worker->running = true;
		
		char thread_name[32];
		snprintf(thread_name, sizeof(thread_name), "GL3_Worker_%d", i);
		
		worker->thread = SDL_CreateThread(WorkerThread_Main, thread_name, worker);
		if (!worker->thread)
		{
			ri.Con_Printf(PRINT_ALL, "GL3_InitJobs: Failed to create worker thread %d\n", i);
			
			// Cleanup already created threads.
			for (int j = 0; j < i; j++)
			{
				job_system.workers[j].running = false;
				SDL_SignalSemaphore(job_system.queue.semaphore);
				SDL_WaitThread(job_system.workers[j].thread, NULL);
			}
			
			JobQueue_Shutdown(&job_system.queue);
			return false;
		}
	}
	
	job_system.initialized = true;
	ri.Con_Printf(PRINT_ALL, "GL3_InitJobs: Created %d worker threads\n", num_threads);
	
	return true;
}

void GL3_ShutdownJobs(void)
{
	if (!job_system.initialized)
		return;

	ri.Con_Printf(PRINT_ALL, "GL3_ShutdownJobs: Shutting down job system...\n");

	// Wait for pending jobs to complete.
	GL3_WaitForAllJobs();

	// Set flag to signal all workers to exit (BEFORE signaling semaphore).
	for (int i = 0; i < job_system.num_threads; i++)
	{
		job_system.workers[i].running = false;
	}

	// Signal all worker threads to wake up and check the running flag.
	// Must signal once for each thread that might be waiting on the semaphore.
	for (int i = 0; i < job_system.num_threads; i++)
	{
		SDL_SignalSemaphore(job_system.queue.semaphore);
	}

	// Wait for all threads to finish.
	for (int i = 0; i < job_system.num_threads; i++)
	{
		if (job_system.workers[i].thread)
		{
			SDL_WaitThread(job_system.workers[i].thread, NULL);
			job_system.workers[i].thread = NULL;
		}
	}

	// Cleanup queue synchronization primitives.
	JobQueue_Shutdown(&job_system.queue);

	job_system.initialized = false;
	ri.Con_Printf(PRINT_ALL, "GL3_ShutdownJobs: Job system shutdown complete\n");
}

job_handle_t GL3_ScheduleJob(JobFunc func, void* data)
{
	job_handle_t handle;
	handle.job_id = -1;
	handle.completed = NULL;
	
	if (!job_system.initialized)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_ScheduleJob: Job system not initialized\n");
		return handle;
	}
	
	// Allocate completion flag.
	volatile int* completion_flag = (volatile int*)malloc(sizeof(int));
	*completion_flag = 0;
	
	job_t job;
	job.func = func;
	job.data = data;
	job.completion_flag = completion_flag;
	job.job_id = SDL_AtomicIncRef((SDL_AtomicInt*)&job_system.next_job_id);
	
	if (JobQueue_Push(&job_system.queue, &job))
	{
		handle.job_id = job.job_id;
		handle.completed = completion_flag;
	}
	else
	{
		free((void*)completion_flag);
	}
	
	return handle;
}

void GL3_WaitForJob(job_handle_t handle)
{
	if (!handle.completed)
		return;
	
	// Busy-wait with yield (could be improved with condition variable).
	while (*handle.completed == 0)
		SDL_Delay(0);
	
	// Free the completion flag.
	free((void*)handle.completed);
}

void GL3_WaitForAllJobs(void)
{
	if (!job_system.initialized)
		return;
	
	// Wait until queue is empty.
	while (job_system.queue.head != job_system.queue.tail)
		SDL_Delay(0);
}

int GL3_GetNumWorkerThreads(void)
{
	return job_system.num_threads;
}
