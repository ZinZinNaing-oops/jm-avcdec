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

    std::ifstream file("/Users/zinnaing/projects/avc_project/AUD_MW_E.264", std::ios::binary);
    
    std::vector<uint8_t> chunk(4096);

    while (file)
    {
        file.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        std::streamsize bytesRead = file.gcount();

        if (bytesRead <= 0)
            break;

        decoder.vdec_put_bs(chunk.data(), bytesRead, 0, 0, 0, 0);

        int w, h;
        while (uint8_t* frame = decoder.vdec_get_picture(&w, &h))
        {
            std::cout << "Frame decoded: "
                      << w << "x" << h << std::endl;
        }
    }
    //decoder.vdec_put_bs(nullptr, 0, 1, 0, 0, 0);

    decoder.vdec_stop();
}
