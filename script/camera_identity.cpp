// Query the device once before the streaming driver opens it. No saved settings are written.
#include "nanolib.h"
#include <arpa/inet.h>
#include <cstdio>

int main(int argc, char **argv) {
    in_addr address{};
    if (argc != 2 || inet_pton(AF_INET, argv[1], &address) != 1) {
        std::fprintf(stderr, "Usage: camera_identity <camera IPv4 address>\n");
        return 2;
    }
    NslConfig config{};
    config.chipID = config.waferID = -1;
    const int handle = nsl_open(argv[1], &config, NslOption::FUNCTION_OPTIONS::FUNC_OFF);
    if (handle < 0) {
        std::fprintf(stderr, "Cannot query camera identity at %s (SDK %d)\n", argv[1], handle);
        return 1;
    }
    // nsl_open already reads NslConfig. Some SDK builds report an error enum from
    // a second getCurrentConfig call despite populating the structure successfully.
    nsl_closeHandle(handle);
    const int chip = config.chipID, wafer = config.waferID;
    if (chip < 0 || chip > 0xffff || wafer < 0 || wafer > 0xffff ||
            (chip == 0 && wafer == 0) || (chip == 0xffff && wafer == 0xffff)) {
        std::fprintf(stderr, "SDK returned invalid identity fields (%d, %d)\n", chip, wafer);
        return 1;
    }
    std::printf("{\"serial\":\"N%04X%04X\",\"chip_id\":%d,\"wafer_id\":%d,"
                "\"source\":\"ethernet_sdk_ids\",\"sensor_host\":\"%s\"}\n",
                chip, wafer, chip, wafer, argv[1]);
    return 0;
}
