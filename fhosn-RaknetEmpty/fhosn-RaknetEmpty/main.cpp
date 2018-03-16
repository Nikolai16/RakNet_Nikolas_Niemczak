#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
#include "BitStream.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>
#include <string>  

static unsigned int SERVER_PORT = 65000;
static unsigned int CLIENT_PORT = 65001;
static unsigned int MAX_CONNECTIONS = 3;

void brodcastEvent(std::string info);
void switchPlayersTurn();

enum NetworkState
{
	NS_Init = 0,
	NS_PendingStart,
	NS_Started,
	NS_Lobby,
	NS_Pending,
	NS_Game_Current_Turn,
	NS_Game_Waiting,
	NS_Server_Started_Game,
};

bool isServer = false;
bool isRunning = true;

RakNet::RakPeerInterface *g_rakPeerInterface = nullptr;
RakNet::SystemAddress g_serverAddress;

std::mutex g_networkState_mutex;
std::mutex g_textPrint_mutex;
NetworkState g_networkState = NS_Init;

RakNet::RakString playerName = "Default";
unsigned int playersTurn = 0;
std::string* playerNameArray[3];

enum {
	ID_THEGAME_LOBBY_READY = ID_USER_PACKET_ENUM,
	ID_PLAYER_READY,
	ID_THEGAME_START,
	ID_Character_Selected,
	ID_Set_Players_Turn,
	ID_Heal,
	ID_Attack_Player,
	ID_Request_Stats,
	ID_Recive_Stats,
	ID_Invalid_Target,
	ID_Event_Info,
	ID_End_Game,
};

enum EPlayerClass
{
	Default = 0,
	Mage,
	Rogue,
	Cleric,
};

RakNet::RakString convertClass(EPlayerClass character)
{
	if (character == EPlayerClass::Mage)
		return "Mage";
	else if (character == EPlayerClass::Rogue)
		return "Rogue";
	else if (character == EPlayerClass::Cleric)
		return "Cleric";
	else if (character == EPlayerClass::Default)
		return "Default";

	return "ERROR: Getting class";

}

struct SPlayer
{
	std::string m_name;
	int m_health = 50;
	EPlayerClass m_class;

	//function to send a packet with name/health/class etc
	void SendName(RakNet::SystemAddress systemAddress, bool isBroadcast)
	{
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString name(m_name.c_str());
		writeBs.Write(name);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}
};

std::map<unsigned long, SPlayer> m_players;

SPlayer& GetPlayer(RakNet::RakNetGUID raknetId)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(raknetId);
	std::map<unsigned long, SPlayer>::iterator it = m_players.find(guid);
	assert(it != m_players.end());
	return it->second;
}

void OnLostConnection(RakNet::Packet* packet)
{
	SPlayer& lostPlayer = GetPlayer(packet->guid);
	//lostPlayer.SendName(RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
	unsigned long keyVal = RakNet::RakNetGUID::ToUint32(packet->guid);

	std::string info;

	info += lostPlayer.m_name;
	info += " has disconnected form the game.";

	if (g_networkState == NS_Server_Started_Game)
	{
		for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
		{
			if (playerNameArray[playersTurn]->c_str() == lostPlayer.m_name)
			{
				brodcastEvent(info);
				m_players.erase(keyVal);
				switchPlayersTurn();
				return;
			}
		}
	}

	m_players.erase(keyVal);
	brodcastEvent(info);

	//m_players.erase(keyVal);
}

//server
void OnIncomingConnection(RakNet::Packet* packet)
{
	//must be server in order to recieve connection
	assert(isServer);
	m_players.insert(std::make_pair(RakNet::RakNetGUID::ToUint32(packet->guid), SPlayer()));
	std::cout << "Total Players: " << m_players.size() << std::endl;
}

//client
void OnConnectionAccepted(RakNet::Packet* packet)
{
	//server should not ne connecting to anybody, 
	//clients connect to server
	assert(!isServer);
	g_networkState_mutex.lock();
	g_networkState = NS_Lobby;
	g_networkState_mutex.unlock();
	g_serverAddress = packet->systemAddress;
}

