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

void showImage(const std::vector<uint8_t>& payload, cv::VideoWriter& video, uint64_t timestamp, bool is_rgb) {
    size_t expected_size = gvsp_config::IMAGE_WIDTH * gvsp_config::IMAGE_HEIGHT * (is_rgb ? 3 : 1);
    if (payload.size() != expected_size) {
        std::cerr << "Payload size mismatch: " << payload.size() << " vs expected " << expected_size 
                  << " (RGB: " << (is_rgb ? "yes" : "no") << ")" << std::endl;
        return;
    }

    cv::Mat image;
    if (is_rgb) {
        image = cv::Mat(gvsp_config::IMAGE_HEIGHT, gvsp_config::IMAGE_WIDTH, CV_8UC3, (void*)payload.data());
    } else {
        cv::Mat gray_image(gvsp_config::IMAGE_HEIGHT, gvsp_config::IMAGE_WIDTH, CV_8UC1, (void*)payload.data());
        cv::cvtColor(gray_image, image, cv::COLOR_GRAY2BGR);
    }

    // Prepare information structure
    std::string pixel_type = is_rgb ? "RGB8" : "Mono";
    std::ostringstream payload_stream;
    payload_stream << payload.size();
    std::string payload_size = payload_stream.str();
    std::ostringstream ts_stream;
    ts_stream << std::fixed << std::setprecision(3) << (timestamp / 1000.0);  // Camera timestamp in seconds
    std::string timestamp_str = ts_stream.str();
    std::ostringstream posix_stream;
    posix_stream << std::fixed << std::setprecision(3) << 
        (std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() / 1000000.0);  // POSIX time in seconds
    std::string posix_time = posix_stream.str();

    // Define text properties
    double font_scale = 0.6;  // Smaller font for multiple lines
    int thickness = 1;
    int baseline = 0;
    int line_spacing = 20;  // Space between lines
    int right_margin = 20;  // Margin from right edge

    // List of info to display
    std::vector<std::string> info_lines = {"Pixel Type: " + pixel_type,
        "Payload Size: " + payload_size,
        "Timestamp: " + timestamp_str,
        "POSIX Time: " + posix_time
    };

    // Calculate maximum text width for background rectangle
    int max_width = 0;
    for (const auto& line : info_lines) {
        cv::Size text_size = cv::getTextSize(line, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline);
        max_width = std::max(max_width, text_size.width);
    }

    // Position parameters
    int x_pos = gvsp_config::IMAGE_WIDTH - max_width - right_margin;
    int y_start = 30;  // Starting y-position from top
    x_pos = std::max(0, x_pos);  // Prevent negative position

    // Draw background rectangle
    int rect_height = line_spacing * info_lines.size() + 10;
    cv::rectangle(image,
                  cv::Point(x_pos - 5, y_start - 15),
                  cv::Point(x_pos + max_width + 5, y_start + rect_height - 5),
                  cv::Scalar(0, 0, 0),  // Black background
                  cv::FILLED);

    // Draw each line of text
    for (size_t i = 0; i < info_lines.size(); ++i) {
        int y_pos = y_start + (i * line_spacing);
        cv::putText(image, info_lines[i],
                    cv::Point(x_pos, y_pos),
                    cv::FONT_HERSHEY_SIMPLEX, font_scale,
                    cv::Scalar(255, 255, 255), thickness);
    }

    if (video.isOpened()) {
        video.write(image);
    } else {
        std::cerr << "Video writer not opened, skipping write" << std::endl;
    }

    cv::namedWindow("Image", cv::WINDOW_NORMAL);
    cv::imshow("Image", image);
}

