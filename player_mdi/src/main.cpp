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

    // Create decoder configuration
    DECPARAM_AVC param = {};
    param.bs_buf_size = 11718750;      // 11.7 MB (High Profile)
    param.disp_buf_num = 16;
    param.disp_format = 0;             // YUV420
    param.disp_max_width = 1920;
    param.disp_max_height = 1080;
    param.target_profile = 100;        // High Profile
    param.target_level = 42;           // Level 4.2

    // Create decoder
    Avcdec decoder(&param);

    // Start decoder
    decoder.vdec_start(0, 0);

    // Open input file
    std::cout << "Opening: " << argv[1] << std::endl;
    std::ifstream file(argv[1], std::ios::binary);
    if (!file.is_open())
    {
        std::cout << "ERROR: Cannot open file" << std::endl;
        return -1;
    }

    // Read file size
    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::cout << "File size: " << file_size << " bytes" << std::endl << std::endl;

    // Feed data to decoder
    std::vector<Byte> chunk(65536);
    
    while (file)
    {
        file.read((char*)chunk.data(), chunk.size());
        std::streamsize bytes_read = file.gcount();
        
        if (bytes_read <= 0)
            break;

        // Feed to decoder
        unsigned int ret = decoder.vdec_put_bs(
            chunk.data(),
            bytes_read,
            file.eof() ? 1 : 0,  // END_OF_AU when EOF
            0,                    // PTS
            0,                    // ERR_FLAG
            0                     // ERR_SN_SKIP
        );

        if (ret == (unsigned int)-1)
        {
            std::cout << "Error feeding data" << std::endl;
            break;
        }
    }

    file.close();
    std::cout << std::endl;
    // Get decoded pictures
    std::cout << "Retrieved pictures:" << std::endl;
    int pic_count = 0;
    
    while (true)
    {
        PICMETAINFO_AVC pic_info = {};
        Byte* yuv = decoder.vdec_get_picture(&pic_info);
        
        if (!yuv)
            break;

        pic_count++;
        std::cout << "  Picture #" << pic_count << ": " 
                  << pic_info.pic_width << "x" << pic_info.pic_height << std::endl;
        
        decoder.vdec_release_pic_buffer(yuv);
    }

    std::cout << std::endl;

    // Stop decoder
    decoder.vdec_stop();

    std::cout << "========================================" << std::endl;
    std::cout << "Total pictures: " << pic_count << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}