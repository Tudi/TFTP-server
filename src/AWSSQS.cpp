#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <list>
#include <iostream>
#include "pthread_win.h"
#include "AWSSQS.h"
#ifndef _DISABLE_SQS_MODULE
#include <aws/core/Aws.h>
#include <aws/sqs/SQSClient.h>
#include <aws/sqs/model/SendMessageRequest.h>
#include <aws/sqs/model/SendMessageResult.h>

struct SQS_SocketSessionParams
{
    SOCKET ClientSocket = -1;
    char uuid_str[37];
    char ClientIP[15+1];
    Aws::SQS::SQSClient sqs;
    Aws::SQS::Model::SendMessageRequest sm_req;
	int MessagesQueuedForSend = 0;
	int MessagesSent = 0;
	int MessagesFailedToSend = 0;
};

Aws::SDKOptions options;

// In theory this will contain a single element. Not thread safe !
std::list<SQS_SocketSessionParams*> Sessions;
char* ServerID = NULL;
unsigned short ServerPort = 0;
char* ServerIP = NULL;


void FillLocalIP()
{
    struct ifaddrs* ifAddrStruct = NULL;
    struct ifaddrs* ifa = NULL;
    void* tmpAddrPtr = NULL;

    // Make sure the returned result is always valid in some form
    ServerIP[0] = 0;

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr->sa_family == AF_INET)
        { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            if (((char*)tmpAddrPtr)[0] == 127 || ((char*)tmpAddrPtr)[0] == 0)
                continue;
            //            char ServerIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, ServerIP, INET_ADDRSTRLEN);
            //            printf("'%s': %s\n", ifa->ifa_name, ServerIP); 
            break;
        }
        else if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
        // is a valid IP6 Address
            tmpAddrPtr = &((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
            //            char ServerIP[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, tmpAddrPtr, ServerIP, INET6_ADDRSTRLEN);
            //            printf("'%s': %s\n", ifa->ifa_name, ServerIP); 
            break;
        }
    }
    if (ifAddrStruct != NULL)
        freeifaddrs(ifAddrStruct);//remember to free ifAddrStruct
}

void AWS_SQS_Init(const char* pServerID, const unsigned short pServerPort)
{
	Aws::InitAPI(options);

    ServerID = _strdup(pServerID);
    ServerPort = pServerPort;

    ServerIP = (char*)malloc(INET6_ADDRSTRLEN+1);
    if (ServerIP != NULL)
    {
        FillLocalIP();
    }
}

void AWS_SQS_Destroy()
{
	Aws::ShutdownAPI(options);

    if (ServerID != NULL)
    {
        free(ServerID);
        ServerID = NULL;
    }

    if (ServerIP != NULL)
    {
        free(ServerIP);
        ServerIP = NULL;
    }

    for (auto itr = Sessions.begin(); itr != Sessions.end(); itr++)
        delete (*itr);
    Sessions.clear();
}

SQS_SocketSessionParams* GetCreateSession(SOCKET Socketfd, struct sockaddr_in* client)
{
    // Sanity check
    if (Socketfd == INVALID_SOCKET || client == NULL)
    {
        return NULL;
    }

    // Do we have a session for this socket ?
    for (auto itr = Sessions.begin(); itr != Sessions.end(); itr++)
    {
        if ((*itr)->ClientSocket == Socketfd)
        {
            return (*itr);
        }
    }

    // Create a new element
    SQS_SocketSessionParams* ret = new SQS_SocketSessionParams();
    if (ret == NULL)
    {
        return NULL;
    }

    // We will ID a connection based on the socketfd
    ret->ClientSocket = Socketfd;

    // Generate a session GUID
    create_guid(ret->uuid_str,sizeof(ret->uuid_str));

    // This is static
    const Aws::String queue_url = AWS_QUEUE_URL;
    ret->sm_req.SetQueueUrl(queue_url);

    // Also ID the client not just session based, but IP based ( 1 client can create multiple sessions )
    char* clientIP = inet_ntoa(client->sin_addr);
    strncpy(ret->ClientIP, clientIP, sizeof(ret->ClientIP));

    return ret;
}

