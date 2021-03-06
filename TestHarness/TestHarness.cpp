//	Copyright (c) 2018 Chet Simpson & The Giblet Initiative
//	
//	This file is subject to the terms and conditions defined in
//	file 'LICENSE.MD', which is part of this source code package.
//
#include "stdafx.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#include <Protocols/YMsg/ProtocolStream.h>
#include <YahooWeb/MultilHostWebServer.h>
#include <YahooWeb/YahooInsiderWebServer.h>
#include <YahooWeb/MyYahooWebServer.h>
#include <YahooWeb/Utilities.h>
#include <Protocols/YMsg/Server/Processors/Mock/ProtocolSync.h>
#include <Protocols/YMsg/Server/Processors/Mock/ChallengeRequest.h>
#include <Protocols/YMsg/Server/Processors/Mock/Authentication.h>
#include <Protocols/YMsg/Server/Processors/Mock/ChatLogin.h>
#include <Protocols/YMsg/Server/Processors/Mock/ChatJoin.h>
#include <Protocols/YMsg/Server/Processors/Mock/AddContact.h>
#include <Protocols/YMsg/Server/Processors/Mock/ServiceOperation.h>
#include <Protocols/YMsg/Server/Processors/Mock/PrivateMessage.h>
#include <Protocols/YMsg/Server/Processors/Mock/ChangeBlockedUser.h>
#include <Protocols/YMsg/Server/Processors/Mock/RenameContactGroup.h>
#include <Protocols/YMsg/Server/Processors/Mock/SetAway.h>
#include <Protocols/YMsg/Server/Processors/Mock/SetAvailable.h>
#include <Protocols/YMsg/Server/Processors/Mock/ActivateProfile.h>
#include <Protocols/YMsg/Server/Processors/Mock/DeactivateProfile.h>
#include <Protocols/YMsg/Server/Processors/Mock/DenyContactAddRequest.h>
#include <Protocols/YMsg/Server/Processors/Mock/RemoveContact.h>
#include <Protocols/YMsg/Server/Processors/Mock/RequestProfile.h>
#include <Protocols/YMsg/Server/Processors/Mock/MassPrivateMessage.h>
#include <mongoose.h>
#include <iostream>
#include <map>
#include <thread>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "ws2_32.lib")

void HTTPServerThread();

void InjectDLL(std::string targetDLLPath, HANDLE targetProcess)
{
	targetDLLPath.push_back('\0');

	// 1. Allocate memory in the remote process for szLibPath
	// 2. Write szLibPath to the allocated memory
	auto remoteBuffer = ::VirtualAllocEx(targetProcess, nullptr, targetDLLPath.size(), MEM_COMMIT, PAGE_READWRITE);
	::WriteProcessMemory(targetProcess, remoteBuffer, targetDLLPath.c_str(), targetDLLPath.size(), nullptr);


	// Load DLL into the remote process (via CreateRemoteThread & LoadLibrary)
	auto remoteThread = ::CreateRemoteThread(
		targetProcess,
		nullptr,
		0,
		(LPTHREAD_START_ROUTINE)::GetProcAddress(::GetModuleHandleA("Kernel32"), "LoadLibraryA"),
		remoteBuffer,
		0,
		nullptr);

	::WaitForSingleObject(remoteThread, INFINITE);

	// Get handle of the loaded module
	DWORD hLibModule;   // Base address of loaded module (==HMODULE);
	::GetExitCodeThread(remoteThread, &hLibModule);

	// Clean up
	::CloseHandle(remoteThread);
	::VirtualFreeEx(targetProcess, remoteBuffer, targetDLLPath.size(), MEM_RELEASE);
}


HANDLE LaunchMessenger()
{
	const std::wstring applicationPath1(L"C:\\Program Files (x86)\\Yahoo!\\Messenger\\YahooMessenger.exe");
	const std::wstring applicationPath2(L"C:\\Program Files (x86)\\Yahoo!\\Messenger\\ypager.exe");
	STARTUPINFO startupInfo{ 0 };
	PROCESS_INFORMATION processInformation{ 0 };

	startupInfo.cb = sizeof(startupInfo);
	BOOL createResult = CreateProcess(
		applicationPath1.c_str(),
		nullptr,
		nullptr,
		nullptr,
		FALSE,
		CREATE_SUSPENDED,
		nullptr,
		nullptr,
		&startupInfo,
		&processInformation);
	if (createResult == FALSE)
	{
		createResult = CreateProcess(
			applicationPath2.c_str(),
			nullptr,
			nullptr,
			nullptr,
			FALSE,
			CREATE_SUSPENDED,
			nullptr,
			nullptr,
			&startupInfo,
			&processInformation);
	}


	if (createResult == FALSE)
	{
		std::cerr << "Unable to start process\n";
		return nullptr;
	}

	char filenameBuffer[_MAX_PATH];
	::GetModuleFileNameA(nullptr, filenameBuffer, sizeof(filenameBuffer));
	::PathRemoveFileSpecA(filenameBuffer);

	InjectDLL(std::string(filenameBuffer) + "/YIMPatch.dll", processInformation.hProcess);

	ResumeThread(processInformation.hThread);

	return processInformation.hProcess;
}



