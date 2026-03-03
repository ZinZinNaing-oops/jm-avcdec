#include <stdio.h>
#include "AvcDecoder.h"
#include <iostream>
#include <fstream>
#include <vector>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <h264_file>\n", argv[0]);
        return -1;
    }

    const char* input_file = argv[1];

    AvcDecoder decoder;

    if (!decoder.vdec_start(0, 0))
    {
        fprintf(stderr, "Failed to start decoder\n");
        return -1;
    }

    std::ifstream file(input_file, std::ios::binary);
    if (!file)
    {
        fprintf(stderr, "Cannot open file: %s\n", input_file);
        return -1;
    }

    printf("=== FILE DECODING STARTED ===\n");
    printf("Input: %s\n\n", input_file);

    std::vector<uint8_t> chunk(65536);
    uint32_t frame_count = 0;
    size_t total_bytes_read = 0;

    // Feed file to decoder
    while (file)
    {
        file.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        std::streamsize bytesRead = file.gcount();

        if (bytesRead <= 0)
            break;

        total_bytes_read += bytesRead;

        int ret = decoder.vdec_put_bs(
            chunk.data(),
            static_cast<uint32_t>(bytesRead),
            0,
            0,
            0,
            0
        );

        if (ret < 0)
        {
            fprintf(stderr, "Error in vdec_put_bs\n");
            break;
        }

        // Try to get frames (won't get any until end)
        int width, height;
        uint8_t* frame_data = nullptr;
        while ((frame_data = decoder.vdec_get_picture(&width, &height)) != nullptr)
        {
            frame_count++;
            int size_kb = (width * height * 3 / 2) / 1024;
            printf("  ✓ Frame #%u: %dx%d (%d KB)\n", frame_count, width, height, size_kb);
        }
    }

    file.close();

    printf("\n=== SIGNALING END OF STREAM ===\n");
    decoder.vdec_put_bs(nullptr, 0, 1, 0, 0, 0);

    // Retrieve frames from EOS flush
    int width, height;
    uint8_t* frame_data = nullptr;
    while ((frame_data = decoder.vdec_get_picture(&width, &height)) != nullptr)
    {
        frame_count++;
        printf("  ✓ Frame #%u (EOS flush): %dx%d\n", frame_count, width, height);
    }

    // CRITICAL: Stop decoder to flush DPB
    decoder.vdec_stop();

    // CRITICAL: Get frames that were flushed during vdec_stop()
    while ((frame_data = decoder.vdec_get_picture(&width, &height)) != nullptr)
    {
        frame_count++;
        printf("  ✓ Frame #%u (DPB flushed): %dx%d\n", frame_count, width, height);
    }

    printf("\n========================================\n");
    printf("✓ DECODE COMPLETE\n");
    printf("  Frames decoded: %u\n", frame_count);
    printf("  Total bytes:    %zu bytes (%.2f MB)\n", total_bytes_read, total_bytes_read / (1024.0 * 1024.0));
    printf("========================================\n\n");

    return 0;
}