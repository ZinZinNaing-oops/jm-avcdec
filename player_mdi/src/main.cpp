#include "AvcDecoder.h"
#include <fstream>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <h264_file>" << std::endl;
        return -1;
    }

    const char* input_file = argv[1];

    // ========== CREATE DECODER PARAMETERS ==========
    DECPARAM_AVC param = {};
    
    // High Profile (11,718,750 bytes as per specification)
    param.bs_buf_size = 11718750;       // High Profile maximum
    param.disp_buf_num = 16;
    param.disp_format = 0;              // YUV420
    param.disp_max_width = 1920;
    param.disp_max_height = 1080;
    param.target_profile = 100;         // High Profile
    param.target_level = 42;            // Level 4.2

    std::cout << "========================================" << std::endl;
    std::cout << "H.264 Decoder - JM19.0 Integration" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // ========== CREATE DECODER ==========
    Avcdec decoder(&param);

    // ========== START DECODER ==========
    decoder.vdec_start(0, 0);

    // ========== OPEN FILE ==========
    std::cout << "Opening file: " << input_file << std::endl;
    
    std::ifstream file(input_file, std::ios::binary);
    if (!file.is_open())
    {
        std::cout << "ERROR: Cannot open file!" << std::endl;
        return -1;
    }

    // Get file size
    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::cout << "File size: " << file_size << " bytes" << std::endl;
    std::cout << std::endl;

    // ========== FEED H.264 DATA ==========
    std::cout << "========================================" << std::endl;
    std::cout << "Feeding bitstream data" << std::endl;
    std::cout << "========================================" << std::endl;

    std::vector<Byte> chunk(65536);
    uint32_t total_bytes = 0;
    uint32_t chunk_count = 0;

    while (true)
    {
        file.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        std::streamsize bytes_read = file.gcount();
        
        if (bytes_read <= 0)
            break;

        total_bytes += bytes_read;
        chunk_count++;

        bool is_eof = file.eof();

        std::cout << "Chunk " << chunk_count << ": " << bytes_read << " bytes";
        if (is_eof)
            std::cout << " (END_OF_AU)";
        std::cout << std::endl;

        unsigned int ret = decoder.vdec_put_bs(
            chunk.data(),
            bytes_read,
            is_eof ? 1 : 0,
            chunk_count,
            0,
            0
        );

        if (ret == (unsigned int)-1)
        {
            std::cout << "ERROR: vdec_put_bs failed!" << std::endl;
            break;
        }
    }

    file.close();

    // ========== WAIT FOR DECODING ==========
    std::cout << std::endl;
    std::cout << "Waiting for decoder to process..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // ========== GET PICTURES ==========
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Retrieving decoded pictures" << std::endl;
    std::cout << "========================================" << std::endl;

    int pic_count = 0;
    while (true)
    {
        PICMETAINFO_AVC pic_info = {};
        Byte* yuv = decoder.vdec_get_picture(&pic_info);

        if (!yuv)
        {
            std::cout << "No more pictures" << std::endl;
            break;
        }

        pic_count++;
        std::cout << "Picture #" << pic_count << ": " 
                  << pic_info.pic_width << "x" << pic_info.pic_height;
        
        switch (pic_info.pic_type)
        {
            case 0: std::cout << " (I)"; break;
            case 1: std::cout << " (P)"; break;
            case 2: std::cout << " (B)"; break;
            default: std::cout << " (?)"; break;
        }
        std::cout << std::endl;

        decoder.vdec_release_pic_buffer(yuv);
    }

    // ========== GET STATUS ==========
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Decoder status" << std::endl;
    std::cout << "========================================" << std::endl;
    
    UInt16 dec_status, disp_status, err_status;
    decoder.vdec_get_status(&dec_status, &disp_status, &err_status);

    // ========== STOP DECODER ==========
    std::cout << std::endl;
    std::cout << "Stopping decoder..." << std::endl;
    decoder.vdec_stop();

    // ========== SUMMARY ==========
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "DECODE SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "File size: " << total_bytes << " bytes" << std::endl;
    std::cout << "Chunks fed: " << chunk_count << std::endl;
    std::cout << "Pictures decoded: " << pic_count << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}