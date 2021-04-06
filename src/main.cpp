#include "TFTP.h"
#include "config.h"
#include "Logger.h"

void RunTFTPServer(unsigned short port);

int main(int argc, char* argv[])
{

	if (argc != 2)
	{
		printf("Usage : %s [Port]\n", argv[0]);
		return 0;
	}

#ifdef WIN32
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		LOG_ERROR("WSAStartup failed with error: %d\n", iResult);
		return 1;
	}
#endif

	// Load config variables
	InitConfig();

#ifndef _DEBUG
	sLogger.SetLogLevelFlags((LogSeverityFlags)0);
#endif

	// Run the server until closed
	unsigned short port = atoi(argv[1]);
	RunTFTPServer(port);

#ifdef WIN32
	WSACleanup();
#endif

	return 0;
}