//this is on the client side
void DisplayPlayerReady(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	std::cout << userName.C_String() << " has joined" << std::endl;
}

void OnLobbyReady(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);
	player.m_name = userName;
	//std::cout << userName.C_String() << " aka " << player.m_name.c_str() << " IS READY!!!!!" << std::endl;

	//notify all other connected players that this plyer has joined the game
	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		//skip over the player who just joined
		if (guid == it->first)
		{
			continue;
		}

		SPlayer& player = it->second;
		player.SendName(packet->systemAddress, false);
		/*RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
		RakNet::RakString name(player.m_name.c_str());
		writeBs.Write(name);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, false));*/
	}

	player.SendName(packet->systemAddress, true);

	/*RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_PLAYER_READY);
	RakNet::RakString name(player.m_name.c_str());
	writeBs.Write(name);
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->systemAddress, true));*/

}

void selectedCharacter(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString character;
	bs.Read(character);

	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);

	if (character == "1")
	{
		player.m_class = EPlayerClass::Mage;
		player.m_health += 10;
	}

	else if (character == "2")
	{
		player.m_class = EPlayerClass::Rogue;
	}

	else if (character == "3")
	{
		player.m_class = EPlayerClass::Cleric;
	}

	else
	{
		//Explode if no proper class is chosen
		player.m_class = EPlayerClass::Mage;
		player.m_health += 10;
	}

	std::cout << convertClass(player.m_class) << " aka " << player.m_name.c_str() << " IS READY!!!!!" << std::endl;

	if (m_players.size() == MAX_CONNECTIONS)
	{
		for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
		{
			if (it->second.m_class == EPlayerClass::Default)
				return;
			//std::cout << convertClass(it->second.m_class) << " is " << it->second.m_name.c_str() << std::endl;
		}

		std::cout << "All players ready starting game" << std::endl;

		g_networkState = NS_Server_Started_Game;

		system("cls");

		int i = 0;
		for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
		{
			playerNameArray[i] = &it->second.m_name;
			i++;
		}

		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_Set_Players_Turn);

		RakNet::RakString name(playerNameArray[0]->c_str());
		writeBs.Write(name);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));
	}
}

void givePlayerStats(RakNet::Packet *packet)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(packet->guid);
	SPlayer& player = GetPlayer(packet->guid);

	RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_Recive_Stats);

	std::string statsToSend;
	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		if (it->first == RakNet::RakNetGUID::ToUint32(packet->guid))
		{
			statsToSend += "(you ";
			statsToSend += it->second.m_name.c_str();
			statsToSend += " the ";
			statsToSend += convertClass(it->second.m_class);
			statsToSend += " has ";
			statsToSend += std::to_string(it->second.m_health);
			statsToSend += " HP)    ";
		}
		else
		{
			if (it->second.m_health > 0)
			{
				statsToSend += it->second.m_name.c_str();
				statsToSend += " the ";
				statsToSend += convertClass(it->second.m_class);
				statsToSend += " has ";
				statsToSend += std::to_string(it->second.m_health);
				statsToSend += " HP    ";
			}
		}
	}

	RakNet::RakString stats(statsToSend.c_str());
	writeBs.Write(stats);

	//returns 0 when something is wrong
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->guid, false));
}

void reciveStats(RakNet::Packet *packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString stats;
	bs.Read(stats);

	g_textPrint_mutex.lock();
	std::cout << stats << std::endl;
	g_textPrint_mutex.unlock();
	g_networkState_mutex.lock();
	g_networkState = NS_Game_Current_Turn;
	g_networkState_mutex.unlock();
}

void askForStats()
{
	RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_Request_Stats);

	//returns 0 when something is wrong
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));
}

void setPlaying(RakNet::Packet *packet)
{
	//system("cls");

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString character;
	bs.Read(character);

	if (character == playerName)
	{

		g_networkState_mutex.lock();
		g_networkState = NS_Game_Current_Turn;
		g_networkState_mutex.unlock();
		g_textPrint_mutex.lock();
		std::cout << "It's you're turn" << std::endl;
		g_textPrint_mutex.unlock();
	}
	else
	{
		std::cout << "It's " << character << " turn" << std::endl;
		g_networkState_mutex.lock();
		g_networkState = NS_Game_Waiting;
		g_networkState_mutex.unlock();
	}
}

