#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
	#include <Windows.h>
#endif
#include <ctime>
#include <stdarg.h>
#include "Logger.h"
#ifndef WIN32
	#include "pthread_win.h" // contains linux specific declarations
#endif

SimpleLogger sLogger;

void Log(SimpleLogger *logger, LogSeverityFlags severity, const char *file, const char *function, int line, const char *logString)
{
	if (logger == NULL)
	{
		logger = &sLogger;
	}
	if (logger == NULL)
	{
		return;
	}
	if (logger->HasOutput() == false)
	{
		return;
	}

	UNREFERENCED_PARAMETER(file);

	char LogLine[32000];
	int WrittenLen = 0;
	time_t rawtime;
	time(&rawtime);
	struct tm* timeinfo;
#ifndef WIN32
	timeinfo = localtime(&rawtime);
#else
	timeinfo = new tm;
	errno_t er = localtime_s(timeinfo, &rawtime);
	if (er == NO_ERROR)
#endif
	{
		WrittenLen += sprintf_s(LogLine, sizeof(LogLine), "[%d-%d %d:%d:%d:%03d]", timeinfo->tm_mon, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, GetTickCount() % 1000);
#ifdef WIN32
		delete timeinfo;
#endif
	}

	WrittenLen += sprintf_s(&LogLine[WrittenLen], sizeof(LogLine) - WrittenLen, " %s:%d", function, line);
	WrittenLen += sprintf_s(&LogLine[WrittenLen], sizeof(LogLine) - WrittenLen, " :: %s", logString);

	logger->Log(severity, LogLine);
}

void LogV(LogSeverityFlags severity, const char* file, const char* function, int line, const char* format, ...)
{
	char log[10000];
	va_list vl;

	va_start(vl, format);
	vsprintf_s(log, format, vl);
	va_end(vl);

	Log(&sLogger, severity, file, function, line, log);
}

void SimpleLogger::Log(LogSeverityFlags flags, const char* str)
{
	// Are we logging this type of message ?
	if ((mLogSeverityFilter & flags) == 0)
	{
		return;
	}
	// Have valid output ?
	if (mLogFile == NULL)
	{
		return;
	}
	// Sanity check
	if (str == NULL)
	{
		return;
	}

	fwrite(str, 1, strlen(str), mLogFile);

	if (mLogToConsole)
	{
		printf("%s",str);
	}
}

SimpleLogger::SimpleLogger()
{
	Init("logs.txt");
}

void SimpleLogger::Init(const char *FilePath)
{
	// Close previous file if already open
	if (mLogFile != NULL)
	{
		fclose(mLogFile);
		mLogFile = NULL;
	}

	// Open a new file
	errno_t er = fopen_s(&mLogFile, FilePath, "wt");

	// Should already be NULL
	if (er != NOERROR)
	{
		mLogFile = NULL;
	}
}

SimpleLogger::~SimpleLogger()
{
	if(mLogFile != NULL)
	{
		fclose(mLogFile);
		mLogFile = NULL;
	}
}