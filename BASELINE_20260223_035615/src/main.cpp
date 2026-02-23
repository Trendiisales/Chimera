#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <map>

#pragma comment(lib, "ws2_32.lib")

struct Config {
    std::string host;
    int quote_port = 0;
    std::string sender;
    std::string target;
    std::string username;
    std::string password;
    int heartbeat = 30;
};

Config g_cfg;
std::atomic<bool> g_running(true);

std::map<int,std::string> g_idToName;
std::map<std::string,double> g_bid;
std::map<std::string,double> g_ask;

static std::string trim(std::string s)
{
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}

bool load_config(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    bool in_fix = false;

    while (std::getline(f, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        if (line == "[fix]") { in_fix = true; continue; }
        if (line[0] == '[' && line != "[fix]") { in_fix = false; continue; }

        if (!in_fix)
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "host") g_cfg.host = val;
        if (key == "port") g_cfg.quote_port = std::stoi(val);
        if (key == "sender_comp_id") g_cfg.sender = val;
        if (key == "target_comp_id") g_cfg.target = val;
        if (key == "username") g_cfg.username = val;
        if (key == "password") g_cfg.password = val;
        if (key == "heartbeat_interval") g_cfg.heartbeat = std::stoi(val);
    }

    return !g_cfg.host.empty() && g_cfg.quote_port != 0;
}

std::string timestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm gmt{};
    gmtime_s(&gmt, &now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H:%M:%S", &gmt);
    return buf;
}

int checksum(const std::string& msg)
{
    int sum = 0;
    for (unsigned char c : msg) sum += c;
    return sum % 256;
}

std::string wrap_fix(const std::string& body)
{
    std::stringstream msg;
    msg << "8=FIX.4.4\x01"
        << "9=" << body.size() << "\x01"
        << body;

    std::string base = msg.str();
    msg << "10=" << std::setw(3) << std::setfill('0') << checksum(base) << "\x01";
    return msg.str();
}

std::string build_logon(int& seq)
{
    std::stringstream body;
    body << "35=A\x01"
         << "49=" << g_cfg.sender << "\x01"
         << "56=" << g_cfg.target << "\x01"
         << "50=QUOTE\x01"
         << "57=QUOTE\x01"
         << "34=" << seq++ << "\x01"
         << "52=" << timestamp() << "\x01"
         << "98=0\x01"
         << "108=" << g_cfg.heartbeat << "\x01"
         << "141=Y\x01"
         << "553=" << g_cfg.username << "\x01"
         << "554=" << g_cfg.password << "\x01";

    return wrap_fix(body.str());
}

std::string build_security_list_req(int& seq)
{
    std::stringstream body;
    body << "35=x\x01"
         << "49=" << g_cfg.sender << "\x01"
         << "56=" << g_cfg.target << "\x01"
         << "50=QUOTE\x01"
         << "57=QUOTE\x01"
         << "34=" << seq++ << "\x01"
         << "52=" << timestamp() << "\x01"
         << "320=REQ1\x01"
         << "559=0\x01";

    return wrap_fix(body.str());
}

std::string build_md_request(int& seq, int symbolId)
{
    std::stringstream body;
    body << "35=V\x01"
         << "49=" << g_cfg.sender << "\x01"
         << "56=" << g_cfg.target << "\x01"
         << "50=QUOTE\x01"
         << "57=QUOTE\x01"
         << "34=" << seq++ << "\x01"
         << "52=" << timestamp() << "\x01"
         << "262=MD" << symbolId << "\x01"
         << "263=1\x01"
         << "264=1\x01"
         << "146=1\x01"
         << "55=" << symbolId << "\x01"
         << "267=2\x01"
         << "269=0\x01"
         << "269=1\x01";

    return wrap_fix(body.str());
}

void print_prices()
{
    if (g_bid.count("XAUUSD") && g_ask.count("XAUUSD"))
        std::cout << "XAU  " << g_bid["XAUUSD"] << " / " << g_ask["XAUUSD"] << "\n";

    if (g_bid.count("XAGUSD") && g_ask.count("XAGUSD"))
        std::cout << "XAG  " << g_bid["XAGUSD"] << " / " << g_ask["XAGUSD"] << "\n";
}

