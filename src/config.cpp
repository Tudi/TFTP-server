#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include "pthread_win.h" // contains linux specific declarations
#endif
#include "config.h"

AppConfig sConf;


int chrpos(const char* str, const char c)
{
	int i = 0;
	while (str[i] != 0)
	{
		if (str[i] == c)
			return i;
		else
			i++;
	}
	return -1;
}

int strcmp2(const char* s1, const char* s2)
{
	int i = 0;
	while (s1[i] == s2[i] && s2[i] != 0 && s1[i] != 0)
		i++;
	//did we found the whole s2 ?
	if (s2[i] == 0)
		return 0;
	return 1;
}

void RemoveEOL(char* s)
{
	int i = 0;
	while (s[i] != 0)
	{
		if (s[i] == '\n' || s[i] == '\r')
			s[i] = 0;
		i++;
	}
}

int GenericStrLoader(const char* Filename, const char* ConfName, char* Store, int MaxStore)
{
	FILE* f;
	errno_t er = fopen_s(&f, Filename, "rt");
	if (f == NULL)
		return 1;
	while (!feof(f))
	{
		char ConfigFileLine[1500];
		char* ret = fgets(ConfigFileLine, sizeof(ConfigFileLine), f);
		if (ret && strcmp2(ConfigFileLine, ConfName) == 0)
		{
			int strposI = chrpos(ConfigFileLine, '=');
			if (strposI > 0)
			{
				fclose(f);
				strcpy_s(Store, MaxStore, &ConfigFileLine[strposI + 1]);
				RemoveEOL(Store);
				return 0;
			}
		}
	}
	fclose(f);
	Store[0] = 0;
	return -1;
}

int GetIntConfig(const char* Filename, const char* ConfName, int* Store)
{
	char ConfigFileValue[1500];
	int ret = GenericStrLoader(Filename, ConfName, ConfigFileValue, sizeof(ConfigFileValue));
	if (ret != 0)
		return ret;
	*Store = atoi(ConfigFileValue);
	return 0;
}

int GetStrConfig(const char* Filename, const char* ConfName, char* Store, int MaxBytes)
{
	return GenericStrLoader(Filename, ConfName, Store, MaxBytes);
}

int GetFloatConfig(const char* Filename, const char* ConfName, float* Store)
{
	char ConfigFileValue[1500];
	int ret = GenericStrLoader(Filename, ConfName, ConfigFileValue, sizeof(ConfigFileValue));
	if (ret != 0)
		return ret;
	*Store = (float)atof(ConfigFileValue);
	return 0;
}

int LoadMultiLineValue(const char* Filename, const char* ConfName, std::list<char*> &store)
{
	FILE* f;
	errno_t er = fopen_s(&f, Filename, "rt");
	if (f == NULL)
		return 1;
	while (!feof(f))
	{
		char ConfigFileLine[1500];
		char* ret = fgets(ConfigFileLine, sizeof(ConfigFileLine), f);
		if (ret && strcmp2(ConfigFileLine, ConfName) == 0)
		{
			int strposI = chrpos(ConfigFileLine, '=');
			if (strposI > 0)
			{
				RemoveEOL(&ConfigFileLine[strposI + 1]);
				store.push_back(_strdup(&ConfigFileLine[strposI + 1]));
			}
		}
	}
	fclose(f);
	return -1;
}

void InitConfig()
{
	GetIntConfig("init.cfg", "RetryResendPacketMax", &sConf.RetryResendPacketMax);
	GetIntConfig("init.cfg", "ParallelTransfersMax", &sConf.ParallelTransfersMax);
	GetIntConfig("init.cfg", "CommPortRangeStart", &sConf.CommPortStart);
	GetIntConfig("init.cfg", "CommPortRangeEnd", &sConf.CommPortEnd);
	GetIntConfig("init.cfg", "MaxFileSizeUpload", &sConf.KillConnectionAfterxBytesReceived);
	GetIntConfig("init.cfg", "MaxFileSizeDownload", &sConf.KillConnectionAfterxBytesSent);
	GetIntConfig("init.cfg", "AllowFileSizeQuery", &sConf.AllowTSizeOption);
	GetIntConfig("init.cfg", "TimeoutSecMin", &sConf.ClientTimeoutSecMin);
	GetIntConfig("init.cfg", "TimeoutSecMax", &sConf.ClientTimeoutSecMax);
	GetIntConfig("init.cfg", "WindowSizeMin", &sConf.WindowSizeMin);
	GetIntConfig("init.cfg", "WindowSizeMax", &sConf.WindowSizeMax);
	GetIntConfig("init.cfg", "NoFileWrite", &sConf.NoFileWriteOnServer);
	GetIntConfig("init.cfg", "UseServerPortForComm", &sConf.ReuseServerPortForComm);

	// Load multiple strings with same config value
	LoadMultiLineValue("init.cfg", "DownloadableFileName", sConf.DownloadableFileNames);
	LoadMultiLineValue("init.cfg", "UploadableFileName", sConf.UploadableFileNames);
}