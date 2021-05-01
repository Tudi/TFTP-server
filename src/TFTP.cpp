#include "TFTP.h"
#include <list>
#include "AWSSQS.h"
#include "config.h"
#include "Logger.h"

#ifndef MIN
	#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

// Next port we will pick(if available) when we create a new comm connection. 
unsigned short NextPortToUse = sConf.CommPortStart;
// This should be something that can be set by an event ( wait for keypress / button press ... )
int ServerIsRunning = 1;
// Count the number of active connections so we may limit new connections based on server setting
volatile int TransferCounter = 0;
// Sometimes new session requests may come as duplicats. Need to filter out Duplicats
struct sockaddr **ActiveClientList = NULL;

int CreateUDPSocket(SOCKET *sockfd, unsigned short port)
{
	struct sockaddr_in server;
	int err;
	*sockfd = socket( AF_INET, SOCK_DGRAM, 0);
	if(*sockfd == INVALID_SOCKET_2)
	{
		// could not create socket
		LOG_ERROR("Could not create socket\n");
		return -1;
	}

	// we need to check when a session action timed out. For this we will periodically recheck session statuses
	err = SetSocketTimeOut(*sockfd, SOCKET_TIMEOUT_SECONDS);
	if (err != 0)
	{
		LOG_ERROR("Socket error %d while setting timeout option\n", err);
	}
	int True = 1;
	err = setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&True, sizeof True);
	if (err != 0)
	{
		LOG_ERROR("Socket error %d while setting reuse addr option\n", err);
	}

	memset( &server, 0, sizeof( server ) );
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl( INADDR_ANY );
	server.sin_port = htons(port);
	err = bind( *sockfd, (struct sockaddr *) &server, sizeof(server) );
	if (err != 0)
	{
		LOG_ERROR("Socket error %d while binding to port %d\n", err, port);
#ifndef WIN32
		LOG_ERROR("Bind error message : %s\n", explain_bind(*sockfd, (struct sockaddr*)&server, sizeof(server)));
#endif
	}

	LOG_TRACE("Created socket to listen on %d\n", port);
	// should be zero
	return err;
}

int CreateCommUDPSocket(TFTPSession *sess)
{
	struct sockaddr_in server;
	SOCKET* sockfd = &sess->CommSocket;
	int err;
	int RetryCount = 0;

	// Do we reuse server listen socket for communication ?
	if (sess->ServerSocket != INVALID_SOCKET_2)
	{
		sess->CommSocket = sess->ServerSocket;
		sess->ServerSocket = INVALID_SOCKET_2;

		LOG_TRACE("Going to reuse server port to comm with client\n");
		err = connect(sess->CommSocket, (struct sockaddr*)&sess->Client, sizeof(sess->Client));
		if (err != 0)
		{
			closesocket(*sockfd);
		}

		return err;
	}

	// Search for a port that could be used for communication with client
	do {

		*sockfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (*sockfd == INVALID_SOCKET_2)
		{
			// could not create socket
			LOG_ERROR("Could not create socket\n");
			return -1;
		}
		err = SetSocketTimeOut(*sockfd, SOCKET_TIMEOUT_SECONDS);
		if (err != 0)
		{
			LOG_ERROR("Socket error %d while setting timeout option\n", err);
		}

		// Try to listen on next picked "random" port
		memset(&server, 0, sizeof(server));
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = htonl(INADDR_ANY);
		if(sess->PortPrefered)
			server.sin_port = htons(sess->PortPrefered);
		else
			server.sin_port = htons(NextPortToUse);
		err = bind(*sockfd, (struct sockaddr*)&server, sizeof(server));
		if (err != 0)
		{
#ifndef WIN32
			LOG_ERROR("Bind error message : %s\n", explain_bind(*sockfd, (struct sockaddr*)&server, sizeof(server)));
#endif
			closesocket(*sockfd);
		}
		else if(sess->Client.sa_family != AF_MAX) // We do not need to connect connection listener socket
		{
			// Try to connect this random port to "client"
			err = connect(sess->CommSocket, (struct sockaddr*)&sess->Client, sizeof(sess->Client));
			if (err != 0)
			{
				closesocket(*sockfd);
			}
		}

		if (sess->PortPrefered)
		{
			if (err != 0)
			{
				// Could not respect requirement to use specific port to listen on
				sess->PortPrefered = 0;
			}
		}
		else
		{
			// Increase port number we will try next
			NextPortToUse = (NextPortToUse + 1) % sConf.CommPortEnd;
			if (NextPortToUse < sConf.CommPortStart)
			{
				NextPortToUse = sConf.CommPortStart;
			}
		}

		// Avoid deadlocks
		RetryCount++;
	} while (err != 0 && RetryCount < MAX_RETRY_CREATE_SOCKET);

	// Should be zero
	return err;
}

