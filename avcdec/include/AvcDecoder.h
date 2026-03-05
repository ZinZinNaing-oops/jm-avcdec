#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <iostream>
#include <cstring>
#include <memory>

//============================================================================
// Data Types (Specification 5.1.1)
//============================================================================

typedef unsigned char   Byte;
typedef unsigned char   PIXEL;
typedef unsigned char   Bool;
typedef unsigned char   UInt_8;
typedef unsigned short  UInt16;
typedef unsigned long   UInt32;
typedef char            SInt_8;
typedef short           SInt16;
typedef long            SInt32;

//============================================================================
// Data Structures (Specification 5.1.2)
//============================================================================

/**
 * DECPARAM_AVC - Decoder Parameters
 */
typedef struct {
    UInt32  bs_buf_size;            ///< Bitstream buffer size (bytes)
    UInt16  disp_buf_num;           ///< Display picture buffer count
    UInt16  disp_format;            ///< Display format (0=YUV4:2:0)
    UInt16  disp_max_width;         ///< Max horizontal pixels
    UInt16  disp_max_height;        ///< Max vertical lines
    UInt16  target_profile;         ///< Target H.264 profile
    UInt16  target_level;           ///< Target H.264 level
} DECPARAM_AVC;

/**
 * TIMEINFO_AVC - Timing Information
 */
typedef struct {
    UInt32  input_pts;
    int     m_ts_success;
    double  time;
    int     m_timinginfo_success;
    UInt32  time_scale;
    UInt32  num_units_in_tick;
    UInt32  low_delay_hrd_flag;
    UInt32  BitRate;
    UInt32  cbr_flag;
    UInt32  cpb_removal_delay;
    UInt32  dpb_output_delay;
    int     is_first_au_of_buff_period;
    UInt32  initial_cpb_removal_delay;
    UInt32  initial_cpb_removal_delay_offset;
} TIMEINFO_AVC;

/**
 * PICMETAINFO_AVC - Picture Metadata
 */
typedef struct {
    TIMEINFO_AVC  pts;
    UInt16        pic_width;
    UInt16        pic_height;
    UInt16        pic_type;
    UInt16        bit_depth;
} PICMETAINFO_AVC;

//============================================================================
// JM Integration Structures
//============================================================================

struct JMDecoderContext {
    void* p_Dec;                        // DecoderParams*
    void* p_Vid;                        // VideoParameters*
    void* pDecPicList;                  // DecodedPicList*
    int initialized;
};

//============================================================================
// Avcdec Class (Specification Section 5.2)
//============================================================================

class Avcdec
{
public:
    // Section 5.2.1 - Constructor
    explicit Avcdec(DECPARAM_AVC *INPUT_PARAM);
    
    // Section 5.2.2 - Destructor
    ~Avcdec();

    // Section 5.2.3 - Start decoding
    void vdec_start(UInt16 PLAY_MODE, UInt16 POST_PROCESS);
    
    // Section 5.2.4 - Stop decoding
    int vdec_stop();
    
    // Section 5.2.5 - Set post-processing
    void vdec_postprocess(UInt16 TYPE);
    
    // Section 5.2.6 - Feed bitstream
    unsigned int vdec_put_bs(
        Byte* PAYLOAD,
        UInt32 LENGTH,
        UInt16 END_OF_AU,
        UInt32 PTS,
        UInt16 ERR_FLAG,
        UInt32 ERR_SN_SKIP
    );
    
    // Section 5.2.7 - Get picture
    Byte* vdec_get_picture(PICMETAINFO_AVC* PIC_METAINFO);
    
    // Section 5.2.8 - Get status
    unsigned int vdec_get_status(
        UInt16* DEC_STATUS,
        UInt16* DISP_STATUS,
        UInt16* ERR_STATUS
    );
    
    // Section 5.2.9 - Get decoded handle
    void* vdec_get_DecodedHandle();
    
    // Section 5.2.10 - Release picture buffer
    void vdec_release_pic_buffer(Byte* PIC_ADDR);
    
    // Section 5.2.11 - Convert YUV420 to RGB24
    int vdec_YUV420toRGB24(
        int Mode,
        unsigned char* iBGR,
        unsigned char* iYUV,
        int width,
        int height
    );
    
    // Section 5.2.12 - Fast YUV420 to RGB24
    void vdec_YUV420toRGB24_2(
        unsigned char* y,
        unsigned char* u,
        unsigned char* v,
        unsigned char* rgb,
        int width,
        int height
    );
    
    // Section 5.2.13 - YUV420 to BGR24 for DirectDraw
    int YUV420toRGB24_DX(
        int Mode,
        unsigned char* iBGR,
        unsigned char* iYUV,
        int width,
        int height
    );

private:
    DECPARAM_AVC m_config;
    
    // Stream buffer
    Byte* m_streamBuffer;
    UInt32 m_streamCapacity;
    UInt32 m_streamSize;
    std::mutex m_streamMutex;
    
    // Decoder state
    bool m_started;
    bool m_finished;
    bool m_postprocess_enabled;
    
    // JM decoder context
    JMDecoderContext m_jmContext;
    
    // Frame storage
    struct Frame {
        std::vector<uint8_t> yuv;
        int width;
        int height;
        PICMETAINFO_AVC metadata;
    };
    
    std::queue<Frame> m_frameQueue;
    std::mutex m_frameMutex;
    std::vector<uint8_t> m_currentFrame;
    
    // Decode thread
    std::thread m_decodeThread;
    bool m_threadRunning;
    
    // Statistics
    int m_framesDecoded;
    int m_picturesOutput;
    
    // Private methods
    void InitJMDecoder();
    void DecodeThreadFunc();
    void ProcessDecodedFrames();
    bool CheckBufferSpace(UInt32 needed_bytes);
    void HandleEndOfAU();
    void Cleanup();
};