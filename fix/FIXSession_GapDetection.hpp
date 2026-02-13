#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <map>
#include <fstream>
#include <filesystem>
#include "FIXMessage.hpp"
#include "FIXSSLTransport.hpp"

namespace Chimera {

class FIXSession {
public:
    FIXSession(const std::string& sessionName)
        : sessionName_(sessionName)
        , outgoingSeqNum_(1)
        , expectedIncomingSeq_(1)
    {
        loadSeq();
    }

    void persistSeq() {
        std::filesystem::create_directories("seq_store");
        std::ofstream f("seq_store/" + sessionName_ + ".seq");
        f << outgoingSeqNum_ << "\n";
        f << expectedIncomingSeq_ << "\n";
        f.close();
    }

    void loadSeq() {
        std::filesystem::create_directories("seq_store");
        std::ifstream f("seq_store/" + sessionName_ + ".seq");
        if (f.good()) {
            f >> outgoingSeqNum_;
            f >> expectedIncomingSeq_;
        } else {
            outgoingSeqNum_ = 1;
            expectedIncomingSeq_ = 1;
        }
    }

    void sendResendRequest(uint32_t begin, uint32_t end) {
        std::string msg;
        msg += "35=2\001";
        msg += "49=" + senderCompID_ + "\001";
        msg += "56=" + targetCompID_ + "\001";
        msg += "34=" + std::to_string(outgoingSeqNum_++) + "\001";
        msg += "7=" + std::to_string(begin) + "\001";
        msg += "16=" + std::to_string(end) + "\001";
        
        sendRaw(msg);
        persistSeq();
    }

    void sendRaw(const std::string& msg) {
        std::lock_guard<std::mutex> lock(sendMutex_);
        
        // Store message for replay
        uint32_t currentSeq = outgoingSeqNum_ - 1;
        sentMessages_[currentSeq] = msg;
        
        // Trim old messages (keep last 1000)
        if (sentMessages_.size() > 1000) {
            auto it = sentMessages_.begin();
            sentMessages_.erase(it);
        }
        
        // Actually send
        transport_.send(msg);
    }

    bool validateIncomingSeq(uint32_t msgSeq) {
        std::lock_guard<std::mutex> lock(seqMutex_);
        
        if (msgSeq == expectedIncomingSeq_) {
            expectedIncomingSeq_++;
            persistSeq();
            return true;
        }
        else if (msgSeq > expectedIncomingSeq_) {
            // Gap detected - request resend
            sendResendRequest(expectedIncomingSeq_, msgSeq - 1);
            expectedIncomingSeq_ = msgSeq + 1;
            persistSeq();
            return false;
        }
        else if (msgSeq < expectedIncomingSeq_ - 50) {
            // Severe desync - force disconnect
            disconnect();
            return false;
        }
        else {
            // Duplicate - ignore
            return false;
        }
    }

    void handleResendRequest(uint32_t begin, uint32_t end) {
        std::lock_guard<std::mutex> lock(sendMutex_);
        
        for (uint32_t i = begin; i <= end; ++i) {
            if (sentMessages_.count(i)) {
                transport_.send(sentMessages_[i]);
            }
        }
    }

    uint32_t getOutgoingSeq() const { return outgoingSeqNum_; }
    uint32_t getExpectedIncomingSeq() const { return expectedIncomingSeq_; }

private:
    void disconnect() {
        // Trigger disconnect
        transport_.disconnect();
    }

    std::string sessionName_;
    std::string senderCompID_;
    std::string targetCompID_;
    
    uint32_t outgoingSeqNum_;
    uint32_t expectedIncomingSeq_;
    
    std::mutex sendMutex_;
    std::mutex seqMutex_;
    
    std::map<uint32_t, std::string> sentMessages_;
    FIXSSLTransport transport_;
};

} // namespace Chimera
