#include <windows.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include "TitanEngine.h"

/*

Ursnif config dumper

Author: Nikolaos Pantazopoulos <Nikolaos.Pantazopoulos@nccgroup.com>

Released as open source by NCC Group, under the AGPL license.

Part of the source code is inspired by Mr Exodia example on 
how to unpack MPRESS/PESpin using TitanEngine

Tested with the following samples (SHA-256):

	F30454BCC7F1BC1F328B9B546F5906887FD0278C40D90AB75B8631EF18ED3B7F
	91CF2D4EA869972571D80F4C39253E0A044057D952F776DC1350C438A2AC886C
	D31F2993EC21C24064CE1F2987E10BFE271103880777B476C0D1812423C1C4B0 (PACKED)
	B16EFB470D941F57E9CA687B717A159B6E922518ACF2F3DAE2EC9F963FED5EDA
	AAA41C27D5B4A160ED2A00BA820DD6ADA86ED80E76D476A8379543478E608F84

*/

PROCESS_INFORMATION* fdProcessInfo;
LPVOID lpBaseOfImage;

std::vector<LPVOID> virtualaddresses;
std::vector<long> virtualaddresses_size;

/*
Purpose: log messages
*/
static void log(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	char msg[1024] = "";
	vsprintf(msg, format, args);
	puts(msg);
}

/*
Purpose: Search for null bytes so we can find the start and end of config
*/
DWORD loopback(DWORD addr,BOOL movement)
{
	int buf = 0;

	while (TRUE)
	{
		if (ReadProcessMemory(fdProcessInfo->hProcess, (void*)addr, &buf, 2, 0))
		{
			if (buf == 0x0000)
			{
				return addr;
			}
			else
			{
				if (movement)
				{
					addr = addr - 0x1;
				}
				else {
					addr = addr + 0x1;
				}
			}
		}
		else
		{
			fprintf(stderr,"ReadProcessMemory failed");
			return 0;
		}
		
	}
	
}

/*
Purpose: Search and dump the config file
*/

VOID StrtoInt_API_callback()
{
	long searchaddr;
	long foundaddr;
	DWORD APIAddress = GetContextData(UE_ESP);
  	BYTE wildcard = 0;
	int value = 0;
	
	APIAddress = APIAddress + 0x4;
	if (!ReadProcessMemory(fdProcessInfo->hProcess, (void*)APIAddress, &value, 4, 0))
	{
		fprintf(stderr, "ReadProcessMemory failed");
		StopDebug();
	}
	
	
	unsigned char pattern[6] = { 0x31 ,0x30 ,0x00 ,0x32 ,0x30 ,0x00};
	searchaddr = Find((void*)value, 0x100, pattern, 6, &wildcard);
	if (searchaddr)
	{
		foundaddr = loopback(searchaddr,1);
		int config_size = loopback(searchaddr,0) - loopback(searchaddr,1);

		if (DumpMemory(fdProcessInfo->hProcess, (void*)foundaddr, config_size, "config.txt"))
			log("Config dumped");
			StopDebug();
	
	}
	

}

/*
Purpose: Set breakpoint to StrToIntExA once GetComputerNameW is called.
*/
VOID Getcomputer_api_callback()
{
	log("GetComputerNameW called");
	BOOL results2 = SetAPIBreakPoint("kernelbase.dll", "StrToIntExA", UE_BREAKPOINT, UE_APISTART, StrtoInt_API_callback);
	if (!results2) {
		fprintf(stderr, "Error setting breakpoint to StrToIntExA\n");
		StopDebug();
	}

}

/*
Purpose: Set breakpoint to GetComputerNameW
*/
static void dumper_callback() {
		BOOL result1 = SetAPIBreakPoint("kernel32.dll", "GetComputerNameW", UE_SINGLESHOOT, UE_APIEND, Getcomputer_api_callback);
 		if (!result1)
		{
			fprintf(stderr, "Error setting breakpoint to GetComputerNameW\n");
			StopDebug();
		}
	
}

/*
Purpose: Record all virtual allocated memory addresses
*/
void virtualalloc_callback()
{
	long virtualalloc_address = GetContextData(UE_EAX);
	log("VirtualAlloc called");
	if (virtualalloc_address > 0) {
		virtualaddresses.push_back((LPVOID)virtualalloc_address);
	}
	long virtualalloc_size = GetFunctionParameter(fdProcessInfo->hProcess, UE_FUNCTION_STDCALL, 2, UE_PARAMETER_DWORD);
	if (virtualalloc_size > 0)
	{
		virtualaddresses_size.push_back(virtualalloc_size);
	}
}

