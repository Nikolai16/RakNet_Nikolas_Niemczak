
#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
static unsigned int SERVER_PORT = 65000;
unsigned int CLIENT_PORT = 65001;
static unsigned int MAX_CONNECTIONS = 4;

enum NetworkState
{
	NS_Init = 0,
	NS_PendingStart,
	NS_Started,
	NS_ReadyToChat,
};

bool isServer = false;
bool isRunning = true;

std::string userName;

RakNet::RakPeerInterface *g_rakPeerInterface = nullptr;
NetworkState g_networkState = NS_Init;

unsigned char GetPacketIdentifier(RakNet::Packet *packet)
{
	if (packet == nullptr)
		return 255;

	if ((unsigned char)packet->data[0] == ID_TIMESTAMP)
	{
		RakAssert(packet->length > sizeof(RakNet::MessageID) + sizeof(RakNet::Time));
		return (unsigned char)packet->data[sizeof(RakNet::MessageID) + sizeof(RakNet::Time)];
	}
	else
		return (unsigned char)packet->data[0];
}


void InputHandler()
{
	while (isRunning)
	{
		char userInput[255];

		if (g_networkState == NS_Init)
		{
			std::cout << "press (s) for server (c) for client" << std::endl;
			std::cin >> userInput;
			isServer = (userInput[0] == 's');
			g_networkState = NS_PendingStart;
		}
		if (g_networkState == NS_Started)
		{
			std::cin >> userInput;

			char message2[2048];
			message2[0] = 0;
			//const static char prefix[] = "Server: ";
			strncpy_s(message2, userName.c_str(), sizeof(message2));
			strncat_s(message2, userInput, sizeof(message2) - strlen(userName.c_str()) - 1);
			g_rakPeerInterface->Send(message2, (const int)strlen(message2) + 1, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
		}
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}

void PacketHandler()
{
	while (isRunning)
	{
		for (RakNet::Packet* packet = g_rakPeerInterface->Receive(); packet != nullptr; g_rakPeerInterface->DeallocatePacket(packet), packet = g_rakPeerInterface->Receive())
		{
			// We got a packet, get the identifier with our handy function
			unsigned char packetIdentifier = GetPacketIdentifier(packet);

			// Check if this is a network message packet
			switch (packetIdentifier)
			{
			case ID_DISCONNECTION_NOTIFICATION:
				// Connection lost normally
				printf("ID_DISCONNECTION_NOTIFICATION\n");
				break;
			case ID_ALREADY_CONNECTED:
				// Connection lost normally
				printf("ID_ALREADY_CONNECTED with guid %" PRINTF_64_BIT_MODIFIER "u\n", packet->guid);
				break;
			case ID_INCOMPATIBLE_PROTOCOL_VERSION:
				printf("ID_INCOMPATIBLE_PROTOCOL_VERSION\n");
				break;
			case ID_REMOTE_DISCONNECTION_NOTIFICATION: // Server telling the clients of another client disconnecting gracefully.  You can manually broadcast this in a peer to peer enviroment if you want.
				printf("ID_REMOTE_DISCONNECTION_NOTIFICATION\n");
				break;
			case ID_REMOTE_CONNECTION_LOST: // Server telling the clients of another client disconnecting forcefully.  You can manually broadcast this in a peer to peer enviroment if you want.
				printf("ID_REMOTE_CONNECTION_LOST\n");
				break;
			case ID_REMOTE_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
				printf("ID_REMOTE_NEW_INCOMING_CONNECTION\n");
				break;
			case ID_CONNECTION_BANNED: // Banned from this server
				printf("We are banned from this server.\n");
				break;
			case ID_CONNECTION_ATTEMPT_FAILED:
				printf("Connection attempt failed\n");
				break;
			case ID_NO_FREE_INCOMING_CONNECTIONS:
				// Sorry, the server is full.  I don't do anything here but
				// A real app should tell the user
				printf("ID_NO_FREE_INCOMING_CONNECTIONS\n");
				break;

			case ID_INVALID_PASSWORD:
				printf("ID_INVALID_PASSWORD\n");
				break;

			case ID_CONNECTION_LOST:
				// Couldn't deliver a reliable packet - i.e. the other system was abnormally
				// terminated
				printf("ID_CONNECTION_LOST\n");
				break;

			case ID_CONNECTION_REQUEST_ACCEPTED:
				// This tells the client they have connected
				printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n", packet->systemAddress.ToString(true), packet->guid.ToString());
				printf("My external address is %s\n", g_rakPeerInterface->GetExternalID(packet->systemAddress).ToString(true));
				break;
			case ID_CONNECTED_PING:
			case ID_UNCONNECTED_PING:
				printf("Ping from %s\n", packet->systemAddress.ToString(true));
				break;

			default:
				// It's a client, so just show the message
				printf("%s\n", packet->data);

				if (isServer)
				{
					char userInput[255];
					sprintf_s(userInput, "%s", packet->data);
					g_rakPeerInterface->Send(userInput, (const int)strlen(userInput) + 1, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, true);
				}

				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}


}

int main()
{
	g_rakPeerInterface = RakNet::RakPeerInterface::GetInstance();

	std::thread inputHandler(InputHandler);
	std::thread packetHandler(PacketHandler);

	while (isRunning)
	{
		if (g_networkState == NS_PendingStart)
		{
			if (isServer)
			{
				std::cout << "Enter in a user name" << std::endl;
				std::cin >> userName;
				userName = userName + ": ";

				RakNet::SocketDescriptor socketDescriptors[1];
				socketDescriptors[0].port = SERVER_PORT;
				socketDescriptors[0].socketFamily = AF_INET; // Test out IPV4

				bool isSuccess = g_rakPeerInterface->Startup(MAX_CONNECTIONS, socketDescriptors, 1) == RakNet::RAKNET_STARTED;
				assert(isSuccess);
				//ensures we are server
				g_rakPeerInterface->SetMaximumIncomingConnections(MAX_CONNECTIONS);
				std::cout << "server started" << std::endl;
				g_networkState = NS_Started;
			}
			//client
			else
			{
				std::cout << "Enter in a port number" << std::endl;
				std::cin >> CLIENT_PORT;

				std::cout << "Enter in your user name" << std::endl;
				std::cin >> userName;
				userName = userName + ": ";

				RakNet::SocketDescriptor socketDescriptor(CLIENT_PORT, 0);
				socketDescriptor.socketFamily = AF_INET;
				RakNet::StartupResult result = g_rakPeerInterface->Startup(8, &socketDescriptor, 1);
				assert(result == RakNet::RAKNET_STARTED);
				g_rakPeerInterface->SetOccasionalPing(true);
				//"127.0.0.1" = local host = your machines address


				//Enter in adress here
				RakNet::ConnectionAttemptResult car = g_rakPeerInterface->Connect("10.4.89.108", SERVER_PORT, nullptr, 0);

				RakAssert(car == RakNet::CONNECTION_ATTEMPT_STARTED);
				std::cout << "client attempted connection..." << std::endl;
				g_networkState = NS_Started;
			}
		}

	}

	inputHandler.join();
	packetHandler.join();
	return 0;
}