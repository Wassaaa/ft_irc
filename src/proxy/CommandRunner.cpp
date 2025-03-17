#include <CommandRunner.hpp>
#include <unordered_set>

bool CommandRunner::_mapInitialized = false;
std::unordered_map<std::string, void (CommandRunner::*)()> CommandRunner::_commandRunners;

CommandRunner::CommandRunner(const MessageParser::CommandContext &ctx)
    : _server(Server::getInstance())
    , _channels(_server.getChannels())
    , _clients(_server.getClients())
    , _command(ctx.command)
    , _client(_clients.getByFd(ctx.clientFd))
    , _clientFd(ctx.clientFd)
    , _nickname(_client.getNickname())
    , _messageSource(ctx.source)
    , _params(ctx.params)
{
    if (!_mapInitialized)
        initCommandMap();
}

bool CommandRunner::validateCommandAccess()
{
    static const std::unordered_set<std::string> duplicateRegistration = {"PASS", "USER"};
    static const std::unordered_set<std::string> alwaysAllowedCommands = {"PASS", "QUIT", "CAP"};
    static const std::unordered_set<std::string> preRegistrationCommands = {"NICK", "USER", "PONG"};

    if (duplicateRegistration.find(_command) != duplicateRegistration.end()) {
        if (_client.getIsRegistered())
            sendToClient(_clientFd, ERR_ALREADYREGISTERED(_nickname));
        return false;
    }
    if (alwaysAllowedCommands.find(_command) != alwaysAllowedCommands.end()) {
        return true;
    }
    if (!_client.getPasswordVerified()) {
        sendToClient(_client.getFd(), ERR_NOTREGISTERED(_nickname));
        return false;
    }
    if (!_client.getIsRegistered() &&
        preRegistrationCommands.find(_command) != preRegistrationCommands.end()) {
        return true;
    }
    if (!_client.getIsRegistered()) {
        sendToClient(_client.getFd(), ERR_NOTREGISTERED(_nickname));
        return false;
    }
    return true;
}

void CommandRunner::execute()
{
    if (!validateCommandAccess()) {
        return;
    }

    auto commandIterator = _commandRunners.find(_command);
    if (commandIterator != _commandRunners.end()) {
        // Extract the command function pointer from the map
        auto commandFunction = commandIterator->second;
        (this->*commandFunction)();
    }
    else {
        sendToClient(_client.getFd(), ERR_UNKNOWNCOMMAND(_nickname, _command));
    }
}

bool CommandRunner::validateParams(size_t min, size_t max,
                                   std::array<ParamType, MAX_PARAMS> pattern)
{
    if (_params.size() < min) {
        if (_command == "NICK")
            sendToClient(_client.getFd(), ERR_NONICKNAMEGIVEN(_nickname));
        else
            sendToClient(_client.getFd(), ERR_NEEDMOREPARAMS(_nickname, _command));
        return false;
    }

    if (_params.size() > max) {
        // truncate silently
        _params.resize(max);
    }

    // Validate each parameter according to the pattern
    for (size_t i = 0; i < _params.size(); i++) {
        std::string &param = _params[i];

        switch (pattern[i]) {
        case NICK:
            if (!IRCValidator::isValidNickname(_client.getFd(), _nickname, param)) {
                return false;
            }
            break;

        case CHAN:
            if (!IRCValidator::isValidChannelName(_client.getFd(), _nickname, param)) {
                return false;
            }
            break;

        case MODE:
            if (!IRCValidator::isValidChannelMode()) {
                return false;
            }
            break;

        case USER:
            if (!IRCValidator::isValidUsername(_client.getFd(), _nickname, param)) {
                return false;
            }
            break;

        case KEY:
            if (!IRCValidator::isValidChannelKey()) {
                return false;
            }
            break;

        case PASS:
            if (!IRCValidator::isValidServerPassword()) {
                return false;
            }
            break;

        case REAL:
            // Usually no validation for realname
            break;

        case NOVAL:
            // No validation needed
            break;
        default:
            break;
        }
    }

    return true;
}

void CommandRunner::initCommandMap()
{
    _commandRunners["NICK"] = &CommandRunner::nick;
    _commandRunners["PASS"] = &CommandRunner::pass;
    _commandRunners["USER"] = &CommandRunner::user;
    _commandRunners["CAP"] = &CommandRunner::silentIgnore;
    // _commandRunners["LUSERS"] = &CommandRunner::lusers;
    // _commandRunners["MOTD"] = &CommandRunner::motd;
    _commandRunners["QUIT"] = &CommandRunner::quit;
    _commandRunners["JOIN"] = &CommandRunner::join;
    _commandRunners["PART"] = &CommandRunner::part;
    // _commandRunners["MODE"] = &CommandRunner::mode;
    _commandRunners["TOPIC"] = &CommandRunner::topic;
    // _commandRunners["INVITE"] = &CommandRunner::invite;
    // _commandRunners["KICK"] = &CommandRunner::kick;
    // _commandRunners["PING"] = &CommandRunner::ping;
    // _commandRunners["PONG"] = &CommandRunner::pong;
    // _commandRunners["PRIVMSG"] = &CommandRunner::privmsg;
    // _commandRunners["NOTICE"] = &CommandRunner::notice;
    // _commandRunners["WHO"] = &CommandRunner::who;
    // _commandRunners["WHOIS"] = &CommandRunner::whois;}
    _mapInitialized = true;
}

bool CommandRunner::canCompleteRegistration()
{
    return !_client.getIsRegistered() && _client.getNickname() != "*" &&
           !_client.getUsername().empty();
}

void CommandRunner::completeRegistration()
{
    _client.setIsRegistered(true);
    sendWelcome();
}

bool CommandRunner::tryRegisterClient()
{
    if (_client.getIsRegistered())
        return false;

    if (canCompleteRegistration()) {
        completeRegistration();
        return true;
    }

    return false;
}

void CommandRunner::sendWelcome()
{
    // Send the welcome messages
    sendToClient(_clientFd, RPL_WELCOME(_nickname));
    sendToClient(_clientFd, RPL_YOURHOST(_nickname));
    sendToClient(_clientFd, RPL_CREATED(_nickname, Server::getInstance().getCreatedTime()));
    sendToClient(_clientFd, RPL_MYINFO(_nickname));
    sendToClient(_clientFd, RPL_ISUPPORT(_nickname));
}
