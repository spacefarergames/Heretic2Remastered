//
// gl3_Jobs.h
//
// Multi-threaded job system for parallelizing renderer tasks.
//

#ifndef GL3_JOBS_H
#define GL3_JOBS_H

#include "../game/q_shared.h"

// Job callback function type.
typedef void (*JobFunc)(void* data);

// Job handle for tracking completion.
typedef struct job_handle_s
{
	int job_id;
	volatile int* completed;
} job_handle_t;

// Job data structure.
typedef struct job_s
{
	JobFunc func;
	void* data;
	volatile int* completion_flag;
	int job_id;
} job_t;

// Initialize the job system with the specified number of worker threads.
// If num_threads is 0, uses (CPU cores - 1) to leave one for main thread.
qboolean GL3_InitJobs(int num_threads);

// Shutdown the job system and wait for all pending jobs.
void GL3_ShutdownJobs(void);

// Schedule a job to be executed on a worker thread.
// Returns a handle that can be used to wait for completion.
job_handle_t GL3_ScheduleJob(JobFunc func, void* data);

// Wait for a specific job to complete.
void GL3_WaitForJob(job_handle_t handle);

// Wait for all pending jobs to complete.
void GL3_WaitForAllJobs(void);

// Get the number of worker threads.
int GL3_GetNumWorkerThreads(void);

#endif // GL3_JOBS_H
