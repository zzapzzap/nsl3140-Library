// change_camera_ip.cpp
// Connects to NSL-3130AA via Vendor USB (1fc9:0099) and changes IP/netmask/gateway.
//
// SDK docs: for Vendor USB mode, pass "" to auto-detect, or serial number (e.g. N008B060D).
// CDC mode (1fc9:0094): pass /dev/ttyACM0 or /dev/ttyLidar.
//
// Build:
//   g++ -std=c++17 -I../../../NSL3130_driver/src/roboscan_nsl3130/nsl_lib/include
//       -L../../../NSL3130_driver/src/roboscan_nsl3130/nsl_lib/lib/linux-7.5/shared
//       -Wl,-rpath,../../../NSL3130_driver/src/roboscan_nsl3130/nsl_lib/lib/linux-7.5/shared
//       -o change_camera_ip change_camera_ip.cpp -lnanolib
//
// Usage:
//   ./change_camera_ip <new_cam_ip> <new_netmask> <new_gateway> [serial_or_empty]
// Example (auto-detect any connected Vendor USB camera):
//   ./change_camera_ip 192.168.2.201 255.255.255.0 192.168.2.1
// Example (specific camera by serial):
//   ./change_camera_ip 192.168.2.201 255.255.255.0 192.168.2.1 N008B060D

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include "nanolib.h"

using namespace NslOption;

// The SDK fetches calibration asynchronously during open. Commands sent before
// that reply is drained can consume its response (33) as their acknowledgement.
// Tests shorten these compile-time waits while retaining the same ordering.
#ifndef NSL_CAMERA_SETTLE_MS
#define NSL_CAMERA_SETTLE_MS 1200
#endif
#ifndef NSL_CAMERA_RECONNECT_WINDOW_MS
#define NSL_CAMERA_RECONNECT_WINDOW_MS 30000
#endif
#ifndef NSL_CAMERA_RECONNECT_RETRY_MS
#define NSL_CAMERA_RECONNECT_RETRY_MS 500
#endif
#ifndef NSL_CAMERA_RECONNECT_ATTEMPTS
#define NSL_CAMERA_RECONNECT_ATTEMPTS 60
#endif

static void settle_open()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(NSL_CAMERA_SETTLE_MS));
}

static bool is_ipv4(const char *value)
{
    in_addr address{};
    return inet_pton(AF_INET, value, &address) == 1;
}

// The camera automatically reboots after an address change. Ping can recover
// before its SDK control port. Allow startup time without a second address write,
// then verify the device and network settings before requesting flash save.
static bool save_at_new_address(const char *ip, const char *mask, const char *gateway,
                                const NslConfig &original)
{
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(NSL_CAMERA_RECONNECT_WINDOW_MS);
    for (int attempt = 1; attempt <= NSL_CAMERA_RECONNECT_ATTEMPTS &&
         std::chrono::steady_clock::now() < deadline; ++attempt) {
        if (attempt > 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(NSL_CAMERA_RECONNECT_RETRY_MS));
            if (std::chrono::steady_clock::now() >= deadline) break;
        }
        NslConfig config{};
        config.chipID = config.waferID = -1;
        config.lensType = LENS_TYPE::LENS_SF;
        std::printf("[INFO] Reconnecting at %s to verify and save (attempt %d, budget %d ms)...\n",
                    ip, attempt, NSL_CAMERA_RECONNECT_WINDOW_MS);
        const int handle = nsl_open(ip, &config, FUNCTION_OPTIONS::FUNC_ON);
        if (handle < 0) continue;
        settle_open();

        char actual_ip[64] = {}, actual_mask[64] = {}, actual_gateway[64] = {};
        const auto read_ret = nsl_getIpAddress(handle, actual_ip, actual_mask, actual_gateway);
        const bool identity_matches = original.chipID < 0 || original.waferID < 0 ||
            (config.chipID == original.chipID && config.waferID == original.waferID);
        if (read_ret != NSL_ERROR_TYPE::NSL_SUCCESS || !identity_matches ||
            std::strcmp(actual_ip, ip) != 0 || std::strcmp(actual_mask, mask) != 0 ||
            std::strcmp(actual_gateway, gateway) != 0) {
            std::printf("[ERROR] Reconnected device identity or IP/netmask/gateway did not verify; not saving.\n");
            nsl_closeHandle(handle);
            return false;
        }
        const auto save_ret = nsl_saveConfiguration(handle);
        nsl_closeHandle(handle);
        if (save_ret == NSL_ERROR_TYPE::NSL_SUCCESS) {
            std::printf("[OK] New address and device verified; configuration saved to flash.\n");
            return true;
        }
        std::printf("[WARN] Save at new address returned %s (%d).\n", toString(save_ret), (int)save_ret);
    }
    return false;
}

