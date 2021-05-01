#pragma once

// The queue should already exist. Application will not create one if not existing
#define AWS_QUEUE_URL "https://sqs.us-east-2.amazonaws.com/507620807254/DaveHudson"
// Max packet size we are allowed to generate to be sent to AWS as an SQS message
#define SQS_MESSAGE_BUFFER_SIZE 15*1024*1024

// Set up internal states
void AWS_SQS_Init(const char *ServerID, const unsigned short ServerPort);

// Free up resources
void AWS_SQS_Destroy();

// Every time the TFTP server receives a packet, will callback this function
void AWS_SQS_OnPacketArrived(const char *packet, int size, SOCKET Socketfd, struct sockaddr_in *client);

// When TFTP server closes a connection, should call this function
void AWS_SQS_SessionClose(SOCKET Socketfd);