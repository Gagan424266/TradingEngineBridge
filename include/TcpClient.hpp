#ifndef TCP_CLIENT_HPP
#define TCP_CLIENT_HPP

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include "../kissnet.hpp"

// Callback invoked on each complete packet received from CMS.
// Parameters: messageType, raw payload bytes (after header), payload length
using CmsPacketCallback = std::function<void(int16_t messageType, const std::byte* payload, size_t payloadLen)>;

class TcpClient {
private:
    std::unique_ptr<kissnet::tcp_socket> socket;
    std::string cmsHost;
    kissnet::port_t cmsPort;
    bool connected;

    std::thread receiveThread;
    std::atomic<bool> receiveRunning {false};
    CmsPacketCallback packetCallback;

    void receiveLoop();
    bool recvExact(std::byte* buf, size_t n);

public:
    TcpClient();
    TcpClient(const std::string& host, kissnet::port_t port);
    ~TcpClient();

    bool connect(const std::string& host, kissnet::port_t port);
    bool disconnect();
    bool isConnected() const { return connected; }

    bool send(const std::string& message);
    bool send(const std::byte* data, size_t length);

    std::string receive(size_t maxBytes = 4096);
    size_t receiveRaw(std::byte* buffer, size_t maxBytes);

    // Starts a background thread that reads incoming CMS packets and calls cb.
    void startReceiveLoop(CmsPacketCallback cb);
    void stopReceiveLoop();

    std::string getHost() const { return cmsHost; }
    kissnet::port_t getPort() const { return cmsPort; }
};

#endif // TCP_CLIENT_HPP