void invalidTarget(RakNet::Packet *packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString info;
	bs.Read(info);

	g_textPrint_mutex.lock();
	std::cout << info << std::endl;
	g_textPrint_mutex.unlock();

	//askForStats();
	g_networkState_mutex.lock();
	g_networkState = NS_Game_Current_Turn;
	g_networkState_mutex.unlock();
	g_textPrint_mutex.lock();
	std::cout << "It's you're turn" << std::endl;
	g_textPrint_mutex.unlock();
}

void endGame(RakNet::Packet *packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString info;
	bs.Read(info);

	g_textPrint_mutex.lock();
	std::cout << info << std::endl;
	g_textPrint_mutex.unlock();
}

void switchPlayersTurn()
{
	bool temp = true;

	while (temp)
	{
		int tempPlayers = 0;
		std::string tempStr;
		for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
		{
			if (it->second.m_health > 0)
			{
				tempPlayers++;
				tempStr += it->second.m_name;
				tempStr += " the ";
				tempStr += convertClass(it->second.m_class);
			}
		}

		if (tempPlayers == 1)
		{
			RakNet::BitStream writeBs;
			writeBs.Write((RakNet::MessageID)ID_End_Game);

			tempStr += " has won the game";

			RakNet::RakString name(tempStr.c_str());
			writeBs.Write(name);

			//returns 0 when something is wrong
			assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));

			return;
		}

		playersTurn++;

		if (playersTurn > MAX_CONNECTIONS - 1)
			playersTurn = 0;

		for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
		{
			if (playerNameArray[playersTurn]->c_str() == it->second.m_name.c_str())
			{
				if (it->second.m_health > 0)
				{
					RakNet::BitStream writeBs;
					writeBs.Write((RakNet::MessageID)ID_Set_Players_Turn);

					RakNet::RakString name(playerNameArray[playersTurn]->c_str());
					writeBs.Write(name);

					//returns 0 when something is wrong
					assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));

					return;
				}
				else
				{
					break;
				}
			}
		}
	}
}

void brodcastEvent(std::string info)
{
	RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_Event_Info);

	const char* derp = info.c_str();

	RakNet::RakString name(derp);
	writeBs.Write(name);

	//returns 0 when something is wrong
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));
}

void healPlayer(RakNet::Packet *packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	SPlayer& player = GetPlayer(packet->guid);

	int healAmount;

	if (player.m_class == Cleric)
		healAmount = 15;
	else
		healAmount = 10;

	std::string tempString;
	tempString += player.m_name;
	tempString += " the ";
	tempString += convertClass(player.m_class);
	tempString += " healed for ";
	tempString += std::to_string(healAmount);
	tempString += ".";

	brodcastEvent(tempString);

	player.m_health += healAmount;
	//std::cout << player.m_name.c_str() <<" current health is " << player.m_health << std::endl;

	switchPlayersTurn();
}

void attackPlayer(RakNet::Packet *packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString targetName;
	bs.Read(targetName);

	std::string statsToSend;

	for (std::map<unsigned long, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		if (it->first == RakNet::RakNetGUID::ToUint32(packet->guid))
			continue;

		if (it->second.m_name == targetName.C_String() && it->second.m_health > 0)
		{
			SPlayer& player = GetPlayer(packet->guid);

			int damage;

			if (player.m_class == Rogue)
				damage = 25;
			else
				damage = 20;

			it->second.m_health -= damage;

			if (it->second.m_health > 0)
			{
				std::string tempString;
				tempString += it->second.m_name;
				tempString += " the ";
				tempString += convertClass(it->second.m_class);
				tempString += " was hit by ";
				tempString += player.m_name;
				tempString += " the ";
				tempString += convertClass(player.m_class);
				tempString += " for ";
				tempString += std::to_string(damage);
				tempString += " damage.";

				brodcastEvent(tempString);
			}
			else
			{
				std::string tempString;
				tempString += it->second.m_name;
				tempString += " the ";
				tempString += convertClass(it->second.m_class);
				tempString += " was killed by ";
				tempString += player.m_name;
				tempString += " the ";
				tempString += convertClass(player.m_class);
				tempString += ".";

				brodcastEvent(tempString);
			}

			switchPlayersTurn();
			return;
		}
	}

	RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_Invalid_Target);

	RakNet::RakString name("Invald target");
	writeBs.Write(name);

	//returns 0 when something is wrong
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->guid, false));
}