int main(int argc, char *argv[])
{
    if (argc < 4 || argc > 5) {
        printf("Usage: %s <new_cam_ip> <new_netmask> <new_gateway> [serial_or_empty]\n", argv[0]);
        printf("  new_cam_ip   : e.g. 192.168.2.201\n");
        printf("  new_netmask  : e.g. 255.255.255.0\n");
        printf("  new_gateway  : e.g. 192.168.2.1\n");
        printf("  serial       : 9-char serial (optional, e.g. N008B060D). Default: auto-detect\n");
        return 1;
    }

    const char *new_ip  = argv[1];
    const char *netmask = argv[2];
    const char *gateway = argv[3];
    const char *usb_id  = (argc >= 5) ? argv[4] : "";  // "" = auto-detect first Vendor USB camera

    if (!is_ipv4(new_ip) || !is_ipv4(netmask) || !is_ipv4(gateway)) {
        std::printf("[ERROR] Camera IP, netmask and gateway must be IPv4 addresses.\n");
        return 1;
    }
    const bool ethernet = is_ipv4(usb_id);

    NslConfig config;
    memset(&config, 0, sizeof(config));
    config.chipID = config.waferID = -1;
    config.lensType = LENS_TYPE::LENS_SF;

    printf("[INFO] Opening camera via %s (id='%s')...\n", ethernet ? "Ethernet" : "USB", usb_id);
    int handle = nsl_open(usb_id, &config, FUNCTION_OPTIONS::FUNC_ON);
    if (handle < 0) {
        printf("[ERROR] nsl_open failed (code %d).\n", handle);
        if (ethernet) printf("  - Check the camera address, Ethernet link and exclusive SDK access.\n");
        else {
            printf("  - Is USB cable connected? (lsusb should show 1fc9:0099)\n");
            printf("  - Is the USB udev rule installed?\n");
        }
        return 1;
    }
    printf("[OK] Connected (handle=%d).\n", handle);
    settle_open();

    char r_ip[64] = {}, r_mask[64] = {}, r_gw[64] = {};
    const auto read_ret = nsl_getIpAddress(handle, r_ip, r_mask, r_gw);
    if (read_ret == NSL_ERROR_TYPE::NSL_SUCCESS) {
        printf("[INFO] Camera current IP : %s  mask=%s  gw=%s\n", r_ip, r_mask, r_gw);
    }

    const bool already_configured = read_ret == NSL_ERROR_TYPE::NSL_SUCCESS &&
        std::strcmp(r_ip, new_ip) == 0 && std::strcmp(r_mask, netmask) == 0 &&
        std::strcmp(r_gw, gateway) == 0;
    NSL_ERROR_TYPE set_ret = NSL_ERROR_TYPE::NSL_SUCCESS;
    if (already_configured) {
        printf("[INFO] Requested IP/netmask/gateway already match; saving without another camera reboot.\n");
    } else {
        printf("[INFO] Setting new IP: %s  mask=%s  gw=%s\n", new_ip, netmask, gateway);
        set_ret = nsl_setIpAddress(handle, new_ip, netmask, gateway);
        if (set_ret != NSL_ERROR_TYPE::NSL_SUCCESS) {
            printf("[WARN] nsl_setIpAddress returned %s (%d). SDK did not acknowledge the write; attempting flash save.\n",
                   toString(set_ret), (int)set_ret);
        }
    }

    NSL_ERROR_TYPE save_ret = nsl_saveConfiguration(handle);
    if (save_ret != NSL_ERROR_TYPE::NSL_SUCCESS) {
        printf("[WARN] nsl_saveConfiguration on original handle returned %s (%d).\n",
               toString(save_ret), (int)save_ret);
        nsl_closeHandle(handle);
        if (ethernet && save_at_new_address(new_ip, netmask, gateway, config)) {
            printf("\n[DONE] Camera IP changed and saved -> %s.\n", new_ip);
            return 0;
        }
        printf("[ERROR] Flash save was not confirmed. New IP may not survive power cycle.\n");
        return 3;
    }

    printf("[OK] Configuration saved to flash.\n");

    nsl_closeHandle(handle);
    printf("\n[DONE] Camera IP configuration saved -> %s.\n", new_ip);
    return set_ret == NSL_ERROR_TYPE::NSL_SUCCESS ? 0 : 2;
}
