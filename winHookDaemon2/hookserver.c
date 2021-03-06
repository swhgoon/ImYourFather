#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <windows.h>
#include "hookproc.h"

typedef LRESULT CALLBACK (*HookFunc) (int nCode, WPARAM wParam, LPARAM lParam);
typedef void (*SetTidFunc) (DWORD tid);

#define MAX_CLI 5
SOCKET clients[MAX_CLI];

void*
service(void* port)
{	
    SOCKET sock_server;
	// int port_int = (int)(intptr_t) port; // 64bits version
	int port_int = (int) port; // 32 bits version

    WSADATA wsaData;
    SOCKADDR_IN local;

    int wsaret = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if(wsaret != 0) {
		fprintf(stderr, "WSAStartup error");
        exit(EXIT_FAILURE);
    }

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(port_int);

    sock_server = socket(AF_INET,SOCK_STREAM, 0);
    if(sock_server == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation error");
		exit(EXIT_FAILURE);
    }

    if(bind(sock_server, (SOCKADDR*) &local, sizeof(local)) != 0) {
        fprintf(stderr, "Bind error");
		exit(EXIT_FAILURE);
    }

    if(listen(sock_server, 10) != 0) {
        fprintf(stderr, "Listen error");
		exit(EXIT_FAILURE);
    }

	for(int i = 0; i < MAX_CLI; i++) {
		clients[i] = -1;
	}
	
    SOCKET client;
    SOCKADDR_IN from;
    int fromlen = sizeof(from);
	int found;
    while(1) {
        client = accept(sock_server, (struct sockaddr*)&from, &fromlen);
        
		if(client == INVALID_SOCKET) {
			fprintf(stderr, "connect error");
			continue;
		}
		
		found = 0;
		for(int i = 0; i < MAX_CLI; i++) {
			if(clients[i] == -1) {
				clients[i] = client;
				found = 1;
				break;
			}
		}
		
		if(!found) {
			closesocket(client);
		}
    }
}

int
main(int argc, char** argv)
{
    int nRetCode = 0;
	if(argc != 2) {
	 fprintf(stderr, "usage : %s [port]", argv[0]);
	 exit(EXIT_FAILURE);
	}
	int port = atoi(argv[1]); // TODO : change atoi use / consider possible errors
	pthread_t thread;
	
    // int rc = pthread_create(&thread, NULL, service, (void*)(intptr_t) port); // 64 bits version
	int rc = pthread_create(&thread, NULL, service, (void*) port); // 32 bits version
	if(rc != 0) {
      exit(EXIT_FAILURE);
	}
		
	HMODULE hkModule = LoadLibrary(TEXT("HookProc.dll"));
	if (! hkModule) {
		fprintf(stderr, "Error loading \"HookProc.dll\".\n");
		return EXIT_FAILURE;
	}
	
	HookFunc Hook = (HookFunc) GetProcAddress(hkModule, "HookProc");
	if (! Hook) {
		fprintf(stderr, "Error locating \"HookProc\" function.\n");
		return EXIT_FAILURE;
    }
	
	SetTidFunc SetTid = (SetTidFunc) GetProcAddress(hkModule, "HookProcSetTid");
	if (! SetTid) {
		fprintf(stderr, "Error locating \"SetTid\" function.\n");
		return EXIT_FAILURE;
    }

	int myTid = GetCurrentThreadId();
	SetTid(myTid);
	
	HHOOK hhk = SetWindowsHookEx(WH_CBT, Hook, hkModule, 0);
	if(hhk == NULL) {
		fprintf(stderr, "ERROR SetWindowsHookEx");
		return EXIT_FAILURE;
	}

	/* message loop in order to get the hwnd of the creawndted window */
	MSG msg;
	HWND threadOnly = (HWND) -1; // -1 : only message pass to thread
	int intHwnd, type;
	HWND hwnd;
	while(GetMessage(&msg, threadOnly, 0, 0)) {		
		if((msg.message == CUSTOM_MSG_CREATION) || (msg.message == CUSTOM_MSG_DESTRUCTION)) {
			hwnd = (HWND) msg.wParam;
			intHwnd = hwnd;
			intHwnd = htonl(intHwnd);
			
			type = (msg.message == CUSTOM_MSG_CREATION) ? 0 : htonl(1);
			fprintf(stderr, "%s : %d\n", (msg.message == CUSTOM_MSG_CREATION) ? "creation " : "destruction ", hwnd);
			
			for(int i = 0; i < MAX_CLI; i++) {
				if(clients[i] != -1) {
				    rc = send(clients[i], (char*) &type, sizeof(int), 0);
					if(rc == SOCKET_ERROR) {
						closesocket(clients[i]);
						continue;
					}
					
					rc = send(clients[i], (char*) &intHwnd, sizeof(int), 0);
					if(rc == SOCKET_ERROR) {
						closesocket(clients[i]);
						continue;
					}
				}
			}
		}
	}
}