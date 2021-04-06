#pragma once
#include <string>

enum LogSeverityFlags
{
	LogSeverityTrace = 1,
	LogSeverityInfo = 2,
	LogSeverityError = 4,
	LogSeverityAllFlags = 0xFFFFFF
};

class SimpleLogger;
// C like interface for the logger class
void LogV(LogSeverityFlags severity, const char *file, const char *function, int line, const char *format, ...);
void Log(SimpleLogger* logger, LogSeverityFlags severity, const char* file, const char* function, int line, const char* logString);
using namespace std;

#ifdef WIN32
	#define LOG_TRACE(logFormat, ...) { \
			LogV(LogSeverityFlags::LogSeverityTrace, __FILE__, __FUNCTION__, __LINE__, logFormat, __VA_ARGS__); \
		}
	#define LOG_INFO(logFormat, ...) { \
			LogV(LogSeverityFlags::LogSeverityInfo, __FILE__, __FUNCTION__, __LINE__, logFormat, __VA_ARGS__); \
		}
	#define LOG_ERROR(logFormat, ...) { \
			LogV(LogSeverityFlags::LogSeverityError, __FILE__, __FUNCTION__, __LINE__, logFormat, __VA_ARGS__); \
		}
#else
	#define LOG_TRACE(logFormat, ...) { \
				LogV(LogSeverityFlags::LogSeverityTrace, __FILE__, __FUNCTION__, __LINE__, logFormat, ## __VA_ARGS__); \
			}
	#define LOG_INFO(logFormat, ...) { \
				LogV(LogSeverityFlags::LogSeverityInfo, __FILE__, __FUNCTION__, __LINE__, logFormat, ## __VA_ARGS__); \
			}
	#define LOG_ERROR(logFormat, ...) { \
				LogV(LogSeverityFlags::LogSeverityError, __FILE__, __FUNCTION__, __LINE__, logFormat, ## __VA_ARGS__); \
			}
#endif

// Logger class to hold a logging session details : output, flags ...
// Here is used as a global resource, but possible to be instantiated multiple times
class SimpleLogger
{
public:
	SimpleLogger();
	~SimpleLogger();
	void Init(const char* FilePath);
	bool HasOutput() { return mLogFile != NULL; }
	void Log(LogSeverityFlags flags, const char* str);
	void SetLogLevelFlags(LogSeverityFlags NewFlags) { mLogSeverityFilter = NewFlags; }
private:
	FILE* mLogFile = NULL;
	bool mRotateLogs = false;
	bool mClearLogs = false;
	bool mLogToConsole = true;
	int mLogSeverityFilter = LogSeverityFlags::LogSeverityAllFlags;
};

extern SimpleLogger sLogger;