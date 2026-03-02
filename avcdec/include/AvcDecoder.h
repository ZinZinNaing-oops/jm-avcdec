#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <queue>

/*----------------------------------------
  整数データ型 
----------------------------------------*/
typedef unsigned char   Byte;
typedef unsigned char   PIXEL;
typedef unsigned char   Bool;
typedef unsigned char   UInt8;
typedef unsigned short  UInt16;
typedef unsigned long   UInt32;
//typedef char            Boolean;
typedef char            SInt_8;
typedef short           SInt16;
typedef long            SInt32;
typedef int             Int;

/*----------------------------------------
  ビットストリームバッファサイズなどavcdec_coreに指定するための構造体
----------------------------------------*/
typedef struct {
    UInt32  bs_buf_size;
    UInt16  disp_buf_num;
    UInt16  disp_format;       // 0: YUV420
    UInt16  disp_max_width;
    UInt16  disp_max_height;
    UInt16  target_profile;
    UInt16  target_level;
} DECPARAM_AVC;

/*----------------------------------------
  表示画像に付随する表示時刻情報
----------------------------------------*/
typedef struct {
    UInt32  input_pts;
    bool m_ts_success;
    double  time;
    bool m_timinginfo_success;
    UInt32  time_scale;
    UInt32  num_units_in_tick;
    UInt32  low_delay_hrd_flag;
    UInt32  BitRate;
    UInt32  cbr_flag;
    UInt32  cpb_removal_delay;
    UInt32  dpb_output_delay;
    bool is_first_au_of_buff_period;
    UInt32  initial_cpb_removal_delay;
    UInt32  initial_cpb_removal_delay_offset;
} TIMEINFO_AVC;

/*----------------------------------------
  表示画像に付随するメタ情報を出力するための構造体
----------------------------------------*/
typedef struct {
    TIMEINFO_AVC pts;
    UInt16       pic_width;
    UInt16       pic_height;
    UInt16       pic_type;
    UInt16       bit_depth;
} PICMETAINFO_AVC;

class AvcDecoder
{
    private:
        std::vector<uint8_t> m_buffer;
        std::vector<uint8_t> m_y;
        std::vector<uint8_t> m_u;
        std::vector<uint8_t> m_v; 
        
        uint8_t* m_streamBuffer;
        size_t   m_streamCapacity;
        size_t   m_streamSize;

        struct Frame
        {
            std::vector<uint8_t> yuv;
            int width;
            int height;
        };

        std::queue<Frame> m_frameQueue;
        bool m_started;
        bool m_finished = false;
        // NEW: NAL tracking
        std::vector<size_t> m_startCodePositions;

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
 
        void decodeAvailable();

        uint8_t* vdec_get_picture(int* width, int* height);

        bool decode_one_frame(
            uint8_t** y,
            uint8_t** u,
            uint8_t** v,
            int* width,
            int* height
        );

    private:
        void scanNewStartCodes(size_t oldSize);
        bool hasFullNAL();      
};

class NALUParser
{
    public:
        std::vector<uint8_t> buffer;
        size_t current_pos;

    public:
        NALUParser();

        // Feed data into the buffer
        void feed_data(const uint8_t* data, size_t length);
        // Find and extract next complete NALU
        bool get_next_nalu(std::vector<uint8_t>& nalu);
        // Check if we have a complete NALU
        bool has_complete_nalu();
        void NALUParserClear();
        size_t find_start_code(size_t from_pos);

};