void reciveEvent(RakNet::Packet *packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString info;
	bs.Read(info);

	g_textPrint_mutex.lock();
	std::cout << info << std::endl;
	g_textPrint_mutex.unlock();
}


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
			g_networkState_mutex.lock();
			g_networkState = NS_PendingStart;
			g_networkState_mutex.unlock();

		}
		else if (g_networkState == NS_Lobby)
		{
			std::cout << "Enter your name to play or type quit to leave" << std::endl;
			std::cin >> userInput;
			//quitting is not acceptable in our game, create a crash to teach lesson
			assert(strcmp(userInput, "quit"));

			RakNet::BitStream bs;
			bs.Write((RakNet::MessageID)ID_THEGAME_LOBBY_READY);
			RakNet::RakString name(userInput);
			bs.Write(name);
			playerName = name;

			//returns 0 when something is wrong
			assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));

			std::cout << "Enter your class.\npress 1 for mage (more health) \npress 2 for rouge (more damage) \nand 3 for cleric (more health healed)" << std::endl;
			std::cin >> userInput;

			RakNet::BitStream bs2;
			bs2.Write((RakNet::MessageID)ID_Character_Selected);
			RakNet::RakString character(userInput);
			bs2.Write(character);

			assert(g_rakPeerInterface->Send(&bs2, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));

			g_networkState_mutex.lock();
			g_networkState = NS_Pending;
			g_networkState_mutex.unlock();

			system("cls");
		}
		else if (g_networkState == NS_Pending)
		{
			static bool doOnce = false;
			if (!doOnce)
				std::cout << "pending..." << std::endl;

			doOnce = true;
		}
		else if (g_networkState == NS_Game_Current_Turn)
		{
			bool inputValid = false;

			while (!inputValid)
			{
				g_textPrint_mutex.lock();
				std::cout << "Do something. Press 1 to attack, 2 to heal, and 3 for stats" << std::endl;
				g_textPrint_mutex.unlock();
				std::cin >> userInput;

				if (userInput[0] == '1' || userInput[0] == '2' || userInput[0] == '3')
					inputValid = true;
				else
					std::cout << "Input invalid" << std::endl;
			}

			if (userInput[0] == '1')
			{
				RakNet::BitStream bs2;
				bs2.Write((RakNet::MessageID)ID_Attack_Player);

				std::cout << "Who do you want to attack" << std::endl;
				std::cin >> userInput;

				RakNet::RakString character(userInput);
				bs2.Write(character);

				assert(g_rakPeerInterface->Send(&bs2, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));

				g_networkState_mutex.lock();
				g_networkState = NS_Pending;
				g_networkState_mutex.unlock();
			}
			else if (userInput[0] == '2')
			{
				RakNet::BitStream bs2;
				bs2.Write((RakNet::MessageID)ID_Heal);

				assert(g_rakPeerInterface->Send(&bs2, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));

				g_networkState_mutex.lock();
				g_networkState = NS_Pending;
				g_networkState_mutex.unlock();
			}
			else if (userInput[0] == '3')
			{
				askForStats();

				g_networkState_mutex.lock();
				g_networkState = NS_Pending;
				g_networkState_mutex.unlock();
			}
		}

		std::this_thread::sleep_for(std::chrono::microseconds(1000000));
	}
}

