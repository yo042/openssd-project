#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Embedded {
    enum nvme_opcode {
        NVME_CMD_WRITE                  = 0x01,
        NVME_CMD_READ                   = 0x02,
        // [prj1] New command opcode. Must match IO_NVM_HELLO in the firmware nvme.h
        NVME_CMD_HELLO                  = 0x03,
    };

    class Proj1 {
        public:
            Proj1() : fd_(-1) {}
            int Open(const std::string &dev);
            int ImageWrite(const std::vector<uint8_t> &buf);
            int ImageRead(std::vector<uint8_t> &buf, size_t size);
            int Hello();
        private:
            int fd_;
            // [prj1] Take only the fields the firmware reads (opcode, NSID, SLBA, NLB) plus the data buffer
            int nvme_passthru(uint8_t opcode,
                              uint32_t nsid,
                              uint64_t slba,
                              uint16_t nlb,
                              void* buf,
                              uint32_t buf_len);
    };
}
