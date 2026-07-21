#include "../include/TcpClient.hpp"
#include "../include/Logger.h"
#include <iostream>
#include <cstring>
#include <vector>

TcpClient::TcpClient() 
    : socket(nullptr), cmsHost(""), cmsPort(0), connected(false) {
}

TcpClient::TcpClient(const std::string& host, kissnet::port_t port)
    : socket(nullptr), cmsHost(host), cmsPort(port), connected(false) {
    connect(host, port);
}

TcpClient::~TcpClient() {
    disconnect();
}

bool TcpClient::connect(const std::string& host, kissnet::port_t port) {
    try {
        if (connected) {
            LOG_WARN("Already connected. Disconnecting first.");
            disconnect();
        }
        
        LOG_INFO("Connecting to CMS at " + host + ":" + std::to_string(port));
        
        socket = std::make_unique<kissnet::tcp_socket>(kissnet::endpoint(host, port));
        
        auto status = socket->connect();
        
        if (status == kissnet::socket_status::valid) {
            cmsHost = host;
            cmsPort = port;
            connected = true;
            LOG_INFO("Successfully connected to CMS");
            return true;
        } else {
            LOG_ERROR("Failed to connect to CMS at " + host + ":" + std::to_string(port));
            socket.reset();
            connected = false;
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while connecting to CMS: " + std::string(e.what()));
        socket.reset();
        connected = false;
        return false;
    }
}

bool TcpClient::disconnect() {
    if (socket && connected) {
        LOG_INFO("Disconnecting from CMS");
        socket.reset();
        connected = false;
        return true;
    }
    return false;
}

bool TcpClient::send(const std::string& message) {
    return send(reinterpret_cast<const std::byte*>(message.data()), message.size());
}

bool TcpClient::send(const std::byte* data, size_t length) {
    if (!connected || !socket) {
        LOG_ERROR("Cannot send: not connected to CMS");
        return false;
    }
    
    try {
        auto [bytesSent, status] = socket->send(data, length);
        
        if (status == kissnet::socket_status::valid) {
            LOG_DEBUG("Sent " + std::to_string(bytesSent) + " bytes to CMS");
            return bytesSent == length;
        } else {
            LOG_ERROR("Failed to send data to CMS");
            if (status == kissnet::socket_status::errored) {
                connected = false;
            }
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while sending: " + std::string(e.what()));
        connected = false;
        return false;
    }
}

std::string TcpClient::receive(size_t maxBytes) {
    kissnet::buffer<4096> buffer;
    size_t bytesReceived = receiveRaw(buffer.data(), std::min(maxBytes, size_t(4096)));
    
    if (bytesReceived > 0) {
        return std::string(reinterpret_cast<const char*>(buffer.data()), bytesReceived);
    }
    
    return "";
}

size_t TcpClient::receiveRaw(std::byte* buffer, size_t maxBytes) {
    if (!connected || !socket) {
        LOG_ERROR("Cannot receive: not connected to CMS");
        return 0;
    }
    
    try {
        auto [bytesReceived, status] = socket->recv(buffer, maxBytes);
        
        if (status == kissnet::socket_status::valid) {
            if (bytesReceived > 0) {
                LOG_DEBUG("Received " + std::to_string(bytesReceived) + " bytes from CMS");
            }
            return bytesReceived;
        } else if (status == kissnet::socket_status::cleanly_disconnected) {
            LOG_INFO("CMS closed connection cleanly");
            connected = false;
            return 0;
        } else {
            LOG_ERROR("Failed to receive data from CMS");
            connected = false;
            return 0;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while receiving: " + std::string(e.what()));
        connected = false;
        return 0;
    }
}

// Reads exactly n bytes into buf. Returns false if connection drops.
bool TcpClient::recvExact(std::byte* buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        auto [got, status] = socket->recv(buf + done, n - done);
        if (status == kissnet::socket_status::cleanly_disconnected || got == 0) {
            connected = false;
            return false;
        }
        if (status == kissnet::socket_status::errored) {
            connected = false;
            return false;
        }
        done += got;
    }
    return true;
}

void TcpClient::startReceiveLoop(CmsPacketCallback cb) {
    if (receiveRunning) return;
    packetCallback = cb;
    receiveRunning = true;
    receiveThread  = std::thread(&TcpClient::receiveLoop, this);
}

void TcpClient::stopReceiveLoop() {
    receiveRunning = false;
    if (receiveThread.joinable()) {
        receiveThread.join();
    }
}

void TcpClient::receiveLoop() {
    LOG_INFO("CMS receive loop started");

    // CmsMessageHeader layout: int32_t length (4) + int16_t messageType (2) = 6 bytes
    constexpr size_t HEADER_SIZE = 6;

    while (receiveRunning && connected) {
        if (!socket) break;

        // 1. Read 6-byte header
        std::byte headerBuf[HEADER_SIZE];
        if (!recvExact(headerBuf, HEADER_SIZE)) {
            LOG_WARN("CMS receive loop: connection lost reading header");
            break;
        }

        int32_t  length;
        int16_t  messageType;
        std::memcpy(&length,      headerBuf,     4);
        std::memcpy(&messageType, headerBuf + 4, 2);

        if (length < static_cast<int32_t>(HEADER_SIZE) || length > 65536) {
            LOG_ERROR("CMS receive loop: invalid packet length " + std::to_string(length));
            break;
        }

        // 2. Read payload (everything after the header)
        size_t payloadLen = static_cast<size_t>(length) - HEADER_SIZE;
        std::vector<std::byte> payload(payloadLen);

        if (payloadLen > 0 && !recvExact(payload.data(), payloadLen)) {
            LOG_WARN("CMS receive loop: connection lost reading payload");
            break;
        }

        // 3. Dispatch to callback
        if (packetCallback) {
            packetCallback(messageType, payload.data(), payloadLen);
        }
    }

    LOG_INFO("CMS receive loop stopped");
}