class PacketDispatcher : public Giblet::Protocols::YMsg::IPacketDispatcher
{
public:

	PacketDispatcher()
	{
		using namespace Giblet::Protocols::YMsg;
		using namespace Giblet::Protocols::YMsg::Server;

		 processors_ = container_type{
			{ Payloads::ProtocolSync::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::ProtocolSync>>() },
			{ Payloads::ChallengeRequest::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::ChallengeRequest>>() },
			{ Payloads::Authentication::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::Authentication>>() },
			{ static_cast<uint16_t>(21), std::make_shared<NullPacketParser>() },		//	Set IMVironment configuration/settings
			{ static_cast<uint16_t>(22), std::make_shared<NullPacketParser>() },		//	Set configuration/settings
			{ Payloads::RequestProfile::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::RequestProfile>>() },
			//{10, std::make_shared<NullPacketParser>();		//	Add identify/update profile/something
			//{138, std::make_shared<NullPacketParser>();	//	Ping
			//	TODO: System message (Builder only)

			{ Payloads::SetAway::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::SetAway>>() },
			{ Payloads::SetAvailable::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::SetAvailable>>() },
			//	TODO: set idle status (the set idle service id is no longer used and idle is handled in setaway
			{ Payloads::ActivateProfile::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::ActivateProfile>>() },
			{ Payloads::DeactivateProfile::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::DeactivateProfile>>() },
			{ Payloads::AddContact::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::AddContact>>() },
			{ Payloads::RenameContactGroup::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::RenameContactGroup>>() },
			//	TODO: Move contact to group
			{ Payloads::RemoveContact::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::RemoveContact>>() },
			//	TODO: Accept contact add
			{ Payloads::DenyContactAddRequest::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::DenyContactAddRequest>>() },
			{ Payloads::ChangeBlockedUser::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::ChangeBlockedUser>>() },

			{ Payloads::PrivateMessage::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::PrivateMessage>>() },
			{ Payloads::MassPrivateMessage::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::MassPrivateMessage>>() },

			//	TODO: Voice chat request (defer)
			//	TODO: voice chat request deny (defer)
			{Payloads::ServiceOperation::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::ServiceOperation>>() },
			{Payloads::ChatLogin::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::ChatLogin>>() },
			{Payloads::ChatJoin::ServiceId, std::make_shared<PacketProcessor<Processors::Mock::ChatJoin>>() }
			//	TODO: Log out of chat
			//	TODO: Create chat room
			//	TODO: Join user in chat room
			//	TODO: Leave chat room
			//	TODO: Invite user to chat
			//	TODO: Decline invite to chat
			//	TODO: Chat message
			//	TODO: Chat ping

			//	TODO: Send contact details (need to be able to edit/retrieve them via insider.msg.yahoo.com)
			//	TODO: Receive contact details (need to be able to edit/retrieve them via insider.msg.yahoo.com)
		};
	}


	virtual void Dispatch(
		Giblet::Protocols::YMsg::YMSGSession& session,
		const Giblet::Protocols::YMsg::Header& header,
		buffer_type::const_iterator payloadBegin,
		buffer_type::const_iterator payloadEnd)
	{
		auto processor = processors_.find(header.serviceId);
		if (processor == processors_.end())
		{
			std::cerr << "Unable to find processor for service id " << header.serviceId << "  attribute " << header.attribute << "\n";
			Giblet::Protocols::YMsg::Payload payload;
			Giblet::Protocols::YMsg::PacketParser parser;
			parser.Parse(payload, header, payloadBegin, payloadEnd);
			return;
		}

		std::cerr << "Parsing payload for service id " << header.serviceId << "  attribute " << header.attribute << "\n";

		processor->second->Parse(session, header, payloadBegin, payloadEnd);
	}


protected:


	using container_type = std::map<uint16_t, std::shared_ptr<Giblet::Protocols::YMsg::IPacketProcessor>>;

	container_type	processors_;
};




