#pragma once

#include <vector>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include "gvsp_config.hpp"

enum PacketFormat {
    PKT_WITH_ERROR = 0,
    PKT_FORMAT_LEADER = 1,
    PKT_FORMAT_TRAILER = 2,
    PKT_FORMAT_PAYLOAD = 3
};

struct PacketData {
    uint16_t status;
    uint16_t block_id;
    uint8_t format;
    uint32_t packet_id;
    std::vector<uint8_t> payload;

    PacketData(uint16_t s, uint16_t b, uint8_t f, uint32_t p, const uint8_t* data, size_t len);
};

extern uint64_t g_start_timestamp;
extern uint16_t g_current_block_id;

void showImage(const std::vector<uint8_t>& payload, cv::VideoWriter& video, uint64_t timestamp, bool is_rgb);
void closeVideo(cv::VideoWriter& video, const std::string& filename);