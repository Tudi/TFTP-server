#pragma once

#include <list>

void InitConfig();

struct AppConfig
{
	// Kill the connection if we receive more bytes than this
	int	KillConnectionAfterxBytesReceived = 15 * 1025 * 1024;
	// Kill the connection if we receive more bytes than this
	int	KillConnectionAfterxBytesSent = 15 * 1025 * 1024;
	// List of file names we are allowed to share for download requests
	std::list<char*>	DownloadableFileNames;
	// List of files names we are allowed to share for upload requests
	std::list<char*>	UploadableFileNames;
	// If client will not ACK our action, we retry last action. After N retries we disconnect
	int		RetryResendPacketMax = 4;
	// Min Port number we can use to create a connection to the client
	int		CommPortStart = 49152;
	// Max Port number we can use to create a connection to the client
	int		CommPortEnd = 65535;
	// Maximum number of transfers that can happen in parallel
	int		ParallelTransfersMax = 1;
	// Allow server to trasmit FileSize to client if asked ?
	int		AllowTSizeOption = 1;
	// Client may request a specific timeout
	int		ClientTimeoutSecMin  = 1;
	int		ClientTimeoutSecMax = 255;
	// Client may request number of data blocks to receive before we wait/send an ack
	int		WindowSizeMin = 1;
	int		WindowSizeMax = 65535;
	// This is for testing only. Do not actually write data to disk. Just simulate as if we would be writing it
	int		NoFileWriteOnServer = 0;
	// Reuse port for connection. Sometimes there is only 1 port open in firewall, and we really do not wish to open other ports
	int		ReuseServerPortForComm = 0;
};

// Global resource that the whole application is able to access
// Should be initialized on startup and contain static values while the application runs
// Thread safe for reading
extern AppConfig sConf;