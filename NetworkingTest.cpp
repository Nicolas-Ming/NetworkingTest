#include <iostream>
#include <enet/enet.h>
#include <string>
#include <vector>
#include <thread>
using namespace std;

#pragma comment(lib,"Ws2_32.lib")
#pragma comment(lib,"Winmm.lib")

#define NOMINMAX

ENetAddress address;
ENetHost* server = nullptr;
ENetHost* client = nullptr;
ENetPeer* peer;

ENetEvent event;


constexpr int OUTGOING_CONNECTIONS = 1;
constexpr auto SERVER_HOST = "127.0.0.1";
constexpr int SERVER_PORT = 1234;
constexpr int MAX_CLIENTS = 32;
constexpr int MAX_CHANNELS = 2;
constexpr int BANDWIDTH_INCOMING = 0; // 0 = unlimited
constexpr int BANDWIDTH_OUTGOING = 0; // 0 = unlimited
constexpr int TIMEOUT_MS = 1000;
constexpr int CONNECT_TIMEOUT_MS = 5000;
constexpr int LOOP_TIMEOUT_MS = 1000;
constexpr auto WELCOME_MESSAGE = "You're now connected to chat!\nType \"/help\" to see commands.\n";

constexpr auto COMMANDS =
"/guess - Guess a number for the guessing game. Ex. \"/guess 13\"\n"
"/help - Lists all commands\n"
"/play - Initiates the Number Guessing game\n"
"/quit - Exits the chatroom\n"
;

bool guessingGameRunning = false;
int guessingGameNumber = 0;

bool doneChatting = false;
string name;
string message = "";
bool sendPacket = false;

enum Colors
{
    REGULAR = 7,
    BLUE = 9,
    GREEN = 10,
    PURPLE = 13,
};

bool CreateClient();
void SendPacket(string message);
void GetInput();
bool CreateServer();
void SendMessageToClient(string message);
void SendMessageToAll(string message);
bool StartGuessingGame();
void CompleteGuessingGame(string user);
int GetRandomNumber(int min, int max);
bool ShouldSendRandomMessage();
string GetRandomMessage();
string GetMessageFromPacket(string data);
void MakeGuess(string user, int guess);
string GetUserFromPacket(string packet);
string GetCommandFromMessage(string message);

