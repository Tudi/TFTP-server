#pragma once

#ifdef WIN32
	#define _CRT_SECURE_NO_WARNINGS
	#include <windows.h>
	#include <stdlib.h>
	#include <stdio.h>
	#include "pthread_win.h"
	#pragma comment (lib, "Ws2_32.lib")
#else
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <sys/types.h>
	#include <netinet/in.h>
	#include <libexplain/bind.h>
	#include <string.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <ctype.h>
	#include "pthread_win.h"
#endif

#define INVALID_SOCKET_2			-1

#define TFTP_HEADER_SIZE			4
#define TFTP_DATA_BLOCK_SIZE		512
#define TFTP_PACKET_FULL_SIZE		(TFTP_HEADER_SIZE+TFTP_DATA_BLOCK_SIZE)
#define TFTP_PACKET_SAFETY_PAD		2
#define TFTP_PACKET_FULL_SIZE_SAFE	(TFTP_PACKET_SAFETY_PAD+TFTP_PACKET_FULL_SIZE)
#define SOCKET_TIMEOUT_SECONDS		1
#define MAX_RETRY_CREATE_SOCKET		100

enum OP_code 
{ 
	OP_RRQ = 1, 
	OP_WRQ = 2, 
	OP_DATA = 3, 
	OP_ACK = 4, 
	OP_ERROR = 5,
	OP_OACK = 6,
};

enum TFTP_ERROR_CODES
{
	TFTP_ERR_UNDEFINED = 0,
	TFTP_ERR_FILE_NOT_FOUND = 1,
	TFTP_ERR_ACCES_VIOLATION = 2,
	TFTP_ERR_DISK_FULL = 3,
	TFTP_ERR_ILLEGAL_OPERATION = 4,
	TFTP_ERR_UNKNOWN_TRANSFER_ID = 5,
	TFTP_ERR_FILE_ALREADY_EXISTS = 6,
	TFTP_ERR_NO_SUCH_USER = 7,
	TFTP_ERR_OPTIONS_NOT_ACCEPTED = 8,
	TFTP_ERR_NO_ERROR = 0xFF,
};

enum TFTP_TransferModes
{
	TFTP_TM_Binary,
	TFTP_TM_Ascii
};

enum TFTP_SessionOperations
{
	TFTP_OP_NOT_INITIALIZED = 0,
	TFTP_OP_CLIENT_TO_SERVER = 1,
	TFTP_OP_SERVER_TO_CLIENT = 2,
};

#define TFTP_OPT_TSIZE		"tsize"
#define TFTP_OPT_TIMEOUT	"timeout"
#define TFTP_OPT_BLKSIZE	"blksize"
#define TFTP_OPT_WINDOSIZE  "windowsize"
#define TFTP_OPT_MCAST		"multicast"

class TFTPSession
{
public:
	TFTPSession();
	~TFTPSession();

	// If file is present, it will opened and operated on
	FILE* hFile = NULL;
	// Once we start an operation, we should not be able to switch from it until finished
	TFTP_SessionOperations SelectedOperation = TFTP_OP_NOT_INITIALIZED; // read or write ?
	// number of bytes sent/received so far
	unsigned long long BytesHandled = 0;
	// Block number we sent or block number we received and we ACK it
//	unsigned int BlocksHandled = 0;
	// Number of blocks that got confirmed
//	unsigned int BlocksACKd = 0;
	// Who are we communicating with
	struct sockaddr Client;
	// We can clean up a session if enough time has passed
	unsigned long long LastActivityStamp = 0;
	// We have a limit how many times we allow a client to ask for retry
	int WindowRetryCount = 0;
	// In case the session contained an error, we can probably clean up the session
	TFTP_ERROR_CODES Error = TFTP_ERROR_CODES::TFTP_ERR_NO_ERROR;
	// Last packet client sent us
	char *LastRecvPacket = NULL;
	// Number of bytes in the packet we received from client
	int LastRecvPktSize = 0;
	// Last packet we sent out. We can resend this packet more than once on failure
//	char *LastSentPacket = NULL;
	// Unless a full packet is sent/received, we can consider the stream finished
//	int LastSentPktSize = 0;
	// When server receives a "large" file, he should do it on a separate port
	SOCKET CommSocket = INVALID_SOCKET_2;
	// Options might ask us to use specific port for communication, check if we can respect it
	unsigned short PortPrefered = 0;
	// When file read is done, we can set this to 1
	int ReadDone = 0;
	// If we are done sending / receiving data, we can close this connection
	int TransferDone = 0;
	// Custom transfer block size
	int BlockSize = TFTP_DATA_BLOCK_SIZE;
	// Timeout proposed by client
	int PacketTimeout = 0;
	// Window size option
	int WindowSize = 1;
	// After we finish sending/receiving a full window, we can increase this by WindowSize
	int FirstBlockInWindow = 0;
	// We can ACK a window if all blocks got received within it( or transfer is done 
	int* BlockNrInWindowReceived = NULL;
	// Only valid if we want to use server listen port as comm port
	SOCKET ServerSocket = INVALID_SOCKET_2;
};

/*
* Parse packet opcode. Based on opcode call packet handler
*/
int ParsePacket(char* packet, int size, TFTPSession* sess);
/*
* ACK a block number received
*/
void SMSG_ACK_Block(TFTPSession* sess, int Resend);
/*
* Send error code to client so he terminates the session
*/
void SMSG_Error(TFTPSession* sess);
/*
* Send a block of data to client
*/
void SMSG_DATA(TFTPSession* sess, int resend);
/*
* Send back negotiated options
*/
void SMSG_OACK(TFTPSession* sess, const char *packet, int size);
/*
* Client is requesting the server to send a file
*/
int CMSG_RRQ(char* packet, int size, TFTPSession* sess);
/*
* Client is ack-ing a block of data we sent to him. This packet may come for various reasons
*/
int CMSG_ACK(char* packet, int size, TFTPSession* sess);
/*
* Client is signalling an error. Probably a timeout or disconnection
*/
int CMSG_ERROR(char* packet, int size, TFTPSession* sess);
/*
* Client is requesting the server to store a file
*/
int CMSG_WRQ(char* packet, int size, TFTPSession* sess);
/*
* Client is sending a chunk of the file to be sent
*/
int CMSG__DATA(char* packet, int size, TFTPSession* sess);
