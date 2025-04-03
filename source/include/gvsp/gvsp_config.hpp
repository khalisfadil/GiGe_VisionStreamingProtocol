#pragma once

namespace gvsp_config {
    constexpr const char* UDP_IP = "192.168.75.10";  // Updated to your PC’s new IP
    constexpr int UDP_PORT = 49152;                   // Keep this as your listening port
    constexpr int BUFFER_SIZE = 16384;
    constexpr int IMAGE_WIDTH = 1920;
    constexpr int IMAGE_HEIGHT = 1200;
    constexpr int IMAGE_CHANNELS = 1;
    constexpr double VIDEO_DURATION_SEC = 300.0;
    constexpr const char* VIDEO_OUTPUT_DIR = "/media/khalis/Extreme Pro/CamOutput/";
}