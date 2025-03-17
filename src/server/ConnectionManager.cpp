#include <ConnectionManager.hpp>
#include <common.hpp>
#include <responses.hpp>

ConnectionManager::ConnectionManager(SocketManager &socketManager, EventLoop &EventLoop,
                                     ClientIndex &clients)
    : _clients(clients)
    , _socketManager(socketManager)
    , _EventLoop(EventLoop)
{}

ConnectionManager::~ConnectionManager()
{
    disconnectAllClients();
}

void ConnectionManager::handleNewClient()
{
    sockaddr_in clientAddr;

    int clientFd = _socketManager.acceptConnection(&clientAddr);
    std::string ip = inet_ntoa(clientAddr.sin_addr);
    // add new client into ClientIndex
    _clients.add(clientFd);
    Client &client = _clients.getByFd(clientFd);
    client.setIp(ip);
    // add new client to epoll list
    _EventLoop.addToWatch(clientFd);
    std::cout << "New client" << std::endl;
    std::cout << "  Socket: " << clientFd << std::endl;
    std::cout << "  IP:     " << ip << std::endl;
}

void ConnectionManager::disconnectClient(Client &client)
{
    _EventLoop.removeFromWatch(client.getFd());
    _socketManager.closeConnection(client.getFd());
    _clients.remove(client);
}

void ConnectionManager::recieveData(int clientFd)
{
    Client &client = _clients.getByFd(clientFd);

    char buffer[MSG_BUFFER_SIZE];
    std::string &messageBuf = client.getMessageBuf();
    while (true) {
        int bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);

        if (bytesRead < 0) {
            if (errno == EPIPE) {
                std::cerr << "Broken pipe when sending to client " << clientFd << std::endl;
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data, exit the loop
                break;
            }
            else {
                // Handle error
                disconnectClient(client);
                return;
            }
        }
        else if (bytesRead == 0) {
            // Client disconnected
            disconnectClient(client);
            return;
        }

        messageBuf.append(buffer, bytesRead);
    }
    extractFullMessages(client, messageBuf);
}

// extract a valid IRC message with /r/n ending, send the message as std::string to commandHandler
// command handler will get the message WIHTOUT /r/n
void ConnectionManager::extractFullMessages(Client &client, std::string &messageBuffer)
{
    if (messageBuffer.size() > MSG_BUFFER_SIZE) {
        handleOversized(client, messageBuffer);
        return;
    }
    size_t pos;
    while ((pos = messageBuffer.find("\n")) != std::string::npos) {
        size_t end = pos;
        if (end > 0 && messageBuffer[end - 1] == '\r') {
            end--;
        }
        std::string completedMessage = messageBuffer.substr(0, end);
        messageBuffer.erase(0, pos + 1);
        // Call the command handler, currently just printing out the log message.
        MessageParser parser(client.getFd(), completedMessage);
        parser.parseCommand();
    }
}

void ConnectionManager::disconnectAllClients()
{
    _clients.forEachClient([this](Client &client) { disconnectClient(client); });
}

// if the message is over buffer limit, truncate and delete therest of the message.
void ConnectionManager::handleOversized(Client &client, std::string &messageBuffer)
{
    if (messageBuffer.size() > MSG_BUFFER_SIZE) {
        std::cerr << "Message too large from client: " << client.getFd() << std::endl;
        std::cerr << "Truncating message" << std::endl;
        std::string truncatedMessage = messageBuffer.substr(0, MSG_BUFFER_SIZE - 2);
        messageBuffer.clear();
        // call commandhandler, executer or whatever
        MessageParser parser(client.getFd(), truncatedMessage);
        parser.parseCommand();
    }
}
