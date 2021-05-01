#ifndef PTHREAD_WIN_H
#define PTHREAD_WIN_H

#ifdef WIN32
	#include <windows.h>
	#include <process.h>

	#define pthread_mutex_t CRITICAL_SECTION
	//#define pthread_cond_t CONDITION_VARIABLE
	#define pthread_cond_t HANDLE
	#define pthread_t HANDLE

	int pthread_mutex_init(pthread_mutex_t* plock, void* attr);
	int pthread_mutex_lock(pthread_mutex_t* plock);
	int pthread_mutex_trylock(pthread_mutex_t* mutex);
	int pthread_mutex_unlock(pthread_mutex_t* plock);
	int pthread_mutex_destroy(pthread_mutex_t* plock);

	int pthread_cond_init(pthread_cond_t* cond, const void* attr);
	int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
	int pthread_cond_signal(pthread_cond_t* cond);

	#ifndef _beginthread_proc_type
		typedef void (__cdecl* _beginthread_proc_type)(void*);
	#endif

	int pthread_create(pthread_t *thread, void * _StackSize, _beginthread_proc_type _StartAddress, void * _ArgList);

	unsigned int getTick();
	#define _strncpy strcpy_s
	static int SetSocketTimeOut(SOCKET socket, unsigned long timeoutSeconds)
	{
		return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutSeconds, sizeof timeoutSeconds);
	}
	typedef int socklen_t;
	#define __stricmp _stricmp

#else	// Linux version of functions
	#include <time.h>
	#include <cstdint>
	#include <unistd.h>
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <sys/types.h>
	#include <netinet/in.h>
	#include <ifaddrs.h>
	#include <netinet/in.h>
	#include <openssl/md5.h>
	#include <string.h>
	#include <stdio.h>
	#include <cstdlib>

	#define _strncpy(dest,size,src) strncpy(dest,src,size)

	#define GetTickCount getTick
	uint32_t getTick();
	int SetSocketTimeOut(int sockfd, long unsigned int timeout_in_seconds);
	void closesocket(int fd);
	#ifndef INVALID_SOCKET
		#define INVALID_SOCKET  (~0)
	#endif
	#define __stricmp strcasecmp
	typedef int errno_t;
	typedef __int64_t __int64;
	errno_t fopen_s(FILE** out_f, const char* fname, const char* mode);
	char* _strdup(const char* s);
	#define strcpy_s(dst,size,src) strncpy(dst,src,size)
	#define UNREFERENCED_PARAMETER(a)	 // Nothing to do on linux
	#define NO_ERROR 0L
	#define NOERROR 0L
	#define vsprintf_s(dst,formt,vl) vsnprintf(dst,sizeof(dst),formt,vl)
	#define sprintf_s snprintf
	#define localtime_s localtime_r
	#ifndef SOCKET
		#define SOCKET  int
	#endif
#endif

	// There are external libs to generate this, but our project requirement was to use minimal external dependencies
	char* base64_encode(const unsigned char* data, size_t input_length, size_t* output_length);
	void create_guid(char* guid, int maxSize);

#endif // Header end