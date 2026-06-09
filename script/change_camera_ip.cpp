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

#include <cstdio>
#include <cstring>
#include "nanolib.h"

using namespace NslOption;

int main(int argc, char *argv[])
{
    if (argc < 4) {
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

    NslConfig config;
    memset(&config, 0, sizeof(config));
    config.lensType = LENS_TYPE::LENS_SF;

    printf("[INFO] Opening camera via USB (id='%s')...\n", usb_id);
    int handle = nsl_open(usb_id, &config, FUNCTION_OPTIONS::FUNC_ON);
    if (handle < 0) {
        printf("[ERROR] nsl_open failed (code %d).\n", handle);
        printf("  - Is USB cable connected? (lsusb should show 1fc9:0099)\n");
        printf("  - Is udev rule installed? (sudo ./../../NSL3130_driver/src/roboscan_nsl3130/nsl_lib/script/install_libusb_linux.sh)\n");
        return 1;
    }
    printf("[OK] Connected (handle=%d).\n", handle);

    char r_ip[64] = {}, r_mask[64] = {}, r_gw[64] = {};
    if (nsl_getIpAddress(handle, r_ip, r_mask, r_gw) == NSL_ERROR_TYPE::NSL_SUCCESS) {
        printf("[INFO] Camera current IP : %s  mask=%s  gw=%s\n", r_ip, r_mask, r_gw);
    }

    printf("[INFO] Setting new IP: %s  mask=%s  gw=%s\n", new_ip, netmask, gateway);
    NSL_ERROR_TYPE set_ret = nsl_setIpAddress(handle, new_ip, netmask, gateway);
    if (set_ret != NSL_ERROR_TYPE::NSL_SUCCESS) {
        printf("[WARN] nsl_setIpAddress returned %s (%d). SDK did not acknowledge the write; still saving and verifying by ping.\n",
               toString(set_ret), (int)set_ret);
    }

    NSL_ERROR_TYPE save_ret = nsl_saveConfiguration(handle);
    if (save_ret != NSL_ERROR_TYPE::NSL_SUCCESS) {
        printf("[ERROR] nsl_saveConfiguration returned %s (%d). New IP may not survive power cycle.\n",
               toString(save_ret), (int)save_ret);
        nsl_closeHandle(handle);
        return 3;
    }

    printf("[OK] Configuration saved to flash.\n");

    nsl_closeHandle(handle);
    printf("\n[DONE] Camera IP changed -> %s. Power-cycle the camera to apply.\n", new_ip);
    return set_ret == NSL_ERROR_TYPE::NSL_SUCCESS ? 0 : 2;
}
