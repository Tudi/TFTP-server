#include "TFTP.h"
#include "config.h"
#include "Logger.h"
#include "AWSSQS.h"

void RunTFTPServer(unsigned short port);

int main(int argc, char* argv[])
{

	if (argc != 3)
	{
		printf("Usage : %s [Port] [ServerId]\n", argv[0]);
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

#ifndef _DEBUG
	sLogger.SetLogLevelFlags((LogSeverityFlags)0);
#endif

	// Load config variables
	InitConfig();

	// Run the server until closed
	unsigned short port = atoi(argv[1]);

	// Set up the AWS client so we may send messages to AWS
	AWS_SQS_Init(argv[2], port);

	// Run the server : listen / handle to connections
	RunTFTPServer(port);

#ifdef WIN32
	WSACleanup();
#endif

	// Release allocated resources
	AWS_SQS_Destroy();

	return 0;
}