bool HandleLowLevelPackets(RakNet::Packet* packet)
{
	bool isHandled = true;
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
	case ID_NEW_INCOMING_CONNECTION:
		//client connecting to server
		OnIncomingConnection(packet);
		printf("ID_NEW_INCOMING_CONNECTION\n");
		break;
	case ID_REMOTE_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
		OnIncomingConnection(packet);
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
		OnLostConnection(packet);
		break;

	case ID_CONNECTION_REQUEST_ACCEPTED:
		// This tells the client they have connected
		printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n", packet->systemAddress.ToString(true), packet->guid.ToString());
		printf("My external address is %s\n", g_rakPeerInterface->GetExternalID(packet->systemAddress).ToString(true));
		OnConnectionAccepted(packet);
		break;
	case ID_CONNECTED_PING:
	case ID_UNCONNECTED_PING:
		printf("Ping from %s\n", packet->systemAddress.ToString(true));
		break;
	default:
		isHandled = false;
		break;
	}
	return isHandled;
}

void PacketHandler()
{
	while (isRunning)
	{
		for (RakNet::Packet* packet = g_rakPeerInterface->Receive(); packet != nullptr; g_rakPeerInterface->DeallocatePacket(packet), packet = g_rakPeerInterface->Receive())
		{
			if (!HandleLowLevelPackets(packet))
			{
				//our game specific packets
				unsigned char packetIdentifier = GetPacketIdentifier(packet);
				switch (packetIdentifier)
				{
				case ID_THEGAME_LOBBY_READY:
					OnLobbyReady(packet);
					break;
				case ID_PLAYER_READY:
					DisplayPlayerReady(packet);
					break;
				case ID_Character_Selected:
					selectedCharacter(packet);
					break;
				case ID_Set_Players_Turn:
					setPlaying(packet);
					break;
				case ID_Attack_Player:
					attackPlayer(packet);
					break;
				case ID_Heal:
					healPlayer(packet);
					break;
				case ID_Request_Stats:
					givePlayerStats(packet);
					break;
				case ID_Recive_Stats:
					reciveStats(packet);
					break;
				case ID_Invalid_Target:
					invalidTarget(packet);
					break;
				case ID_Event_Info:
					reciveEvent(packet);
					break;
				case ID_End_Game:
					endGame(packet);
					break;
				default:
					std::cout << "error";
					break;
				}
			}
		}

		std::this_thread::sleep_for(std::chrono::microseconds(1000000));
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
				RakNet::SocketDescriptor socketDescriptors[1];
				socketDescriptors[0].port = SERVER_PORT;
				socketDescriptors[0].socketFamily = AF_INET; // Test out IPV4

				bool isSuccess = g_rakPeerInterface->Startup(MAX_CONNECTIONS, socketDescriptors, 1) == RakNet::RAKNET_STARTED;
				assert(isSuccess);
				g_rakPeerInterface->SetTimeoutTime(5000, RakNet::UNASSIGNED_SYSTEM_ADDRESS);
				//ensures we are server
				g_rakPeerInterface->SetMaximumIncomingConnections(MAX_CONNECTIONS);
				std::cout << "server started" << std::endl;
				g_networkState_mutex.lock();
				g_networkState = NS_Started;
				g_networkState_mutex.unlock();
			}
			//client
			else
			{
				RakNet::SocketDescriptor socketDescriptor(CLIENT_PORT, 0);
				socketDescriptor.socketFamily = AF_INET;

				while (RakNet::IRNS2_Berkley::IsPortInUse(socketDescriptor.port, socketDescriptor.hostAddress, socketDescriptor.socketFamily, SOCK_DGRAM) == true)
					socketDescriptor.port++;

				RakNet::StartupResult result = g_rakPeerInterface->Startup(8, &socketDescriptor, 1);
				assert(result == RakNet::RAKNET_STARTED);

				g_networkState_mutex.lock();
				g_networkState = NS_Started;
				g_networkState_mutex.unlock();

				g_rakPeerInterface->SetOccasionalPing(true);
				//"127.0.0.1" = local host = your machines address
				RakNet::ConnectionAttemptResult car = g_rakPeerInterface->Connect("127.0.0.1", SERVER_PORT, nullptr, 0);
				RakAssert(car == RakNet::CONNECTION_ATTEMPT_STARTED);
				std::cout << "client attempted connection..." << std::endl;

			}
		}

	}

	//std::cout << "press q and then return to exit" << std::endl;
	//std::cin >> userInput;

	inputHandler.join();
	packetHandler.join();
	return 0;
}