void YMSGSessionThread(SOCKET clientSocket)
{
	Giblet::Protocols::YMsg::YMSGSession session(clientSocket);
	Giblet::Protocols::YMsg::ProtocolStream inputStream(std::make_shared<PacketDispatcher>());

	for (bool run(true); run;)
	{
		fd_set readfds;
		timeval timeout;

		FD_ZERO(&readfds);
		FD_SET(clientSocket, &readfds);

		timeout.tv_sec = 0;
		timeout.tv_usec = 50000;

		auto result = select(1, &readfds, nullptr, nullptr, &timeout);
		if (result > 0)
		{
			if (!inputStream.Append(session))
			{
				std::cerr << "Unable to retrieve number of bytes available on socket\n";
				return;
			}
		}
	}
}




void YMSGListenerThread(SOCKET listener)
{
	do
	{
		auto clientSocket = accept(listener, nullptr, nullptr);
		if (clientSocket == INVALID_SOCKET)
		{
			break;
		}

		std::thread(YMSGSessionThread, clientSocket).detach();
	}
	while (true);

	if (closesocket(listener) == SOCKET_ERROR)
	{
		std::cerr << "closesocket function failed with error " << WSAGetLastError() << '\n';
	}
}




void HTTPServerThread()
{
	using namespace std::placeholders;

	auto gateway(std::make_shared<Giblet::Protocols::YMsg::SessionGateway>());
	Giblet::YahooWeb::MultilHostWebServer serverHost(gateway);

	//	Add our generic handler for the lcoal host
	serverHost.Add("localhost", std::bind(Giblet::YahooWeb::GenericHandler, _1, "htdoc"));

	//	Add our handler for the chat content server
	serverHost.Add("chat.yahoo.com.localhost", std::bind(Giblet::YahooWeb::GenericHandler, _1, "htdoc/chat.yahoo.com"));

	//	Add our handler for the yahoo insider
	Giblet::YahooWeb::YahooInsiderWebServer yahooInsiderWebServer(gateway);
	serverHost.Add("insider.msg.yahoo.com.localhost", yahooInsiderWebServer);

	//
	Giblet::YahooWeb::MyYahooWebServer myYahooWebServer(gateway, "htdoc/my.yahoo.com");
	serverHost.Add("my.yahoo.com.localhost", myYahooWebServer);


	struct mg_server *server = mg_create_server(&serverHost);
	mg_set_option(server, "document_root", ".");
	mg_set_option(server, "listening_port", "41337");
	mg_add_uri_handler(server, "/", Giblet::YahooWeb::MultilHostWebServer::ConnectionCallback);
	for (;;) mg_poll_server(server, 1000);  // Infinite loop, Ctrl-C to stop
	mg_destroy_server(&server);
}




int main()
{
	WSADATA wsaData{ 0 };
	auto result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != NO_ERROR)
	{
		std::cerr << "WSAStartup() failed with error: " << result << '\n';
		return EXIT_FAILURE;
	}

	std::thread(HTTPServerThread).detach();

	//----------------------
	// Create a SOCKET for listening for incoming connection requests.
	auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == INVALID_SOCKET)
	{
		std::cerr << "socket function failed with error: " <<  WSAGetLastError() << '\n';
		WSACleanup();
		return EXIT_FAILURE;
	}
	//----------------------
	// The sockaddr_in structure specifies the address family,
	// IP address, and port for the socket that is being bound.
	sockaddr_in service;

	service.sin_family = AF_INET;
	service.sin_addr.s_addr = ADDR_ANY;
	service.sin_port = htons(5050);

	result = bind(listener, reinterpret_cast<SOCKADDR*>(&service), sizeof(service));
	if (result == SOCKET_ERROR)
	{
		std::cerr << "bind function failed with error " << WSAGetLastError() << '\n';
		result = closesocket(listener);
		if (result == SOCKET_ERROR)
		{
			std::cerr << "closesocket function failed with error " << WSAGetLastError() << '\n';
		}
		WSACleanup();
		return EXIT_FAILURE;
	}
	//----------------------
	// Listen for incoming connection requests 
	// on the created socket
	if (listen(listener, SOMAXCONN) == SOCKET_ERROR)
	{
		std::cerr << "listen function failed with error: " << WSAGetLastError() << '\n';
	}
	
	std::cerr << "Listening on socket...\n";
	std::thread(YMSGListenerThread, listener).detach();
	std::cerr << "Launching messenger...\n";
	auto messengerHandle(LaunchMessenger());
	std::cerr << "Waiting...\n";
	::WaitForSingleObject(messengerHandle, INFINITE);


	WSACleanup();

	return EXIT_SUCCESS;
}

