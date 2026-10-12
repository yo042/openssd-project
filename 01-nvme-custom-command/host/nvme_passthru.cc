#include "nvme_passthru.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>
#include <fcntl.h>
#include <cstring>
#include <cstdint>
#include <stdlib.h>
#include <vector>
#include <iostream>
#include <cstdio>
#include <inttypes.h>

using namespace std;

const unsigned int PAGE_SIZE = 4096;
const unsigned int MAX_BUFLEN = 16*1024*1024; /* Maximum transfer size (can be adjusted if needed) */
const unsigned int NSID = 1; /* NSID can be checked using 'sudo nvme list' */

int Embedded::Proj1::Open(const std::string &dev) {
    int err;
    err = open(dev.c_str(), O_RDONLY);
    if (err < 0)
        return -1;
    fd_ = err;

    struct stat nvme_stat;
    err = fstat(fd_, &nvme_stat);
    if (err < 0)
        return -1;
    if (!S_ISCHR(nvme_stat.st_mode) && !S_ISBLK(nvme_stat.st_mode))
        return -1;

    return 0;
}

// [prj1] Write the image to the SSD, one 4KB LBA per NVMe WRITE command
int Embedded::Proj1::ImageWrite(const std::vector<uint8_t> &buf) {
    if (buf.empty()) return -EINVAL;
    if (buf.size() > MAX_BUFLEN) {
        cerr << "[ERROR] Image size exceeds the maximum transfer size limit." << endl;
        return -EINVAL;
    }

    // [prj1] Number of LBAs needed (rounded up), start LBA, buffer offset
    uint64_t total_nlb = (buf.size() + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t slba = 0;
    size_t off = 0;

    // [prj1] Send one LBA (4KB) per command, in order
    while (total_nlb > 0) {
        uint32_t nlb = 1;
        uint32_t bytes = nlb * PAGE_SIZE;

        // [prj1] The last chunk may be smaller than 4KB, so copy it into a zero-filled 4KB buffer.
        //        The firmware sets up DMA from NLB in 4KB units, not from data_len.
        size_t copy_bytes = min<size_t>(bytes, buf.size() - off);
        vector<uint8_t> bounce(bytes, 0);
        memcpy(bounce.data(), buf.data() + off, copy_bytes);

        int rc = nvme_passthru(NVME_CMD_WRITE, NSID, slba, nlb,
                               bounce.data(), bytes);
        if (rc < 0) return rc;

        off       += copy_bytes;
        slba      += nlb;
        total_nlb -= nlb;
    }

    return 0;
}

// [prj1] Read back the same LBAs (from 0) and rebuild the original size
int Embedded::Proj1::ImageRead(std::vector<uint8_t> &buf, size_t size) {
    if (size == 0) return -EINVAL;
    if (size > MAX_BUFLEN) {
        cerr << "[ERROR] Requested read size exceeds the maximum transfer size limit." << endl;
        return -EINVAL;
    }

    // [prj1] The caller passes an empty vector, so resize it first
    buf.resize(size);

    uint64_t total_nlb = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t slba = 0;
    size_t off = 0;

    while (total_nlb > 0) {
        uint32_t nlb = 1;
        uint32_t bytes = nlb * PAGE_SIZE;

        vector<uint8_t> bounce(bytes, 0);

        int rc = nvme_passthru(NVME_CMD_READ, NSID, slba, nlb,
                               bounce.data(), bytes);
        if (rc < 0) return rc;

        // [prj1] Copy only the real size of the last chunk (drop the padding)
        size_t copy_bytes = min<size_t>(bytes, size - off);
        memcpy(buf.data() + off, bounce.data(), copy_bytes);

        off       += copy_bytes;
        slba      += nlb;
        total_nlb -= nlb;
    }
    return 0;
}

// [prj1] Send the new HELLO command. No data transfer, so LBA and buffer are unused
int Embedded::Proj1::Hello() {
    int rc = nvme_passthru(NVME_CMD_HELLO, NSID, 0, 0, nullptr, 0);
    if (rc < 0) return rc;

    return 0;
}

// [prj1] Common helper for all I/O: fill nvme_passthru_cmd and pass it to the kernel NVMe driver via ioctl
int Embedded::Proj1::nvme_passthru(uint8_t opcode,
                                   uint32_t nsid,
                                   uint64_t slba,
                                   uint16_t nlb,
                                   void* buf,
                                   uint32_t buf_len)
{
    struct nvme_passthru_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.opcode   = opcode;
    cmd.nsid     = nsid;
    cmd.addr     = reinterpret_cast<__u64>(buf);
    cmd.data_len = buf_len;

    // [prj1] Split the 64-bit start LBA into cdw10 (low 32 bits) and cdw11 (high 32 bits).
    //        The firmware reads them from dword[10] and dword[11].
    cmd.cdw10 = static_cast<__u32>(slba & 0xFFFFFFFFull);
    cmd.cdw11 = static_cast<__u32>((slba >> 32) & 0xFFFFFFFFull);

    // [prj1] NLB is 0-based (0 = 1 block). The firmware uses nlb + 1, so send nlb - 1.
    cmd.cdw12 = static_cast<__u32>((nlb - 1) & 0xFFFF);

    cmd.timeout_ms = 0;

    int ret = ioctl(fd_, NVME_IOCTL_IO_CMD, &cmd);
    if (ret < 0) {
        perror("ioctl(NVME_IOCTL_IO_CMD)");
        return -errno;
    }
    return 0;
}
