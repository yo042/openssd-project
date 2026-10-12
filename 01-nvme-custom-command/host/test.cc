#include "nvme_passthru.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <unistd.h>

using namespace std;
using namespace Embedded;

#define PAGE_SIZE 4096
// [prj1] Device path on the lab PC
#define DEV_PATH "/dev/nvme0n1"  /* Device path (check with lsblk or sudo nvme list, and modify if needed) */

// [prj1] Run the HELLO command (Task 3). Comment this out to run image write/read (Task 2)
#define TASK3_ON

#ifndef TASK3_ON
// ------------------------
// Task 2: Image Write/Read
// ------------------------
static vector<uint8_t> read_file(const string &path) {
    ifstream ifs(path, ios::binary);
    if (!ifs) {
        cerr << "Failed to open input file: " << path << endl;
        exit(1);
    }
    return vector<uint8_t>((istreambuf_iterator<char>(ifs)), {});
}

static void write_file(const string &path, const vector<uint8_t> &data) {
    ofstream ofs(path, ios::binary);
    if (!ofs) {
        cerr << "Failed to open output file: " << path << endl;
        exit(1);
    }
    ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
}
#endif

int main(int argc, char *argv[]) {
    Proj1 dev;
    if (dev.Open(DEV_PATH) < 0) {
        cerr << "Device open failed" << endl;
        return -1;
    }

#ifdef TASK3_ON
    // ------------------------
    // Task 3: Custom NVMe Command
    // ------------------------
    cout << "Sending custom HELLO command to device..." << endl;

    if (dev.Hello() < 0) {
        cerr << "HELLO command failed" << endl;
        return -1;
    }

    cout << "HELLO command sent successfully. Check 'tio' output for device message." << endl;
    return 0;

#else
    // ------------------------
    // Task 2: Image Write/Read
    // ------------------------
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <input_img> <output_img>" << endl;
        return -1;
    }

    string input_path = argv[1];
    string output_path = argv[2];

    vector<uint8_t> src_data = read_file(input_path);

    cout << "Writing " << src_data.size() << " bytes to device..." << endl;
    if (dev.ImageWrite(src_data) < 0) {
        cerr << "ImageWrite failed" << endl;
        return -1;
    }

    vector<uint8_t> dst_data;
    cout << "Reading back from device..." << endl;
    if (dev.ImageRead(dst_data, src_data.size()) < 0) {
        cerr << "ImageRead failed" << endl;
        return -1;
    }

    write_file(output_path, dst_data);

    if (src_data.size() != dst_data.size()) {
        cerr << "Size mismatch!" << endl;
        return -1;
    }
    if (memcmp(src_data.data(), dst_data.data(), src_data.size()) == 0) {
        cout << "SUCCESS: Files are identical." << endl;
    } else {
        cout << "FAIL: Files differ!" << endl;
    }

    return 0;
#endif
}
