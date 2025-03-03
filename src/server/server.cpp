#include <server.hpp>
#include <Client.hpp>
#include <message.hpp>
#include <common.hpp>
#include <ClientIndex.hpp>

Server *Server::instance = nullptr;

Server::Server()
    : port(SERVER_PORT)
    , serverFD(-1)
    , serverName(SERVER_NAME)
    , networkName(NETWORK_NAME)
    , serverVersion(SERVER_VERSION)
    , userModes(USER_MODES)
    , channelModes(CHANNEL_MODES)
    , clients(new ClientIndex())
    , _socketManager(std::make_unique<SocketManager>(SERVER_PORT))
    , _eventLoop(std::make_unique<EventLoop>())
{
    // get current time for server start time with chrono
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%a %d %b %H:%M:%S %Y", std::localtime(&now_time_t));
    createdTime = std::string(buffer);

    // setup signalshandlers
    setInstance(this);
    signal(SIGINT, signalHandler);  // Handle Ctrl+C
    signal(SIGTERM, signalHandler); // Handle termination request
    signal(SIGTSTP, SIG_IGN); // handle server pause
    signal(SIGPIPE, SIG_IGN);       // Ignore SIGPIPE (broken pipe)
}

Server::~Server()
{
    cleanup();
    delete clients;
}

void Server::start()
{
    serverFD = getSocketManager().initialize();
    getEventLoop().addToWatch(serverFD, EPOLLIN | EPOLLET);
    running = true;
    loop();
}

void Server::loop()
{
    while (running) {
        std::vector<Event> events = getEventLoop().waitForEvents(100);
        for (const Event &event : events) {
            if (event.fd == serverFD)
                handleNewClient();
            else {
                std::string msg = recieveMessage(event.fd);
                parseMessage(msg);
            }
        }
        if (paused) {
            std::cout << "Server paused. Waiting for SIGTSTP to resume..." << std::endl;
            while (paused && running) {
                sleep(1);
            }
            std::cout << "Server resumed!" << std::endl;
        }
    }
}

void Server::stop()
{
    running = false;
}

const int Server::getServerFD() const
{
    return this->serverFD;
}

const int Server::getPort() const
{
    return this->port;
}

ClientIndex *Server::getClients()
{
    return this->clients;
}

SocketManager &Server::getSocketManager()
{
    return *_socketManager;
}

EventLoop &Server::getEventLoop()
{
    return *_eventLoop;
}

void Server::setInstance(Server *server)
{
    instance = server;
}

void Server::pause()
{
    paused = true;
    std::cout << "Server pausing..." << std::endl;
}

void Server::resume()
{
    paused = false;
    std::cout << "Server resuming..." << std::endl;
}

void Server::signalHandler(int signum)
{
            std::cout << "\nCaught signal " << signum << std::endl;
            instance->stop();

    }


void Server::handleNewClient()
{
    sockaddr_in clientAddr;

    // accept new client connection
    int clientFD = getSocketManager().acceptConnection(&clientAddr);
    // get client's IP and add new Client to the map and poll list
    std::string ipAddress = inet_ntoa(clientAddr.sin_addr);

    Client *newClient = new Client(clientFD, ipAddress);
    clients->addUnregistered(newClient);
    getEventLoop().addToWatch(clientFD, EPOLLIN | EPOLLET);
    std::cout << "New client connected. Socket: " << clientFD << std::endl;
    sendWelcome(clientFD);
    std::cout << "Client's IP: " << ipAddress << std::endl;
}

void Server::removeClient(int fd)
{
    Client *client = clients->getByFd(fd);
    getEventLoop().removeFromWatch(fd);
    clients->remove(client);
    close(fd);
    delete client;
}

// ":<source> <command/REPL> <parameters> <crlf>"
void Server::sendWelcome(int clientFD)
{
    // 001 RPL_WELCOME
    std::stringstream welcome;
    welcome << ":" << serverName << " 001 " << clientFD << " :Welcome to " << networkName << " Network, " << clientFD
            << "\r\n";
    send(clientFD, welcome.str().c_str(), welcome.str().size(), 0);
    // 002 RPL_YOURHOST
    std::stringstream yourHost;
    yourHost << ":" << serverName << " 002 " << clientFD << " :Your host is " << serverName << ", running version "
             << serverVersion << "\r\n";
    send(clientFD, yourHost.str().c_str(), yourHost.str().size(), 0);
    // 003 RPL_CREATED
    std::stringstream created;
    created << ":" << serverName << " 003 " << clientFD << " :This server was created " << createdTime << "\r\n";
    send(clientFD, created.str().c_str(), created.str().size(), 0);
    // 004 RPL_MYINFO
    std::stringstream myInfo;
    myInfo << ":" << serverName << " 004 " << clientFD << " " << serverName << " " << serverVersion << " " << userModes
           << " " << channelModes << "\r\n";
    send(clientFD, myInfo.str().c_str(), myInfo.str().size(), 0);
}

void Server::parseMessage(std::string msg)
{
    std::cout << "Raw message: " << msg << std::endl;
    message parsedMessage(msg); // Use the new message struct!

    std::cout << "Parsed command: " << parsedMessage.command << std::endl;
    if (msg.find("quit") != std::string::npos) {
        stop();
    }
    else if (parsedMessage.command == "PRIVMSG") {
        std::cout << parsedMessage.prefix << " sent message to " << parsedMessage.parameters[0] << ": "
                  << parsedMessage.parameters[1] << std::endl;
    }
}

std::string Server::recieveMessage(int fd)
{
    char buffer[MSG_BUFFER_SIZE] = {0};
    std::string rawMessage;
    while (true) {
        int bytesRead = recv(fd, buffer, sizeof(buffer), 0);
        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data to read
                break;
            }
            else {
                // Error or client disconnected
                std::cerr << "Error reading from client: " << strerror(errno) << std::endl;
                removeClient(fd);
                return "oops";
            }
        }
        else if (bytesRead == 0) {
            // Client disconnected
            std::cout << "Client disconnected: " << fd << std::endl;
            removeClient(fd);
            return "oops";
        }
        else {
            rawMessage.append(buffer, bytesRead);
            if (rawMessage.size() > MSG_BUFFER_SIZE) {
                std::cerr << "Message too large from client: " << fd << std::endl;
                removeClient(fd);
                return "oops";
            }
        }
    }
    return rawMessage;
}

void Server::cleanup()
{
    std::unordered_map<int, Client *> &clientsByFd = clients->getClientsForCleanup();
    for (const auto &[fd, client] : clientsByFd) {
        close(fd);
        delete (client);
    }
    if (serverFD >= 0) {
        close(serverFD);
    }
    std::cout << "Server shutdown complete" << std::endl;
}