void closeVideo(cv::VideoWriter& video, const std::string& filename) {
    if (video.isOpened()) {
        std::string full_path = std::string(gvsp_config::VIDEO_OUTPUT_DIR) + filename;
        try {
            std::filesystem::create_directories(gvsp_config::VIDEO_OUTPUT_DIR);
            std::cout << "Closing video: " << full_path << std::endl;
            video.release();
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Failed to create directory: " << e.what() << std::endl;
        }
    }
}

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed: " << strerror(errno) << " (errno: " << errno << ")" << std::endl;
        return 1;
    }
    std::cout << "Socket created successfully" << std::endl;

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
    std::cout << "Bound to " << gvsp_config::UDP_IP << ":" << gvsp_config::UDP_PORT << std::endl;

    std::vector<PacketData> image_data;
    image_data.reserve(5000);
    if (image_data.capacity() < 5000) {
        std::cerr << "Failed to reserve memory for image_data" << std::endl;
    }
    std::vector<uint8_t> payload_image;
    payload_image.reserve(gvsp_config::IMAGE_WIDTH * gvsp_config::IMAGE_HEIGHT * 3);

    uint16_t image_id = 0;
    bool img_start = false;
    uint64_t end_timestamp = 0;
    auto segment_start_time = std::chrono::steady_clock::now();
    cv::VideoWriter video;
    bool is_rgb = false;

    uint8_t buffer[gvsp_config::BUFFER_SIZE];

    while (true) {
        ssize_t len = recvfrom(sock, buffer, gvsp_config::BUFFER_SIZE, 0, nullptr, nullptr);
        std::cout << "Received packet size: " << len << " bytes" << std::endl;  // Added logging
        if (len < 8) {
            std::cout << "Received packet too small: " << len << " bytes, skipping" << std::endl;
            continue;
        }

        uint16_t status = (buffer[0] << 8) | buffer[1];
        uint16_t block_id = (buffer[2] << 8) | buffer[3];
        uint8_t format = buffer[4];
        uint32_t packet_id = (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];

        std::cout << "Packet - Status: " << status << ", Block ID: " << block_id 
                  << ", Format: " << (int)format << ", Packet ID: " << packet_id << std::endl;

        if (format == PKT_FORMAT_LEADER) {
            std::cout << "Leader packet received" << std::endl;
            img_start = true;
            image_id = block_id;
            g_current_block_id = block_id;
            image_data.clear();
            if (len >= 20) {
                g_start_timestamp = ((uint64_t)buffer[12] << 56) | ((uint64_t)buffer[13] << 48) |
                                    ((uint64_t)buffer[14] << 40) | ((uint64_t)buffer[15] << 32) |
                                    ((uint64_t)buffer[16] << 24) | ((uint64_t)buffer[17] << 16) |
                                    ((uint64_t)buffer[18] << 8)  | (uint64_t)buffer[19];
            } else {
                std::cerr << "Leader packet too short for timestamp" << std::endl;
            }
            continue;
        } else if (format == PKT_WITH_ERROR) {
            std::cout << "Packet error" << std::endl;
            continue;
        } else if (format == PKT_FORMAT_TRAILER && block_id == image_id) {
            std::cout << "Trailer packet received" << std::endl;
            if (img_start) {
                std::sort(image_data.begin(), image_data.end(),
                          [](const PacketData& a, const PacketData& b) {
                              return a.packet_id < b.packet_id;
                          });

                payload_image.clear();
                for (const auto& pkt : image_data) {
                    size_t offset = payload_image.size();
                    if (offset + pkt.payload.size() > payload_image.capacity()) {
                        payload_image.reserve(offset + pkt.payload.size());
                    }
                    payload_image.resize(offset + pkt.payload.size());
                    size_t i = 0;
                    for (; i + 32 <= pkt.payload.size(); i += 32) {
                        _mm256_storeu_si256((__m256i*)(payload_image.data() + offset + i),
                                            _mm256_loadu_si256((__m256i*)(pkt.payload.data() + i)));
                    }
                    std::copy(pkt.payload.begin() + i, pkt.payload.end(), payload_image.begin() + offset + i);

                    size_t pixel_start = offset / (is_rgb ? 3 : 1);
                    size_t pixel_end = (offset + pkt.payload.size()) / (is_rgb ? 3 : 1);
                    std::cout << "Packet ID " << pkt.packet_id << " maps to pixels " << pixel_start 
                              << " to " << pixel_end << std::endl;
                }

                size_t grayscale_size = gvsp_config::IMAGE_WIDTH * gvsp_config::IMAGE_HEIGHT;
                size_t rgb_size = grayscale_size * 3;
                if (payload_image.size() == rgb_size) {
                    is_rgb = true;
                    std::cout << "Detected RGB format (size: " << payload_image.size() << ")" << std::endl;
                } else if (payload_image.size() == grayscale_size) {
                    is_rgb = false;
                    std::cout << "Detected Grayscale format (size: " << payload_image.size() << ")" << std::endl;
                } else {
                    std::cerr << "Unexpected payload size: " << payload_image.size() << std::endl;
                    continue;
                }

                end_timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                if (!video.isOpened()) {
                    std::ostringstream filename;
                    filename << std::setfill('0') << std::setw(3) << g_current_block_id << "_"
                             << g_start_timestamp << "_" << end_timestamp << ".avi";
                    std::string full_path = std::string(gvsp_config::VIDEO_OUTPUT_DIR) + filename.str();
                    std::filesystem::create_directories(gvsp_config::VIDEO_OUTPUT_DIR);
                    video.open(full_path, cv::VideoWriter::fourcc('X', 'V', 'I', 'D'),
                               10.0, cv::Size(gvsp_config::IMAGE_WIDTH, gvsp_config::IMAGE_HEIGHT), true);
                    if (!video.isOpened()) {
                        std::cerr << "Failed to open video writer: " << full_path << std::endl;
                    }
                }

                showImage(payload_image, video, g_start_timestamp, is_rgb);

                auto now = std::chrono::steady_clock::now();
                double elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - segment_start_time).count();
                if (elapsed_sec >= gvsp_config::VIDEO_DURATION_SEC) {
                    std::ostringstream filename;
                    filename << std::setfill('0') << std::setw(3) << g_current_block_id << "_"
                             << g_start_timestamp << "_" << end_timestamp << ".avi";
                    closeVideo(video, filename.str());
                    segment_start_time = now;
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
            std::cout << "Payload packet received, size: " << (len - 8) << std::endl;
            image_data.emplace_back(status, block_id, format, packet_id, buffer + 8, len - 8);
        }
    }

    close(sock);
    return 0;
}