void AWS_SQS_SessionClose(SOCKET Socketfd)
{
    for (auto itr = Sessions.begin(); itr != Sessions.end(); itr++)
    {
        if ((*itr)->ClientSocket == Socketfd)
        {
			// should we wait for all messages to be sent ?
            delete (*itr);
            Sessions.erase(itr);
        }
    }
}

void SQS_MessageCallback(const Aws::SQS::SQSClient *client, const Aws::SQS::Model::SendMessageRequest &smr, const Aws::SQS::Model::SendMessageOutcome &sm_out, const std::shared_ptr<const Aws::Client::AsyncCallerContext> &ctx)
{
	int ErrorCount = 0;
	
	// Try to track the amount of errors we get
    for (auto itr = Sessions.begin(); itr != Sessions.end(); itr++)
    {
        if (&(*itr)->sqs == client)
        {
			if (!sm_out.IsSuccess())
			{
				ErrorCount = (*itr)->MessagesFailedToSend;
				(*itr)->MessagesFailedToSend++;
			}
			else
				(*itr)->MessagesSent++;
		}
	}
	
    if (!sm_out.IsSuccess())
    {
        std::cout << ErrorCount << ") Error sending message to " << AWS_QUEUE_URL << ": " << sm_out.GetError().GetMessage() << std::endl;
//        std::cout << "msg :" << msg_body << std::endl;
    }
    else
    {
//        std::cout << "Successfully sent message to " << AWS_QUEUE_URL << std::endl;
    }
}

void AWS_SQS_OnPacketArrived(const char* packet, int size, int Socketfd, struct sockaddr_in* client)
{
    // What is this ?
    if (packet == NULL || size <= 0 || client == NULL)
    {
        return;
    }

    SQS_SocketSessionParams* params = GetCreateSession(Socketfd, client);

    if (params == NULL)
    {
        return;
    }

    size_t EncodedSize = 0;
    char* EncodedMSG = base64_encode((const unsigned char*)packet, size, &EncodedSize);
    if (EncodedMSG == NULL)
    {
        return;
    }
    if (EncodedSize == 0)
    {
        free(EncodedMSG);
        return;
    }

    char* messageFormatted = (char*)malloc(SQS_MESSAGE_BUFFER_SIZE);
    if (messageFormatted == NULL)
    {
        free(EncodedMSG);
        return;
    }

    // Format message to required format
    snprintf(messageFormatted, SQS_MESSAGE_BUFFER_SIZE,
        "{'weather':'','msg':{'session':'%s','cmd':'%s'},'port':%d,'server_ip':'%s','server_id':'%s','cid':'','sensor':'','proto': 'tcp','ip':'%s'}",
        params->uuid_str, EncodedMSG, ServerPort, ServerIP, ServerID, params->ClientIP);

    // Convert the message to amazon string
    Aws::String msg_body = messageFormatted;

    // Do the internal magic so the string can be sent
    params->sm_req.SetMessageBody(msg_body);

    // The actual sending
#define ASYNC_SEND_SQS_MESSAGES
#ifdef ASYNC_SEND_SQS_MESSAGES
	params->sqs.SendMessageAsync(params->sm_req, SQS_MessageCallback);
	params->MessagesQueuedForSend++;
#else	
    auto sm_out = params->sqs.SendMessage(params->sm_req);
    if (!sm_out.IsSuccess())
    {
        std::cout << "Error sending message to " << AWS_QUEUE_URL << ": " << sm_out.GetError().GetMessage() << std::endl;
        std::cout << "msg :" << msg_body << std::endl;
    }
    else
    {
        std::cout << "Successfully sent message to " << AWS_QUEUE_URL << std::endl;
    }
#endif

    // We no longer need the encoded message
    free(EncodedMSG);
    free(messageFormatted);
}
#else
void AWS_SQS_Init(const char* ServerID, const unsigned short ServerPort)
{
}

// Free up resources
void AWS_SQS_Destroy()
{
}

// Every time the TFTP server receives a packet, will callback this function
void AWS_SQS_OnPacketArrived(const char* packet, int size, SOCKET Socketfd, struct sockaddr_in* client)
{
}

// When TFTP server closes a connection, should call this function
void AWS_SQS_SessionClose(SOCKET Socketfd)
{
}
#endif