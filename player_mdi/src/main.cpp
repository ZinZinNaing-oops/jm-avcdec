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
    AvcDecoder decoder;

    if (!decoder.vdec_start(0, 0))
        return -1;

    std::ifstream file("/Users/zinnaing/projects/avc_project/errnal_002.264", std::ios::binary);
    
    if (!file) {
        fprintf(stderr, "Cannot open file:");
        return -1;
    }

    NALUParser parser;
    std::vector<uint8_t> chunk(4096);
    std::vector<uint8_t> nalu;
    uint32_t nalu_count = 0;
    
    printf("=== DECODER STARTED ===\n");
    printf("Processing file: ");
// Read entire file into memory
    while (file) {
        file.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        std::streamsize bytesRead = file.gcount();

        if (bytesRead <= 0)
            break;

        // Feed chunk to NALU parser
        parser.feed_data(chunk.data(), bytesRead);

        // Extract and decode complete NALUs
        while (parser.has_complete_nalu() && parser.get_next_nalu(nalu)) {
            nalu_count++;
            
            printf("NALU #%u: size=%zu bytes\n", nalu_count, nalu.size());
            
            // Send complete NALU to decoder
            // Use end_of_au=1 only for last NALU
            uint16_t end_of_au = 0;  // Or set to 1 if you know it's the last NALU
            
            decoder.vdec_put_bs(
                nalu.data(), 
                nalu.size(), 
                end_of_au,  // end_of_au
                nalu_count, // pts (use NALU count as frame counter)
                0,          // err_flag
                0           // err_sn_skip
            );

            // Get decoded pictures
            int w, h;
            while (uint8_t* frame = decoder.vdec_get_picture(&w, &h)) {
                printf("Frame decoded: %dx%d\n", w, h);
            }
        }
    }

    // Signal end of stream (optional)
    printf("=== DECODER STOPPING ===\n");
    decoder.vdec_put_bs(nullptr, 0, 1, 0, 0, 0);  // Flush remaining frames
    
    // Get remaining frames
    int w, h;
    while (uint8_t* frame = decoder.vdec_get_picture(&w, &h)) {
        printf("Final frame decoded: %dx%d\n", w, h);
    }

    decoder.vdec_stop();
    printf("=== DECODER STOPPED ===\n");
    
    return 0;
}