/*
Purpose: Dump recorded virtual memory addresses before spawning a new process
*/
void createprocess_callback(){
	int counter = 0;
	int magic = 0;
	char name[8];
	log("CreateProcessW called");
 	if (virtualaddresses.empty())
	{
		fprintf(stderr, "No VirtualAlloc addresses were found");
		StopDebug();
	}
	BOOL virtualalloc_removebp = DeleteAPIBreakPoint("kernel32.dll", "VirtualAlloc", UE_APIEND);
	if (!virtualalloc_removebp)
	{
		fprintf(stderr, "Failed to remove VirtualAlloc breakpoint\n");
		StopDebug();
	}
	

	for (std::vector<LPVOID>::iterator iterate = virtualaddresses.begin(); iterate != virtualaddresses.end(); ++iterate)
	{
			std::cout << "Checking: "  << std::hex <<  *iterate << std::endl;
			snprintf(name, 8, "%p", *iterate);
			if (ReadProcessMemory(fdProcessInfo->hProcess, *iterate, &magic, 2, 0))
			{
				if (magic == 0x5A4D)
				{
					if (DumpMemory(fdProcessInfo->hProcess, *iterate, virtualaddresses_size.at(counter), name))
					{
						std::cout << "Found executable in " << std::hex << *iterate << std::endl;
						std::cout << "Dumped: " << std::hex << *iterate << std::endl;
					}
					else {
						fprintf(stderr, "Failed to dump\n");
						StopDebug();
					}
				}
				 
			}
			else {
				fprintf(stderr, "ReadProcessMemory failed");
				StopDebug();
			}
			
			counter++;
	}
	StopDebug();
}
/*
Purpose: Set breakpoints to VirtualAlloc and CreateProcessW.
*/
static void unpacker_callback() 
{
	BOOL virtualalloc_breakpoint = SetAPIBreakPoint("kernel32.dll", "VirtualAlloc", UE_BREAKPOINT, UE_APIEND, virtualalloc_callback);
	if (!virtualalloc_breakpoint)
	{
		fprintf(stderr, "Error setting breakpoint to VirtualAlloc\n");
		StopDebug();
	}
	BOOL createprocess_breakpoint = SetAPIBreakPoint("kernel32.dll", "CreateProcessW", UE_BREAKPOINT, UE_APISTART, createprocess_callback);
	if (!createprocess_breakpoint)
	{
		fprintf(stderr, "Error setting breakpoint to CreateProcessW\n");
		StopDebug();
	}
}

static void cbCreateProcess(CREATE_PROCESS_DEBUG_INFO* CreateProcessInfo)
{
	//Get the loaded base
	lpBaseOfImage = CreateProcessInfo->lpBaseOfImage;
	log("Process created on 0x%lX!", lpBaseOfImage);
}

 
/*
Purpose: Call the config dump callback 
*/
static void config_dumper(char* szFileName)
{
	//hide console window of created process
	SetEngineVariable(UE_ENGINE_NO_CONSOLE_WINDOW, true);
	//path
	HANDLE hFile = CreateFileA(szFileName, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		log("File \"%s\" doesn't exist...", szFileName);
		return;
	}
	CloseHandle(hFile);
	
	FILE_STATUS_INFO inFileStatus = {};
	
	//Start the process
	fdProcessInfo = (PROCESS_INFORMATION*)InitDebugEx(szFileName, 0, 0, (void*)dumper_callback);
	if (fdProcessInfo)
	{
		log("InitDebug OK!");
		//Set a custom handler
		SetCustomHandler(UE_CH_CREATEPROCESS, (void*)cbCreateProcess);
		//Start debug loop
		DebugLoop();
		}
		else
			log("InitDebug failed...");

}

/*
Purpose: Call the unpacking callback procedure. Please note that this monitors only CreateProcessW and VirtualAlloc
*/
static void unpackfile(char* filename)
{
	SetEngineVariable(UE_ENGINE_NO_CONSOLE_WINDOW, true);
	HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		log("File \"%s\" doesn't exist...", filename);
		return;
	}
	CloseHandle(hFile);
	log("Unpack of file \"%s\" started", filename);
	FILE_STATUS_INFO inFileStatus = {};
	fdProcessInfo = (PROCESS_INFORMATION*)InitDebugEx(filename, 0, 0, (void*)unpacker_callback);
	if (fdProcessInfo)
	{
		log("InitDebug OK!");
		//Set a custom handler
		SetCustomHandler(UE_CH_CREATEPROCESS, (void*)cbCreateProcess);
		//Start debug loop
		DebugLoop();
	}
	else
		log("InitDebug failed");

}
int main(int argc, char* argv[])
{
	puts("Ursnif dumper \n");
	if (argc == 2)
	{
		config_dumper(argv[1]);
		Sleep(2500);
	}

	else if (argc==3 and std::string(argv[2])=="unpack"){
		unpackfile(argv[1]);
	}
	else
	{
		puts("usage: dumper.exe [file.exe]");
		puts("usage: dumper.exe [file.exe] unpack");
	}
	return 0;
}