#ifdef WIN32
void ThreadDirectComm(void *pThreadArgs)
#else
void* ThreadDirectComm(void* pThreadArgs)
#endif
{
	TFTPSession* sess = (TFTPSession*)pThreadArgs;

	ActiveClientList[TransferCounter] = &sess->Client;
	TransferCounter++;

	LOG_TRACE("Creating new session : received %d bytes from client\n", sess->LastRecvPktSize);

	// Mark this session as it's have been used
	sess->LastActivityStamp = getTick();

	int ClientHasActivity = 1;
	while(ClientHasActivity != 0 && ServerIsRunning == 1)
	{
		// Make the next move in the session
		if (sess->LastRecvPktSize > 0)
		{
			int err = ParsePacket(sess->LastRecvPacket, sess->LastRecvPktSize, sess);
		}

		// callback to AWS backend
		AWS_SQS_OnPacketArrived(sess->LastRecvPacket, sess->LastRecvPktSize, sess->CommSocket, (struct sockaddr_in* )&sess->Client);

		// Nothing to do without a valid comm channel
		if (sess->CommSocket == INVALID_SOCKET_2)
		{
			break;
		}

		// If there is an error, no need to continue
		if (sess->Error != TFTP_ERR_NO_ERROR)
		{
			SMSG_Error(sess);
			break;
		}

		// Empty packet is a signal that we are done with this client
		if (sess->TransferDone == 1)
		{
			LOG_TRACE("Transfer done for IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
			break;
		}

		// Wait for client next move
		sess->LastRecvPktSize = recv(sess->CommSocket, sess->LastRecvPacket, sess->BlockSize + TFTP_HEADER_SIZE, 0);

		// Socket wait timed out. Check if we should kill this connection
		if (sess->LastRecvPktSize <= 0)
		{
			// How much time passed since we heard from this socket ?
			__int64 TimePassed = getTick() - sess->LastActivityStamp;

			// Too much time passed since we last heard of him
			if (TimePassed >= sess->PacketTimeout * 1000)
			{
				sess->WindowRetryCount++;

				// Too many retries and still no sign
				if (sess->WindowRetryCount >= sConf.RetryResendPacketMax)
				{
					LOG_TRACE("Client session timed out for IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
					ClientHasActivity = 0;
					break;
				}

				// Try to resend the same block
				if (sess->SelectedOperation == TFTP_OP_SERVER_TO_CLIENT)
				{
					LOG_TRACE("Client ACK timed out, resending DATA to IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
					SMSG_DATA(sess, 1);
				}
				// Signal to the client that he should resend the last chunk
				// This is not required as in UDP packets might duplicate
				else if (sess->SelectedOperation == TFTP_OP_CLIENT_TO_SERVER)
				{
					LOG_TRACE("Client DATA timed out, resending ACK to IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
					SMSG_ACK_Block(sess, 1);
				}
			}
		}
	};

	TransferCounter--;
	ActiveClientList[TransferCounter] = NULL;

	// Clean up the session and consider it disconnected
	delete sess;

#ifndef WIN32
	return NULL;
#endif
}

int IsDuplicateClientConnectionRequest(struct sockaddr *NewClient)
{
	for (int i = 0; i < TransferCounter; i++)
	{
		if (ActiveClientList[i] != NULL
			&& memcmp(NewClient->sa_data, ActiveClientList[i]->sa_data, sizeof(NewClient->sa_data)) == 0)
		{
			LOG_TRACE("Duplicate session request from IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)NewClient)->sin_addr), ((struct sockaddr_in*)NewClient)->sin_port);
			return 1;
		}
	}
	return 0;
}

void RunTFTPServer(unsigned short port)
{
	printf("Server listening on port: %d\n", port);

	// Create a list of active connections so we may detect duplicate session requests
	ActiveClientList = (struct sockaddr**)malloc((sConf.ParallelTransfersMax + 1) * sizeof(struct sockaddr*));
	if (ActiveClientList == NULL)
	{
		LOG_ERROR("Could not allocate memory");
		return;
	}

	// Create an UDP socket we will listen on
	SOCKET sockfd;
	CreateUDPSocket(&sockfd, port);

	// Loop while transmitting
	TFTPSession* session = new TFTPSession;
	socklen_t len = sizeof(session->Client);
	while (ServerIsRunning == 1)
	{
		// Get the next packet from any specific client
		session->LastRecvPktSize = recvfrom(sockfd, session->LastRecvPacket, session->BlockSize + TFTP_HEADER_SIZE, 0, (struct sockaddr*)&session->Client, &len);

		unsigned short OP_code = ntohs(*(unsigned short*)&session->LastRecvPacket[0]);

		if (session->LastRecvPktSize > 0
			&& TransferCounter < sConf.ParallelTransfersMax
			&& (OP_code == OP_RRQ || (OP_code == OP_WRQ)
			&& IsDuplicateClientConnectionRequest(&session->Client) == 0	)
			)
		{
			// Run the session handler in this thread and stop listening to new connections
			if (sConf.ReuseServerPortForComm != 0)
			{
				session->ServerSocket = sockfd;
				ThreadDirectComm(session);
				// create a new socket we will listen on
				CreateUDPSocket(&sockfd, port);
			}
			else
			{
				// Create a new thread to handle this new communication
				pthread_t handle;
				pthread_create(&handle, NULL, ThreadDirectComm, session);
			}

			// Create a new session to handle next incomming data
			session = new TFTPSession;
		}
	}

	// Shoudl wait until all worker threads close
	free(ActiveClientList);
	ActiveClientList = NULL;
	closesocket(sockfd);
	AWS_SQS_SessionClose(sockfd);
}

#define EnsureHaveEnoughPacketBytes(packetSize, needSize, sess) { \
	if (packetSize < needSize) \
	{ \
		sess->Error = TFTP_ERR_ILLEGAL_OPERATION; \
		LOG_ERROR("%d Client packet too small. Need %d, have %d for IP %s:%d\n", __LINE__, needSize, packetSize, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port); \
		return -1; \
	}}

// In normal circumstances this should be done with packet handlers, but for only 5 packets, it's not worth the effort
int ParsePacket(char* packet, int size, TFTPSession* sess)
{
	// Did we receive enough bytes for opcode ?
	int bytesRead = 0;

	EnsureHaveEnoughPacketBytes(size, bytesRead + 2, sess)

	unsigned short OP_code = ntohs(*(unsigned short*)&packet[bytesRead]);
	bytesRead += 2;

	// based on opcode, inspect packet
	switch (OP_code)
	{
	case OP_code::OP_RRQ:
	{
		return CMSG_RRQ(packet, size, sess);
	}break;
	case OP_code::OP_WRQ:
	{
		return CMSG_WRQ(packet, size, sess);
	}break;
	case OP_code::OP_ACK:
	{
		return CMSG_ACK(packet, size, sess);
	}break;
	case OP_code::OP_DATA:
	{
		return CMSG__DATA(packet, size, sess);
	}break;
	case OP_code::OP_ERROR:
	{
		return CMSG_ERROR(packet, size, sess);
	}break;
	default:
		sess->Error = TFTP_ERR_UNDEFINED;
		break;
	}
	return 0;
}

int ParseOptions(TFTPSession* sess, char *packet, int size, size_t & bytesRead)
{
	// Parse options ( if there are any )
	char OackContent[TFTP_PACKET_FULL_SIZE_SAFE];
	int OackBytesAdded = 2;
	while (bytesRead < size)
	{
		EnsureHaveEnoughPacketBytes(size, bytesRead + 1, sess)
		const char* OptionName = &packet[bytesRead];
		bytesRead += (int)strlen(OptionName) + 1;

		EnsureHaveEnoughPacketBytes(size, bytesRead + 1, sess)
		const char* OptionValue = &packet[bytesRead];
		bytesRead += (int)strlen(OptionValue) + 1;

		LOG_TRACE("option %s=%s from IP %s:%d\n", OptionName, OptionValue, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

		if ( sConf.AllowTSizeOption == 1 && __stricmp(TFTP_OPT_TSIZE, OptionName) == 0)
		{
			// Gives a chance for early abort for Disk Full error
			int ProposedSize = atoi(OptionValue);
			if (ProposedSize == 0 && sess->hFile != NULL)
			{
				// Get the file size we are going to send
				fseek(sess->hFile, 0L, SEEK_END);
				ProposedSize = ftell(sess->hFile);
				fseek(sess->hFile, 0L, SEEK_SET);
				LOG_TRACE("changed option %s=%d from IP %s:%d\n", OptionName, ProposedSize, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
			}
			OackBytesAdded += snprintf(&OackContent[OackBytesAdded], sizeof(OackContent) - OackBytesAdded, "%s%c%d", OptionName, 0, ProposedSize);
		}
		else if (__stricmp(TFTP_OPT_TIMEOUT, OptionName) == 0)
		{
			int ProposedSize = atoi(OptionValue);
			if (ProposedSize >= 1 && ProposedSize <= 255)
			{
				if (ProposedSize < sConf.ClientTimeoutSecMin)
					ProposedSize = sConf.ClientTimeoutSecMin;
				if (ProposedSize > sConf.ClientTimeoutSecMax)
					ProposedSize = sConf.ClientTimeoutSecMax;
				sess->PacketTimeout = ProposedSize;
				OackBytesAdded += snprintf(&OackContent[OackBytesAdded], sizeof(OackContent) - OackBytesAdded, "%s%c%d", OptionName, 0, sess->PacketTimeout);
			}
		}
		else if (__stricmp(TFTP_OPT_BLKSIZE, OptionName) == 0)
		{
			int ProposedSize = atoi(OptionValue);
			if (ProposedSize >= 8 && ProposedSize <= 65464)
			{
				sess->BlockSize = ProposedSize;
				// Make sure we have enough buffer to store both custom block size and custom window size
				sess->LastRecvPacket = (char*)realloc(sess->LastRecvPacket, sess->BlockSize + TFTP_HEADER_SIZE + TFTP_PACKET_SAFETY_PAD); // 4 bytes header, 2 bytes as null terminator
				OackBytesAdded += snprintf(&OackContent[OackBytesAdded], sizeof(OackContent) - OackBytesAdded, "%s%c%d", OptionName, 0, sess->BlockSize);
			}
		}
		else if (__stricmp(TFTP_OPT_WINDOSIZE, OptionName) == 0)
		{
			int ProposedSize = atoi(OptionValue);
			if (ProposedSize >= 1 
				&& ProposedSize <= 65535
				&& ProposedSize >= sConf.WindowSizeMin // we can not increase proposed window size, only decrease it. We can only refuse too small values
				)
			{
				if (ProposedSize > sConf.WindowSizeMax)
					ProposedSize = sConf.WindowSizeMax;
				sess->WindowSize = ProposedSize;
				OackBytesAdded += snprintf(&OackContent[OackBytesAdded], sizeof(OackContent) - OackBytesAdded, "%s%c%d", OptionName, 0, sess->WindowSize);
			}
		}
		else if (__stricmp(TFTP_OPT_MCAST, OptionName) == 0)
		{
			const char* MultiCastOptions = OptionValue;
			// Make sure the format as is as expected, we are looking for a comma after IP + comma after port
		}
	}
	if (OackBytesAdded != 2)
	{
		SMSG_OACK(sess, OackContent, OackBytesAdded);
	}

	return 0;
}

TFTPSession::TFTPSession()
{
	// Mark client as invalid
	Client.sa_family = AF_MAX;

	// memory to store incomming packets
	LastRecvPacket = (char*)malloc(BlockSize + TFTP_HEADER_SIZE + TFTP_PACKET_SAFETY_PAD);

	PacketTimeout = sConf.ClientTimeoutSecMin;

	// Unless requested by client, this can not be changed server side alone
	WindowSize = 1;
}

TFTPSession::~TFTPSession()
{
	LOG_TRACE("Removed client session for IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&Client)->sin_addr), ((struct sockaddr_in*)&Client)->sin_port);

	if (LastRecvPacket != NULL)
	{
		free(LastRecvPacket);
		LastRecvPacket = NULL;
	}

	// Close file we read / write to
	if (hFile != NULL)
	{
		fclose(hFile);
		hFile = NULL;
	}

	// Close comm socket
	if (CommSocket != INVALID_SOCKET_2)
	{
		AWS_SQS_SessionClose(CommSocket);
		closesocket(CommSocket);
		CommSocket = INVALID_SOCKET_2;
	}

	if (BlockNrInWindowReceived != NULL)
	{
		free(BlockNrInWindowReceived);
		BlockNrInWindowReceived = NULL;
	}
}

void SMSG_ACK_Block(TFTPSession* sess, int Resend)
{
	// First ACK is special, does not need to ack an actual received packet
	int BlocksReceived = 0;
	for (int i = 0; i < sess->WindowSize; i++)
	{
		if (sess->BlockNrInWindowReceived[i] > 0)
		{
			BlocksReceived++;
			continue;
		}
		break;
	}

	unsigned short BlockNrToACK = sess->FirstBlockInWindow + BlocksReceived;

	LOG_TRACE("Sending ACK block %d to IP %s:%d\n", BlockNrToACK, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

	char packet[TFTP_PACKET_FULL_SIZE];

	*(unsigned short*)&packet[0] = htons(OP_code::OP_ACK);
	*(unsigned short*)&packet[2] = htons(BlockNrToACK);

	int BytesSent = send(sess->CommSocket, packet, 4, 0);
	if (BytesSent != 4)
	{
		LOG_ERROR("only managed to send %d bytes out of %d\n", BytesSent, 4);
		sess->Error = TFTP_ERR_UNDEFINED;
		return;
	}

	// We can receive only blocks larger than currently ACKd window
	sess->FirstBlockInWindow = BlockNrToACK;

	// Reset states for packets in the window
	for (int i = 0; i < sess->WindowSize; i++)
	{
		sess->BlockNrInWindowReceived[i] = 0;
	}

	// Conside the socket status refreshed
	sess->LastActivityStamp = getTick();
}

void SMSG_OACK(TFTPSession* sess, const char* packet, int size)
{
	LOG_TRACE("Sending OACK to IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

	*(unsigned short*)&packet[0] = htons(OP_code::OP_OACK);

	int BytesSent = send(sess->CommSocket, packet, size, 0);
	if (BytesSent != size)
	{
		LOG_ERROR("only managed to send %d bytes out of %d\n", BytesSent, size);
	}
}

void SMSG_DATA(TFTPSession* sess, int resend)
{
	// Sanity check
	if (sess->hFile == NULL)
	{
		return;
	}

	// Rewind the file pointer to the correct position to read
	// When sending data, blocks start from 1 instead of 0
	fseek(sess->hFile, (sess->FirstBlockInWindow-1) * sess->BlockSize, SEEK_SET);

	char *packet = (char*)malloc(sess->BlockSize + TFTP_HEADER_SIZE + TFTP_PACKET_SAFETY_PAD);
	if (packet == NULL)
	{
		LOG_ERROR("Allocation error for size %d\n", sess->BlockSize + TFTP_HEADER_SIZE + TFTP_PACKET_SAFETY_PAD);
		sess->Error = TFTP_ERR_UNDEFINED;
		return;
	}

	// Send multiple blocks of data from file
	for (int i = 0; i < sess->WindowSize; i++)
	{

		// Read bytes from file
		size_t read_count_now = fread(&packet[TFTP_HEADER_SIZE], 1, sess->BlockSize, sess->hFile);

		// End of file reached
		if (read_count_now < sess->BlockSize)
		{
			LOG_TRACE("Done reading data for IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
			sess->ReadDone = 1;
		}

		// Set header data
		*(unsigned short*)&packet[0] = htons(OP_code::OP_DATA);
		*(unsigned short*)&packet[2] = htons(sess->FirstBlockInWindow + i);

		// Send block of data
		size_t ThisPacketSize = read_count_now + TFTP_HEADER_SIZE;
		int BytesSent = send(sess->CommSocket, &packet[0], (int)ThisPacketSize, 0);

		// Unexpected disconnection
		if (BytesSent != ThisPacketSize)
		{
			LOG_ERROR("only managed to send %d bytes out of %d\n", BytesSent, ThisPacketSize);
			sess->Error = TFTP_ERR_UNDEFINED;
			free(packet);
			return;
		}

		LOG_TRACE("Sending DATA block %d to IP %s:%d\n", sess->FirstBlockInWindow + i, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

		// This for the sake of tests. Just ignore this feature
		sess->BytesHandled += read_count_now;
		if (sess->BytesHandled > sConf.KillConnectionAfterxBytesReceived && sConf.KillConnectionAfterxBytesReceived != 0)
		{
			sess->Error = TFTP_ERR_DISK_FULL;
			LOG_TRACE("Session sent too much DATA from IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
			free(packet);
			return;
		}

		// No need to read more
		if (read_count_now < sess->BlockSize)
		{
			break;
		}
	}
	free(packet);

	// Mark this packet as have not been resent yet
	sess->WindowRetryCount = 0;

	// Conside the socket status refreshed
	sess->LastActivityStamp = getTick();
}

void SMSG_Error(TFTPSession* sess)
{
	char packet[TFTP_PACKET_FULL_SIZE];
	char err_msg[TFTP_PACKET_FULL_SIZE];

	if (sess->Error == TFTP_ERR_FILE_NOT_FOUND)
	{
		_strncpy(err_msg, sizeof(err_msg), "ERROR: File name and/or directory could not be resolved");
	}
	else if (sess->Error == TFTP_ERR_DISK_FULL)
	{
		sess->Error = TFTP_ERR_UNDEFINED;
		_strncpy(err_msg, sizeof(err_msg), "ERROR: Uploading files is not supported on this TFTP Server");
	}
	else
	{
		_strncpy(err_msg, sizeof(err_msg), "ERROR");
	}

	size_t packet_size = 0;

	// Creating header for error packet
	*(unsigned short*)&packet[0] = htons(OP_code::OP_ERROR);
	*(unsigned short*)&packet[2] = htons(sess->Error);

	// Writing error message into appropriate index
	unsigned int i = 4;
	for (; err_msg[i - 4] != '\0'; i++)
	{
		packet[i] = err_msg[i - 4];
	}

	// NULL terminate string
	packet[i] = 0;

	packet_size = i + 1;

	// Sends error packet to client, then terminates
	size_t BytesSent = send(sess->CommSocket, packet, (int)packet_size, 0);
	if (BytesSent != packet_size)
	{
		LOG_ERROR("only managed to send %d bytes out of %d\n", BytesSent, packet_size);
	}

	// Conside the socket status refreshed
	sess->LastActivityStamp = getTick();

	LOG_TRACE("Sending ERROR %d - %s to IP %s:%d\n", sess->Error, err_msg, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
}

int CMSG_RRQ(char* packet, int size, TFTPSession* sess)
{
	size_t bytesRead = 2; // 2 bytes are the opcode we already read

	LOG_TRACE("Client sent us a RRQ from IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

	// Starting a new s->c session
	if (sess->SelectedOperation != TFTP_OP_NOT_INITIALIZED)
	{
		LOG_TRACE("Duplicate RRQ from IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
//		sess->Error = TFTP_ERR_ILLEGAL_OPERATION;
		return -1;
	}

	// Make sure last string is always 0 terminated
	packet[size] = 0;

	EnsureHaveEnoughPacketBytes(size, bytesRead + 1, sess)

	// Get the file name server should send to the client
	const char* FileName = &packet[bytesRead];
	size_t FileNameSize = strlen(FileName);
	bytesRead += FileNameSize + 1;

	LOG_TRACE("RRQ filename %s from IP %s:%d\n", FileName, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

	EnsureHaveEnoughPacketBytes(size, bytesRead + 1, sess)

	// We will send/recv 8 bits of data. Can ignore mode even if it's depracated mode
	const char* Mode = &packet[bytesRead];
	size_t ModeSize = strlen(Mode);
	bytesRead += ModeSize + 1;

	// create a temp socket we will use for communication
	int err = CreateCommUDPSocket(sess);
	if (err != 0)
	{
		LOG_ERROR("Could not create socket to transfer data on\n");
	}

	LOG_TRACE("RRQ mode %s from IP %s:%d\n", Mode, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

	// Check if we are allowed to server this file
	int FileIsAllowed = 0;
	for (auto itr = sConf.DownloadableFileNames.begin(); itr != sConf.DownloadableFileNames.end(); itr++)
	{
		if (__stricmp(FileName, (*itr)) == 0)
		{
			FileIsAllowed = 1;
			break;
		}
	}
	if (FileIsAllowed == 0)
	{
		sess->Error = TFTP_ERR_FILE_NOT_FOUND;
		LOG_TRACE("File %s is not available for downloading by IP %s:%d\n", FileName, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		return -1;
	}

	// Can we actually open the file ? Maybe someone else is already streaming into it
	errno_t er = fopen_s(&sess->hFile, FileName, "rb");
	if (sess->hFile == NULL)
	{
		sess->Error = TFTP_ERR_FILE_NOT_FOUND;
		LOG_TRACE("File %s is locked for reading by IP %s:%d\n", FileName, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		return -1;
	}

	// Handle TFTP RFC extensions
	ParseOptions(sess, packet, size, bytesRead);

	if (bytesRead != size)
	{
		LOG_INFO("RRQ packet not consumed, remaining %d bytes from IP %s:%d\n", size - bytesRead, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
	}

	// We have enough data to set the session state and start sending data
	sess->SelectedOperation = TFTP_OP_SERVER_TO_CLIENT;

	// When we are sending block count starts at 1
	sess->FirstBlockInWindow = 1;

	// Send the first block of data
	SMSG_DATA(sess, 0);

	// All went good if we got here
	return 0;
}

int CMSG_ACK(char* packet, int size, TFTPSession* sess)
{
	// we need an active session to be able to receive an ACK
	if (sess->SelectedOperation != TFTP_OP_SERVER_TO_CLIENT)
	{
		LOG_TRACE("Session %d is not in a state to receive ACK from IP %s:%d\n", sess->SelectedOperation, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		sess->Error = TFTP_ERR_ILLEGAL_OPERATION;
		return -1;
	}

	EnsureHaveEnoughPacketBytes(size, 4, sess)

	// Get the block number from the ACK packet
	unsigned short ACKBlockNr = ntohs(*(unsigned short*)&packet[2]);

	if (4 != size)
	{
		LOG_INFO("ACK packet not consumed, remaining %d bytes from IP %s:%d\n", size - 4, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
	}

	// echoed block number is not the same as the one we sent out ?
	int ExpectedACKBlockNr = sess->FirstBlockInWindow + sess->WindowSize - 1;
	if (ACKBlockNr < sess->FirstBlockInWindow || ACKBlockNr > ExpectedACKBlockNr)
	{
		// This is not an error as UDP packets may duplicate on network
//			sess->Error = TFTP_ERR_ACCES_VIOLATION;
		LOG_TRACE("Unexpected ACK %d instead %d from IP %s:%d\n", ACKBlockNr, ExpectedACKBlockNr, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		return -1;
	}

	// Move the beginning of the window to the location after the block that got ACKd
	sess->FirstBlockInWindow = ACKBlockNr + 1;

	LOG_TRACE("Client sent us ACK for block %d, from IP %s:%d\n", ACKBlockNr, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

	// we received ACK for a packet that was not full
	if (sess->ReadDone == 1)
	{
		sess->TransferDone = 1;
		LOG_TRACE("Transfer done for IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
	}
	else
	{
		// Read more data to be sent
		SMSG_DATA(sess, 0);
	}

	return 0;
}

int CMSG_ERROR(char* packet, int size, TFTPSession* sess)
{
	size_t bytesRead = 2; // 2 bytes are the opcode we already read

	EnsureHaveEnoughPacketBytes(size, bytesRead + 2, sess)

	unsigned short ErrorCode = ntohs(*(unsigned short*)&packet[2]);
	bytesRead += 2;

	EnsureHaveEnoughPacketBytes(size, bytesRead + 1, sess)

	const char* ErrorMessage = &packet[4];
	bytesRead += strlen(ErrorMessage) + 1;

	sess->Error = (TFTP_ERROR_CODES)ErrorCode;

	if (bytesRead != size)
	{
		LOG_INFO("ERROR packet not consumed, remaining %d bytes from IP %s:%d\n", size - bytesRead, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
	}

	LOG_TRACE("Client sent us ERROR %d - %s, from IP %s:%d\n", ErrorCode, ErrorMessage, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

	return 0;
}

int CMSG_WRQ(char* packet, int size, TFTPSession* sess)
{
	size_t bytesRead = 2; // 2 bytes are the opcode we already read

	// Starting a new s->c session
	if (sess->SelectedOperation != TFTP_OP_NOT_INITIALIZED)
	{
		LOG_TRACE("Session %d is not in a state to receive WRQ from IP %s:%d\n", sess->SelectedOperation, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		return -1;
	}

	// Make sure last string is always 0 terminated
	packet[size] = 0;

	EnsureHaveEnoughPacketBytes(size, bytesRead + 1, sess)

	// Get the file name server should send to the client
	const char* FileName = &packet[bytesRead];
	size_t FileNameSize = strlen(FileName);
	bytesRead += FileNameSize + 1;

	LOG_TRACE("WRQ filename %s from IP %s:%d\n", FileName, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

	EnsureHaveEnoughPacketBytes(size, bytesRead + 1, sess)

	// We will send/recv 8 bits of data. Can ignore mode even if it's depracated mode
	const char* Mode = &packet[bytesRead];
	size_t ModeSize = strlen(Mode);
	bytesRead += ModeSize + 1;

	LOG_TRACE("WRQ mode %s from IP %s:%d\n", Mode, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

	// create a temp socket we will use for communication
	int err = CreateCommUDPSocket(sess);
	if (err != 0)
	{
		LOG_ERROR("Could not create socket to transfer data on\n");
	}

	// Check if we are allowed to server this file
	int FileIsAllowed = 0;
	for (auto itr = sConf.UploadableFileNames.begin(); itr != sConf.UploadableFileNames.end(); itr++)
	{
		if (strcmp(FileName, (*itr)) == 0)
		{
			FileIsAllowed = 1;
			break;
		}
	}
	if (FileIsAllowed == 0)
	{
		sess->Error = TFTP_ERR_FILE_NOT_FOUND;
		LOG_TRACE("File %s is not available for uploading by IP %s:%d\n", FileName, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		return -1;
	}

	// TFTP server in relay mode will redirect incomming data to some other network location
	if (sConf.NoFileWriteOnServer == 0)
	{
		// Can we actually open the file 
		errno_t er = fopen_s(&sess->hFile, FileName, "wb");
		if (sess->hFile == NULL)
		{
			sess->Error = TFTP_ERR_FILE_NOT_FOUND;
			LOG_TRACE("File %s is locked for writing by IP %s:%d\n", FileName, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
			return -1;
		}
	}

	// Handle TFTP RFC extensions
	ParseOptions(sess, packet, size, bytesRead);

	if (bytesRead != size)
	{
		LOG_INFO("WRQ packet not consumed, remaining %d bytes from IP %s:%d\n", size - bytesRead, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
	}
	// We have enough data to set the session state and start sending data
	sess->SelectedOperation = TFTP_OP_CLIENT_TO_SERVER;

	// Need to know if every block in the window got received
	sess->BlockNrInWindowReceived = (int*)malloc(sess->WindowSize * sizeof(int));
	// No blocks have been received from window of data
	memset(sess->BlockNrInWindowReceived, 0, sess->WindowSize * sizeof(int));

	// This will be overwritten when we send the first ACK
	sess->FirstBlockInWindow = 0;

	// Sending an ACK packet should signal the client he can start sending data
	SMSG_ACK_Block(sess, 1);

	// All went good if we got here
	return 0;
}

int CMSG__DATA(char* packet, int size, TFTPSession* sess)
{
	// we need an active session to be able to receive an ACK
	if (sess->SelectedOperation != TFTP_OP_CLIENT_TO_SERVER)
	{
		sess->Error = TFTP_ERR_ILLEGAL_OPERATION;
		LOG_TRACE("Session %d is not in a state to receive DATA from IP %s:%d\n", sess->SelectedOperation, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		return -1;
	}

	EnsureHaveEnoughPacketBytes(size, 4, sess)

	// Always count the number of bytes we receive to avoid "hacks"
	sess->BytesHandled += size;
	if(sess->BytesHandled > sConf.KillConnectionAfterxBytesReceived && sConf.KillConnectionAfterxBytesReceived != 0)
	{
		sess->Error = TFTP_ERR_DISK_FULL;
		LOG_TRACE("Session received too much DATA from IP %s:%d\n", inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		return -1;
	}

	unsigned short BlockNr = ntohs(*(unsigned short*)&packet[2]);

	if(BlockNr <= sess->FirstBlockInWindow)
	{
		LOG_TRACE("DATA packet block number %d is less than window start %d from IP %s:%d\n", BlockNr, sess->FirstBlockInWindow, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		return -1;
	}

	// If window size is more than 1, we might receive out of order blocks
	int BlockIndexInWindow = (BlockNr-1) % sess->WindowSize;

	// Is this a duplicate or maybe "lost" packet ?
	if (sess->BlockNrInWindowReceived[BlockIndexInWindow] != 0)
	{
		LOG_TRACE("DATA packet block number %d already stored from IP %s:%d\n", BlockNr, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);
		return -1;
	}

	// We consider the block as received from client
	sess->BlockNrInWindowReceived[BlockIndexInWindow] = size - TFTP_HEADER_SIZE;

	// Last block will have size 0 to signal us end of the transmission
	if (size > TFTP_HEADER_SIZE)
	{
		// If we have a valid output file, save the data block
		if (sess->hFile != NULL)
		{
			// packets may come out of order when window size is greater than 1
			if (sess->WindowSize != 1)
			{
				fseek(sess->hFile, (BlockNr-1)*sess->BlockSize, SEEK_SET);
			}
			// Write data to file
			fwrite(&packet[TFTP_HEADER_SIZE], 1, size - TFTP_HEADER_SIZE, sess->hFile);
		}

		LOG_TRACE("Stored DATA block %d from IP %s:%d\n", BlockNr, inet_ntoa(((struct sockaddr_in*)&sess->Client)->sin_addr), ((struct sockaddr_in*)&sess->Client)->sin_port);

		// Client sent us data. Reset retry counter
		sess->WindowRetryCount = 0;
	}
	
	if(size < sess->BlockSize + TFTP_HEADER_SIZE)
	{
		// we are done receiving data from client
		sess->TransferDone = 1;
	}

	// Tell the client we received he's data block, so he may send a new one
	SMSG_ACK_Block(sess, 0);

	return 0;
}