int main(int argc, char**argv)
{
    int input;
    cout << "1:Create a server" << endl << "2:Create a client" << endl;
    cin >> input;
    if (input == 1) {
        if (enet_initialize() != 0)
        {
            cout << "An error occurred while initializing ENet." << endl;
            return EXIT_FAILURE;
        }
        atexit(enet_deinitialize);

        if (!CreateServer())
        {
            cout << "An error occurred while trying to create an ENet server host." << endl;
            exit(EXIT_FAILURE);
        }

        cout << "Server running..." << endl;

        while (1)
        {
            while (enet_host_service(server, &event, TIMEOUT_MS) > 0)
            {
                switch (event.type)
                {
                case ENET_EVENT_TYPE_CONNECT:
                {
                    cout << "A new client connected from " << event.peer->address.host << ":" << event.peer->address.port << endl;
                    event.peer->data = (void*)("Client information");
                    SendMessageToClient(WELCOME_MESSAGE);
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE:
                {
                    cout << "Received a packet of length " << event.packet->dataLength << " containing: " << (char*)event.packet->data << endl;

                    string packet = (char*)event.packet->data;
                    string user = GetUserFromPacket(packet);
                    string message = GetMessageFromPacket((char*)event.packet->data);

                    if (message[0] == '/')
                    {
                        // get full command
                        string command = GetCommandFromMessage(message);

                        if (command == "/guess")
                        {
                            if (!guessingGameRunning) SendMessageToClient("The guessing game is not currently active. Type \"/play\" to start.");
                            else MakeGuess(user, stoi(message.substr(7, message.length() - 6)));
                        }
                        else if (command == "/play")
                        {
                            if (StartGuessingGame()) SendMessageToAll(user + " has started a new round of the Guessing Game! Type \"/guess #\" to make a guess!");
                            else SendMessageToClient("The Guessing Game is already active. Make your guess with \"/guess #\"");
                        }
                        else if (command == "/quit")
                        {
                            SendMessageToAll(user + " has left the chatroom.");
                        }
                        else SendMessageToClient(COMMANDS);
                    }
                    else
                    {
                        // send the received message to everyone
                        SendMessageToAll((char*)event.packet->data);

                        // check if we should respond back to the client that sent the message with a random message
                        if (ShouldSendRandomMessage()) SendMessageToClient(GetRandomMessage());
                    }

                    // Clean up the packet now that we're done using it.
                    enet_packet_destroy(event.packet);
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                    // Reset the peer's client information
                    cout << (char*)event.peer->data << " disconnected." << endl;
                    event.peer->data = NULL;
                }
            }
        }
    }
    else {
        // CLIENT SIDE
        if (server != nullptr) enet_host_destroy(server);

        cout << "In order to connect to the server." << endl;
        cout << "Enter your name: ";
        getline(cin, name);

        if (enet_initialize() != 0)
        {
            cout << "An error occurred while initializing ENet." << endl;
            return EXIT_FAILURE;
        }
        atexit(enet_deinitialize);

        if (!CreateClient())
        {
            cout << "An error occurred while trying to create an ENet client host." << endl;
            exit(EXIT_FAILURE);
        }

        // connect to the server
        enet_address_set_host(&address, SERVER_HOST);
        address.port = SERVER_PORT;
        peer = enet_host_connect(client, &address, MAX_CHANNELS, 0);

        if (peer == NULL)
        {
            cout << "No available peers for initiating an ENet connection." << endl;
            exit(EXIT_FAILURE);
        }

        // Wait up to CONNECT_TIME_MS for the connection attempt to succeed
        if (enet_host_service(client, &event, CONNECT_TIMEOUT_MS) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
            cout << "Connection to " << SERVER_HOST << ":" << SERVER_PORT << " succeeded." << endl;
        else
        {
            // Either CONNECT_TIMEOUT_MS was reached or the client disconnected
            enet_peer_reset(peer);
            cout << "Connection to " << SERVER_HOST << ":" << SERVER_PORT << " failed." << endl;
        }

        thread InputThread(GetInput);

        while (!doneChatting)
        {
            /* Wait up to 1000 milliseconds for an event. */
            while (enet_host_service(client, &event, LOOP_TIMEOUT_MS) > 0)
            {
                switch (event.type)
                {
                case ENET_EVENT_TYPE_CONNECT:
                {
                    string packetString = name + " has connected.";
                    SendPacket(packetString);
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE:
                    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

                    string receivedMessage = (char*)event.packet->data;
                    string user = receivedMessage.substr(0, receivedMessage.find(":"));

                    if (user == name) SetConsoleTextAttribute(console, Colors::BLUE);
                    else if (user == "Private Message from Anonymous") SetConsoleTextAttribute(console, Colors::PURPLE);
                    else if (receivedMessage.find(":") == string::npos) SetConsoleTextAttribute(console, Colors::GREEN);

                    cout << receivedMessage << endl;
                    SetConsoleTextAttribute(console, Colors::REGULAR);

                    /* Clean up the packet now that we're done using it. */
                    enet_packet_destroy(event.packet);
                }
            }
        }

        if (client != nullptr) enet_host_destroy(client);

        InputThread.join();
    }
    return EXIT_SUCCESS;
}

bool CreateServer()
{
    // Bind the server to the default `localhost` on port `1234`
    // The host address can be set with `enet_address_set_host(&address, "x.x.x.x");`
    address.host = ENET_HOST_ANY;
    address.port = SERVER_PORT;
    server = enet_host_create(&address, MAX_CLIENTS, MAX_CHANNELS, BANDWIDTH_INCOMING, BANDWIDTH_OUTGOING);
    return server != nullptr;
}

void SendMessageToClient(string message)
{
    const char* msg = message.c_str();
    ENetPacket* packet = enet_packet_create(msg, strlen(msg) + 1, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(event.peer, 0, packet);
    enet_host_flush(server);
}

void SendMessageToAll(string message)
{
    const char* msg = message.c_str();
    ENetPacket* packet = enet_packet_create(msg, strlen(msg) + 1, ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(server, 0, packet);
    enet_host_flush(server);
}

bool StartGuessingGame()
{
    if (!guessingGameRunning)
    {
        guessingGameRunning = true;
        guessingGameNumber = GetRandomNumber(1, 100);
        return true;
    }

    return false;
}

void CompleteGuessingGame(string user)
{
    guessingGameRunning = false;
    SendMessageToAll(user + " has guessed the number correctly! Congratulations!");
}

int GetRandomNumber(int min, int max)
{
    return rand() % max + min;
}

bool ShouldSendRandomMessage()
{
    return (rand() % 10) + 1 == 5;
}

string GetRandomMessage()
{
    int i = (rand() % 5) + 1;
    string randomMessage = "Private Message from Anonymous: ";

    if (i == 1) randomMessage += "Pressing Alt+F4 on your keyboard seems to give you money.";
    else if (i == 2) randomMessage += "Why is Zelda always trying to save the princess?";
    else if (i == 3) randomMessage += "Why is the Lich King's mount called Invincible when you can see it?";
    else if (i == 4) randomMessage += "Why is it called an Xbox 360? Because you turn 360 degrees and walk away.";
    else randomMessage += "Why didn't Harry Potter save Middle-earth at the end of Star Wars?";

    return randomMessage;
}

string GetMessageFromPacket(string data)
{
    string delimiter = ": ";
    size_t pos = 0;
    while ((pos = data.find(delimiter)) != string::npos) data.erase(0, pos + delimiter.length());
    return data;
}

void MakeGuess(string user, int guess)
{
    SendMessageToAll(user + " is making a guess of " + to_string(guess) + ".");

    if (guess == guessingGameNumber) CompleteGuessingGame(user);
    else
    {
        // incorrect, determine if too high or too low
        if (guess > guessingGameNumber) SendMessageToAll(to_string(guess) + " is too high!");
        else SendMessageToAll(to_string(guess) + " is too low!");
    }
}

string GetUserFromPacket(string packet)
{
    return packet.substr(0, packet.find(":"));
}

string GetCommandFromMessage(string message)
{
    return message.substr(0, message.find(" "));
}
void GetInput()
{

    while (!doneChatting)
    {
        cin.clear();
        getline(cin, message);

        if (message == "/quit")
        {
            doneChatting = true;
            cout << "Exitting chat..." << endl;
        }

        // let's back up so new incoming input will overwrite what the user typed
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO cbsi;
        short x = 0;
        short y = 0;

        if (GetConsoleScreenBufferInfo(output, &cbsi))
        {
            x = cbsi.dwCursorPosition.X;
            y = cbsi.dwCursorPosition.Y - 1;
        }
        COORD pos = { x, y };
        SetConsoleCursorPosition(output, pos);

        // create and send the message
        message = name + ": " + message;
        SendPacket(message);
        message = "";
    }
}
bool CreateClient()
{
    client = enet_host_create(NULL, OUTGOING_CONNECTIONS, MAX_CHANNELS, BANDWIDTH_INCOMING, BANDWIDTH_OUTGOING);
    return client != nullptr;
}

void SendPacket(string message)
{
    const char* msg = message.c_str();
    ENetPacket* packet = enet_packet_create(msg, strlen(msg) + 1, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, packet);
    enet_host_flush(client);
}