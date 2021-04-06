#include "pthread_win.h"

#if WIN32
int pthread_mutex_init(pthread_mutex_t* plock, void* attr) {
	InitializeCriticalSection(plock);
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t* plock) {
	EnterCriticalSection(plock);
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* mutex)
{
	return TryEnterCriticalSection(mutex);
}

int pthread_mutex_unlock(pthread_mutex_t* plock) {
	LeaveCriticalSection(plock);
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* plock) {
	DeleteCriticalSection(plock);
	return 0;
}

int pthread_create(pthread_t *thread, void* _StackSize, _beginthread_proc_type _StartAddress, void* _ArgList)
{
	*thread = (pthread_t)_beginthread(_StartAddress, 0, _ArgList);
	return (thread <= 0); // Should have handled errors here
}

int pthread_cond_init(pthread_cond_t* cond, const void* attr)
{
//	InitializeConditionVariable(cond);
	*cond = CreateEvent(NULL, TRUE, FALSE, TEXT("JobArrivedEvent"));
	return 0;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex)
{
//	SleepConditionVariableCS(cond, mutex, INFINITE);
	return WaitForSingleObject(*cond, INFINITE);
}

int pthread_cond_signal(pthread_cond_t* cond)
{
//	WakeConditionVariable(cond);
	return SetEvent(*cond);
}

unsigned int getTick()
{
	return GetTickCount();
}

#else // Linux version

uint32_t getTick()
{
	struct timespec ts;
	unsigned theTick = 0U;
	clock_gettime(CLOCK_REALTIME, &ts);
	theTick = ts.tv_nsec / 1000000;
	theTick += ts.tv_sec * 1000;
	return theTick;
}
int SetSocketTimeOut(int sockfd, long unsigned int timeout_in_seconds)
{
	struct timeval tv;
	tv.tv_sec = timeout_in_seconds;
	tv.tv_usec = 0;
	return setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
}
void closesocket(int fd)
{
	close(fd);
}
errno_t fopen_s(FILE** out_f, const char* fname, const char* mode)
{
	*out_f = fopen(fname, mode);
	return *out_f == NULL;
}
char* _strdup(const char* s)
{
	size_t slen = strlen(s);
	char* result = (char*)malloc(slen + 1);
	if (result == NULL)
	{
		return NULL;
	}

	memcpy(result, s, slen + 1);
	return result;
}

#endif