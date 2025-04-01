#include "gvsp/parse_gvsp.hpp"
#include <iostream>
#include <algorithm>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <immintrin.h>  // For AVX2 SIMD

PacketData::PacketData(uint16_t s, uint16_t b, uint8_t f, uint32_t p, const uint8_t* data, size_t len)
    : status(s), block_id(b), format(f), packet_id(p), payload(data, data + len) {}

uint64_t g_start_timestamp = 0;
uint16_t g_current_block_id = 0;

void showImage(const std::vector<uint8_t>& payload, cv::VideoWriter& video, uint64_t timestamp) {
    if (payload.size() != gvsp_config::IMAGE_WIDTH * gvsp_config::IMAGE_HEIGHT * gvsp_config::IMAGE_CHANNELS) {
        std::cerr << "Payload size mismatch: " << payload.size() << std::endl;
        return;
    }

    cv::Mat image(gvsp_config::IMAGE_HEIGHT, gvsp_config::IMAGE_WIDTH, CV_8UC3, (void*)payload.data());

    // Overlay timestamp
    std::ostringstream ts_stream;
    ts_stream << std::fixed << std::setprecision(3) << (timestamp / 1000.0);
    std::string timestamp_str = ts_stream.str();
    int font = cv::FONT_HERSHEY_SIMPLEX;
    double font_scale = 0.8;
    int thickness = 2;
    cv::Scalar color(255, 255, 255);
    cv::Size text_size = cv::getTextSize(timestamp_str, font, font_scale, thickness, nullptr);
    int x = gvsp_config::IMAGE_WIDTH - text_size.width - 10;
    int y = text_size.height + 10;
    cv::putText(image, timestamp_str, cv::Point(x, y), font, font_scale, color, thickness);

    // Write directly to video
    if (video.isOpened()) {
        video.write(image);
    }

    cv::namedWindow("Image", cv::WINDOW_NORMAL);
    cv::imshow("Image", image);
}

void closeVideo(cv::VideoWriter& video, const std::string& filename) {
    if (video.isOpened()) {
        std::filesystem::create_directories(gvsp_config::VIDEO_OUTPUT_DIR);
        std::string full_path = std::string(gvsp_config::VIDEO_OUTPUT_DIR) + filename;
        std::cout << "Closing video: " << full_path << std::endl;
        video.release();
    }
}

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(gvsp_config::UDP_PORT);
    inet_pton(AF_INET, gvsp_config::UDP_IP, &addr.sin_addr);

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Bind failed: " << strerror(errno) << std::endl;
        close(sock);
        return 1;
    }

    std::vector<PacketData> image_data;
    image_data.reserve(1000);  // Pre-allocate for ~1000 packets
    std::vector<uint8_t> payload_image;
    payload_image.reserve(gvsp_config::IMAGE_WIDTH * gvsp_config::IMAGE_HEIGHT * gvsp_config::IMAGE_CHANNELS);

    uint16_t image_id = 0;
    bool img_start = false;
    uint64_t end_timestamp = 0;
    auto segment_start_time = std::chrono::steady_clock::now();
    cv::VideoWriter video;

    uint8_t buffer[gvsp_config::BUFFER_SIZE];

    while (true) {
        ssize_t len = recvfrom(sock, buffer, gvsp_config::BUFFER_SIZE, 0, nullptr, nullptr);
        if (len < 8) continue;

        uint16_t status = (buffer[0] << 8) | buffer[1];
        uint16_t block_id = (buffer[2] << 8) | buffer[3];
        uint8_t format = buffer[4];
        uint32_t packet_id = (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];

        if (format == PKT_FORMAT_LEADER) {
            img_start = true;
            image_id = block_id;
            g_current_block_id = block_id;
            image_data.clear();
            g_start_timestamp = ((uint64_t)buffer[12] << 56) | ((uint64_t)buffer[13] << 48) |
                                ((uint64_t)buffer[14] << 40) | ((uint64_t)buffer[15] << 32) |
                                ((uint64_t)buffer[16] << 24) | ((uint64_t)buffer[17] << 16) |
                                ((uint64_t)buffer[18] << 8)  | (uint64_t)buffer[19];
            continue;
        } else if (format == PKT_WITH_ERROR) {
            std::cout << "Packet error" << std::endl;
            continue;
        } else if (format == PKT_FORMAT_TRAILER && block_id == image_id) {
            if (img_start) {
                std::sort(image_data.begin(), image_data.end(),
                          [](const PacketData& a, const PacketData& b) {
                              return a.packet_id < b.packet_id;
                          });

                payload_image.clear();
                // SIMD-optimized payload assembly (AVX2)
                for (const auto& pkt : image_data) {
                    size_t offset = payload_image.size();
                    payload_image.resize(offset + pkt.payload.size());
                    size_t i = 0;
                    for (; i + 32 <= pkt.payload.size(); i += 32) {
                        _mm256_storeu_si256((__m256i*)(payload_image.data() + offset + i),
                                            _mm256_loadu_si256((__m256i*)(pkt.payload.data() + i)));
                    }
                    // Handle remaining bytes
                    std::copy(pkt.payload.begin() + i, pkt.payload.end(), payload_image.begin() + offset + i);
                }

                if (payload_image.size() == gvsp_config::IMAGE_WIDTH * gvsp_config::IMAGE_HEIGHT * gvsp_config::IMAGE_CHANNELS) {
                    end_timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // Open video writer if not already open
                    if (!video.isOpened()) {
                        std::ostringstream filename;
                        filename << std::setfill('0') << std::setw(3) << g_current_block_id << "_"
                                 << g_start_timestamp << "_" << end_timestamp << ".avi";
                        std::string full_path = std::string(gvsp_config::VIDEO_OUTPUT_DIR) + filename.str();
                        std::filesystem::create_directories(gvsp_config::VIDEO_OUTPUT_DIR);
                        video.open(full_path, cv::VideoWriter::fourcc('X', 'V', 'I', 'D'),
                                   10.0, cv::Size(gvsp_config::IMAGE_WIDTH, gvsp_config::IMAGE_HEIGHT));
                        if (!video.isOpened()) {
                            std::cerr << "Failed to open video writer: " << full_path << std::endl;
                        }
                    }

                    showImage(payload_image, video, g_start_timestamp);

                    auto now = std::chrono::steady_clock::now();
                    double elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - segment_start_time).count();
                    if (elapsed_sec >= gvsp_config::VIDEO_DURATION_SEC) {
                        std::ostringstream filename;
                        filename << std::setfill('0') << std::setw(3) << g_current_block_id << "_"
                                 << g_start_timestamp << "_" << end_timestamp << ".avi";
                        closeVideo(video, filename.str());
                        segment_start_time = now;
                    }
                } else {
                    std::cerr << "Image size mismatch: " << payload_image.size() << std::endl;
                }
            }
            img_start = false;
            image_data.clear();

            if (cv::waitKey(1) == 'q') {
                std::ostringstream filename;
                filename << std::setfill('0') << std::setw(3) << g_current_block_id << "_"
                         << g_start_timestamp << "_"
                         << std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count()
                         << ".avi";
                closeVideo(video, filename.str());
                break;
            }
        } else if (img_start && block_id == image_id) {
            image_data.emplace_back(status, block_id, format, packet_id, buffer + 8, len - 8);
        }
    }

    close(sock);
    return 0;
}