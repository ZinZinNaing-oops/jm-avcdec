#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <queue>

typedef unsigned char   UInt8;
typedef unsigned short  UInt16;
typedef unsigned long   UInt32;

typedef struct {
    UInt32  bs_buf_size;
    UInt16  disp_buf_num;
    UInt16  disp_format;
    UInt16  disp_max_width;
    UInt16  disp_max_height;
    UInt16  target_profile;
    UInt16  target_level;
} DECPARAM_AVC;

class AvcDecoder
{
    private:
        // Frame storage
        std::vector<uint8_t> m_buffer;
        
        // Stream buffer for JM
        uint8_t* m_streamBuffer;
        size_t   m_streamCapacity;
        size_t   m_streamSize;
        size_t   m_last_memory_pos;

        struct Frame
        {
            std::vector<uint8_t> yuv;
            int width;
            int height;
        };

        std::queue<Frame> m_frameQueue;
        bool m_started;
        bool m_finished = false;
        bool m_eos_signaled = false;

    public:
        AvcDecoder();
        ~AvcDecoder();

        bool vdec_start(UInt16 PLAY_MODE, UInt16 POST_PROCESS);
        void vdec_stop();

        unsigned int vdec_put_bs(
            uint8_t* payload,
            uint32_t length,
            uint16_t end_of_au,
            uint32_t pts,
            uint16_t err_flag,
            uint32_t err_sn_skip
        );
 
        uint8_t* vdec_get_picture(int* width, int* height);

    private:
        void captureDecodedFrame();
        void decodeAvailable();
};