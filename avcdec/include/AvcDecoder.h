#pragma once

#include <vector>
#include <queue>
#include <iostream>
#include <cstring>

// Forward Declarations
extern "C" {
    typedef struct decodedpic_t DecodedPicList;  
    typedef struct storable_picture StorablePicture;  
}

// Data Types 
typedef unsigned char   Byte;
typedef unsigned char   PIXEL;
typedef unsigned char   Bool;
typedef unsigned char   UInt_8;
typedef unsigned short  UInt16;
typedef unsigned long   UInt32;
typedef char            SInt_8;
typedef short           SInt16;
typedef long            SInt32;

// Decoder configuration parameters structure
typedef struct {
    UInt32  bs_buf_size;
    UInt16  disp_buf_num;
    UInt16  disp_format;
    UInt16  disp_max_width;
    UInt16  disp_max_height;
    UInt16  target_profile;
    UInt16  target_level;
} DECPARAM_AVC;

// Timing information structure
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

// Picture metadata structure
typedef struct {
    TIMEINFO_AVC  pts;
    UInt16        pic_width;
    UInt16        pic_height;
    UInt16        pic_type;
    UInt16        bit_depth;
} PICMETAINFO_AVC;

// Avcdec Class 
class Avcdec
{
public:
    // Constructor
    // Allocates stream buffer, picture buffer and initializes decoder state
    Avcdec(DECPARAM_AVC *INPUT_PARAM);
    
    // Destructor
    // Releases all allocated resources
    ~Avcdec();

    // Start decoding
    // Initializes JM decoder and prepares for data input
    void vdec_start(UInt16 PLAY_MODE, UInt16 POST_PROCESS);
    
    // Stop decoding
    // Terminates decoder engine
    int vdec_stop();
    
    // Set post-processing filter
    // Enables/disables deblocking filter
    void vdec_postprocess(UInt16 TYPE);
    
    // Feed H.264 bitstream
    // Accumulates data in stream buffer
    unsigned int vdec_put_bs(
        Byte* PAYLOAD,
        UInt32 LENGTH,
        UInt16 END_OF_AU,
        UInt32 PTS,
        UInt16 ERR_FLAG,
        UInt32 ERR_SN_SKIP
    );
    
    // Get decoded picture
    // Returns YUV420 data when available
    Byte* vdec_get_picture(PICMETAINFO_AVC* PIC_METAINFO);
    
    // Get decoder status
    // Returns current state flags
    unsigned int vdec_get_status(
        UInt16* DEC_STATUS,
        UInt16* DISP_STATUS,
        UInt16* ERR_STATUS
    );
    
    // Get picture ready handle
    // Returns event handle for notification
    void* vdec_get_DecodedHandle();
    
    // Release picture buffer
    // Marks buffer as available for reuse
    void vdec_release_pic_buffer(Byte* PIC_ADDR);
    
    // Convert YUV420 to RGB24
    // Color space conversion
    int vdec_YUV420toRGB24(
        int Mode,
        unsigned char* iBGR,
        unsigned char* iYUV,
        int width,
        int height
    );
    
    // Fast YUV420 to RGB24 conversion
    // Optimized version with separate planes
    void vdec_YUV420toRGB24_2(
        unsigned char* y,
        unsigned char* u,
        unsigned char* v,
        unsigned char* rgb,
        int width,
        int height
    );
    
    // YUV420 to BGR24 for DirectDraw
    // BGR byte order for DirectDraw surface
    int YUV420toRGB24_DX(
        int Mode,
        unsigned char* iBGR,
        unsigned char* iYUV,
        int width,
        int height
    );

private:
    // Configuration
    DECPARAM_AVC m_config;
    
    // Stream buffer (memory-based input)
    Byte* m_streamBuffer;  // pointer to allocated memory
    UInt32 m_streamCapacity; //Max data can be stored
    UInt32 m_streamSize; //How much data you have(from 0 to m_streamCapacity)

    // PICTURE BUFFERS (OUTPUT)
    struct PictureBuffer {
        Byte* data;                 // YUV420 data
        int width;
        int height;
        bool locked;                // In use by application
    };
    
    PictureBuffer* m_pictureBuffers;  // Array of buffers
    UInt16 m_bufferCount;             // Count: disp_buf_num
    int m_currentBufferIndex;         // Current output buffer
    
    // Decoder state
    bool m_started;
    bool m_finished;
    bool m_postprocess_enabled;
    
    // Output frame storage
    struct Frame {
        Byte* data; 
        std::vector<uint8_t> yuv;
        int width;
        int height;
        PICMETAINFO_AVC metadata;
    };
    
    // Output frame queue
    std::queue<PICMETAINFO_AVC> m_frameInfoQueue;
    std::queue<Frame> m_frameQueue;  // Queue COMPLETE frames with data pointers
    
    // Private methods
    void InitJMDecoder();
    void DecodeBuffer();
    void CaptureDecodedFrame();
    void CaptureSinglePicture(DecodedPicList *pPic);
    bool CheckBufferSpace(UInt32 needed_bytes);
    PictureBuffer* GetAvailableBuffer();  
    void HandleEndOfAU();
};