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

// Const conversion table for base64 encoding
static const char encoding_table[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
								'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
								'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
								'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
								'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
								'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
								'w', 'x', 'y', 'z', '0', '1', '2', '3',
								'4', '5', '6', '7', '8', '9', '+', '/' };
// Compiler will probably optimize this one to static code								
static const int mod_table[] = { 0, 2, 1 };

// Convert a binary string to a base64 encoded string to avoid 0 getting interpreted as a null terminated string
char* base64_encode(const unsigned char* data, size_t input_length, size_t* output_length)
{
	// sanity check for size overflow. Maybe something went wrong
	if (input_length > 15*1024*1024)
	{
		*output_length = 0;
		return NULL;
	}

	*output_length = 4 * ((input_length + 2) / 3);

	char* encoded_data = (char*)malloc(*output_length + 1);
	if (encoded_data == NULL)
		return NULL;

	for (unsigned int i = 0, j = 0; i < input_length;)
	{
		unsigned int octet_a = i < input_length ? (unsigned char)data[i++] : 0;
		unsigned int octet_b = i < input_length ? (unsigned char)data[i++] : 0;
		unsigned int octet_c = i < input_length ? (unsigned char)data[i++] : 0;

		unsigned int triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

		encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
	}

	for (int i = 0; i < mod_table[input_length % 3]; i++)
		encoded_data[*output_length - 1 - i] = '=';

	encoded_data[*output_length] = 0;

	return encoded_data;
}

#ifndef _WIN32
#include <uuid/uuid.h>
void create_guid(char *guid, int maxSize)
{
	uuid_t out;
	uuid_generate(out);
	uuid_unparse_lower(out, guid);
}
#else
#include <objbase.h>
#include <stdio.h>
void create_guid(char *guid, int maxSize)
{
	GUID out = { 0 };
	CoCreateGuid(&out);
	sprintf_s(guid, maxSize, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", out.Data1, out.Data2, out.Data3, out.Data4[0], out.Data4[1], out.Data4[2], out.Data4[3], out.Data4[4], out.Data4[5], out.Data4[6], out.Data4[7]);
}
#endif
