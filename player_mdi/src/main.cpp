#include <stdio.h>
#include "AvcDecoder.h"
#include <iostream>
#include <fstream>
#include <vector>
extern "C" {
#include "../../JM/jm_wrapper.h"
}

int main(int argc, char* argv[])
{
    // Create decoder instance
    AvcDecoder decoder;

    // Start decoder
    if (!decoder.vdec_start(0, 0))
    {
        fprintf(stderr, "Failed to start decoder\n");
        return -1;
    }

    // Open H.264 file
    std::ifstream file("/Users/zinnaing/projects/avc_project/AUD_MW_E.264", std::ios::binary);
    //std::ifstream file(input_file, std::ios::binary);
    if (!file)
    {
        fprintf(stderr, "Cannot open file:");
        return -1;
    }

    printf("=== DECODER STARTED ===\n");
    printf("Processing file: ");

    std::vector<uint8_t> chunk(4096);
    uint32_t nalu_count = 0;
    uint32_t frame_count = 0;

    // Read entire file in chunks and feed to decoder
    while (file)
    {
        file.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        std::streamsize bytesRead = file.gcount();

        if (bytesRead <= 0)
            break;

        // Feed chunk to decoder (no intermediate NALU parsing needed)
        // The decoder now handles NALU extraction internally
        decoder.vdec_put_bs(
            chunk.data(),
            static_cast<uint32_t>(bytesRead),
            0,          // end_of_au = 0 (not end of AU yet)
            nalu_count, // pts
            0,          // err_flag
            0           // err_sn_skip
        );

        // Get decoded frames
        int width, height;
        uint8_t* frame_data = nullptr;
        while ((frame_data = decoder.vdec_get_picture(&width, &height)) != nullptr)
        {
            frame_count++;
            printf("Frame #%u decoded: %dx%d\n", frame_count, width, height);
        }
    }

    file.close();

    // Signal end of stream with end_of_au = 1
    printf("\n=== SIGNALING END OF STREAM ===\n");
    decoder.vdec_put_bs(nullptr, 0, 1, nalu_count, 0, 0);

    // Get remaining frames
    int width, height;
    uint8_t* frame_data = nullptr;
    while ((frame_data = decoder.vdec_get_picture(&width, &height)) != nullptr)
    {
        frame_count++;
        printf("Final frame #%u decoded: %dx%d\n", frame_count, width, height);
    }

    // Stop decoder
    printf("\n=== DECODER STOPPING ===\n");
    decoder.vdec_stop();
    printf("=== DECODER STOPPED ===\n");
    printf("Total frames decoded: %u\n", frame_count);

    return 0;
}