SOCKET connect_tcp(int port)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(g_cfg.host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0)
        return INVALID_SOCKET;

    SOCKET s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (s == INVALID_SOCKET)
    {
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    if (connect(s, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR)
    {
        closesocket(s);
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    freeaddrinfo(result);
    return s;
}

void quote_session()
{
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SOCKET sock = connect_tcp(g_cfg.quote_port);
    if (sock == INVALID_SOCKET)
    {
        std::cout << "QUOTE TCP FAILED\n";
        return;
    }

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, (int)sock);

    if (SSL_connect(ssl) <= 0)
    {
        std::cout << "QUOTE SSL FAILED\n";
        closesocket(sock);
        return;
    }

    std::cout << "CONNECTED\n";

    int seq = 1;
    std::string logon = build_logon(seq);
    SSL_write(ssl, logon.c_str(), logon.size());

    char buffer[8192];

    while (g_running)
    {
        int bytes = SSL_read(ssl, buffer, sizeof(buffer));
        if (bytes <= 0)
            break;

        std::string msg(buffer, bytes);

        if (msg.find("35=A") != std::string::npos)
        {
            std::string req = build_security_list_req(seq);
            SSL_write(ssl, req.c_str(), req.size());
        }

        if (msg.find("35=y") != std::string::npos)
        {
            size_t pos = 0;
            while ((pos = msg.find("1007=", pos)) != std::string::npos)
            {
                size_t end = msg.find("\x01", pos);
                std::string name = msg.substr(pos + 5, end - (pos + 5));

                size_t idPos = msg.rfind("55=", pos);
                if (idPos != std::string::npos)
                {
                    size_t idEnd = msg.find("\x01", idPos);
                    int id = std::stoi(msg.substr(idPos + 3, idEnd - (idPos + 3)));

                    if (name == "XAUUSD" || name == "XAGUSD")
                        g_idToName[id] = name;
                }
                pos = end;
            }

            for (auto& p : g_idToName)
            {
                std::string req = build_md_request(seq, p.first);
                SSL_write(ssl, req.c_str(), req.size());
            }
        }

        if (msg.find("35=W") != std::string::npos)
        {
            size_t idPos = msg.find("55=");
            if (idPos == std::string::npos) continue;
            size_t idEnd = msg.find("\x01", idPos);
            int id = std::stoi(msg.substr(idPos + 3, idEnd - (idPos + 3)));

            if (!g_idToName.count(id)) continue;
            std::string name = g_idToName[id];

            size_t p = 0;
            while ((p = msg.find("269=", p)) != std::string::npos)
            {
                char type = msg[p + 4];
                size_t pricePos = msg.find("270=", p);
                if (pricePos != std::string::npos)
                {
                    size_t priceEnd = msg.find("\x01", pricePos);
                    double price = std::stod(msg.substr(pricePos + 4, priceEnd - (pricePos + 4)));

                    if (type == '0') g_bid[name] = price;
                    if (type == '1') g_ask[name] = price;
                }
                p += 5;
            }

            print_prices();
        }

        if (msg.find("35=1") != std::string::npos)
        {
            size_t p = msg.find("112=");
            if (p != std::string::npos)
            {
                size_t end = msg.find("\x01", p);
                std::string testID = msg.substr(p + 4, end - (p + 4));

                std::stringstream hb;
                hb << "35=0\x01"
                   << "49=" << g_cfg.sender << "\x01"
                   << "56=" << g_cfg.target << "\x01"
                   << "50=QUOTE\x01"
                   << "57=QUOTE\x01"
                   << "34=" << seq++ << "\x01"
                   << "52=" << timestamp() << "\x01"
                   << "112=" << testID << "\x01";

                std::string reply = wrap_fix(hb.str());
                SSL_write(ssl, reply.c_str(), reply.size());
            }
        }
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    closesocket(sock);
}

int main(int argc, char* argv[])
{
    std::string config = (argc > 1) ? argv[1] : "..\\..\\config.ini";

    if (!load_config(config))
    {
        std::cout << "CONFIG LOAD FAILED\n";
        return 1;
    }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SSL_library_init();
    SSL_load_error_strings();

    std::thread q(quote_session);

    while (g_running)
        Sleep(1000);

    q.join();
    WSACleanup();
    return 0;
}
