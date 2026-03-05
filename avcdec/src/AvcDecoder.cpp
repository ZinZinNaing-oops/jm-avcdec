#include "AvcDecoder.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

//============================================================================
// JM External Functions and Variables
//============================================================================

extern "C" {
    // ========== INCLUDE CORE HEADERS ONLY ==========
    #include "../../JM/ldecod/inc/global.h"
    #include "../../JM/ldecod/inc/annexb.h"
    
    // ========== DO NOT INCLUDE ldecod.h OR ldecod.c ==========
    
    // ========== DECLARE EXTERNAL VARIABLES ==========
    extern unsigned char* g_memory_buffer;
    extern int g_memory_size;
    extern int g_memory_pos;
    
    // ========== DECLARE EXTERNAL STRUCTURES ==========
    extern DecoderParams *p_Dec;
    extern DecodedPicList *pDecPicList;
    extern Bitstream *currStream;
    
    // ========== DECLARE EXTERNAL FUNCTIONS ==========
    // These are implemented in ldecod.c but we don't include it
    extern int OpenDecoder(InputParameters *p_Inp);
    extern int DecodeOneFrame(DecoderParams *pDecoder);
    extern void FinitDecoder(DecodedPicList **pDecPicList);
    extern int CloseDecoder(void);
    extern void Configure(InputParameters *p_Inp, int ac, char *av[]);
}

//============================================================================
// Constants (from ldecod.c)
//============================================================================

#define DEC_OPEN_NOERR  0
#define DEC_OPEN_ERRMASK 1
#define DEC_SUCCEED 0
#define DEC_EOS 1

//============================================================================
// Constructor (Section 5.2.1)
//============================================================================

Avcdec::Avcdec(DECPARAM_AVC *INPUT_PARAM)
    : m_streamBuffer(nullptr),
      m_streamCapacity(0),
      m_streamSize(0),
      m_started(false),
      m_finished(false),
      m_postprocess_enabled(false),
      m_threadRunning(false),
      m_framesDecoded(0),
      m_picturesOutput(0)
{
    std::cout << "======================================" << std::endl;
    std::cout << "Avcdec Constructor" << std::endl;
    std::cout << "======================================" << std::endl;
    
    // Initialize JM context
    m_jmContext.p_Dec = nullptr;
    m_jmContext.p_Vid = nullptr;
    m_jmContext.pDecPicList = nullptr;
    m_jmContext.initialized = 0;
    
    // Copy configuration
    m_config = *INPUT_PARAM;
    
    std::cout << "Config:" << std::endl;
    std::cout << "  BS Buffer Size: " << m_config.bs_buf_size << " bytes" 
              << " (" << m_config.bs_buf_size / (1024*1024) << " MB)" << std::endl;
    std::cout << "  Display Buffers: " << m_config.disp_buf_num << std::endl;
    std::cout << "  Format: " << (m_config.disp_format == 0 ? "YUV420" : "Unknown") << std::endl;
    std::cout << "  Max Resolution: " << m_config.disp_max_width << "x" 
              << m_config.disp_max_height << std::endl;
    std::cout << "  Profile: " << m_config.target_profile << std::endl;
    std::cout << "  Level: " << m_config.target_level << std::endl;
    
    // Allocate stream buffer
    m_streamCapacity = m_config.bs_buf_size;
    m_streamBuffer = new Byte[m_streamCapacity];
    memset(m_streamBuffer, 0, m_streamCapacity);
    m_streamSize = 0;
    
    std::cout << "Stream buffer allocated: " << m_streamCapacity << " bytes" << std::endl;
    std::cout << "Constructor complete" << std::endl << std::endl;
}

//============================================================================
// Destructor (Section 5.2.2)
//============================================================================

Avcdec::~Avcdec()
{
    std::cout << "Avcdec Destructor" << std::endl;
    
    if (m_started)
    {
        vdec_stop();
    }
    
    Cleanup();
    
    std::cout << "Destructor complete" << std::endl;
}

//============================================================================
// vdec_start() (Section 5.2.3)
//============================================================================

void Avcdec::vdec_start(UInt16 PLAY_MODE, UInt16 POST_PROCESS)
{
    std::cout << "vdec_start(PLAY_MODE=" << PLAY_MODE 
              << ", POST_PROCESS=" << POST_PROCESS << ")" << std::endl;
    
    m_started = true;
    m_finished = false;
    m_postprocess_enabled = (POST_PROCESS != 0);
    
    // Initialize JM decoder
    InitJMDecoder();
    
    // Initialize JM memory mode globals
    g_memory_buffer = m_streamBuffer;
    g_memory_size = m_streamSize;
    g_memory_pos = 0;
    
    std::cout << "Post-processing: " << (m_postprocess_enabled ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << "Decoder started" << std::endl << std::endl;
}

//============================================================================
// vdec_stop() (Section 5.2.4)
//============================================================================

int Avcdec::vdec_stop()
{
    std::cout << "vdec_stop()" << std::endl;
    
    m_threadRunning = false;
    
    if (m_decodeThread.joinable())
    {
        std::cout << "Waiting for decode thread..." << std::endl;
        m_decodeThread.join();
    }
    
    m_started = false;
    m_finished = true;
    
    std::cout << "Frames decoded: " << m_framesDecoded << std::endl;
    std::cout << "Pictures output: " << m_picturesOutput << std::endl;
    std::cout << "Decoder stopped" << std::endl << std::endl;
    
    return 0;
}

//============================================================================
// vdec_postprocess() (Section 5.2.5)
//============================================================================

void Avcdec::vdec_postprocess(UInt16 TYPE)
{
    std::cout << "vdec_postprocess(TYPE=" << TYPE << ")" << std::endl;
    m_postprocess_enabled = (TYPE != 0);
}

//============================================================================
// vdec_put_bs() (Section 5.2.6)
//============================================================================

unsigned int Avcdec::vdec_put_bs(
    Byte* PAYLOAD,
    UInt32 LENGTH,
    UInt16 END_OF_AU,
    UInt32 PTS,
    UInt16 ERR_FLAG,
    UInt32 ERR_SN_SKIP)
{
    std::lock_guard<std::mutex> lock(m_streamMutex);
    
    std::cout << "vdec_put_bs(len=" << LENGTH << ", end_of_au=" << END_OF_AU 
              << ", pts=" << PTS << ")" << std::endl;
    
    if (PAYLOAD && LENGTH > 0)
    {
        // Check buffer space
        if (!CheckBufferSpace(LENGTH))
        {
            std::cout << "ERROR: Buffer overflow!" << std::endl;
            return (unsigned int)-1;
        }
        
        // Copy to stream buffer
        memcpy(m_streamBuffer + m_streamSize, PAYLOAD, LENGTH);
        m_streamSize += LENGTH;
        
        std::cout << "  Added " << LENGTH << " bytes (total: " 
                  << m_streamSize << ")" << std::endl;
    }
    
    // Update JM globals
    g_memory_buffer = m_streamBuffer;
    g_memory_size = m_streamSize;
    
    // Handle END_OF_AU
    if (END_OF_AU == 1)
    {
        std::cout << "  END_OF_AU marker received" << std::endl;
        HandleEndOfAU();
    }
    
    return LENGTH;
}

//============================================================================
// vdec_get_picture() (Section 5.2.7)
//============================================================================

Byte* Avcdec::vdec_get_picture(PICMETAINFO_AVC* PIC_METAINFO)
{
    std::lock_guard<std::mutex> lock(m_frameMutex);
    
    if (m_frameQueue.empty())
    {
        return nullptr;
    }
    
    Frame frame = std::move(m_frameQueue.front());
    m_frameQueue.pop();
    
    m_currentFrame = std::move(frame.yuv);
    
    if (PIC_METAINFO)
    {
        *PIC_METAINFO = frame.metadata;
    }
    
    std::cout << "vdec_get_picture() -> " << frame.width << "x" 
              << frame.height << std::endl;
    
    return m_currentFrame.data();
}

//============================================================================
// vdec_get_status() (Section 5.2.8)
//============================================================================

unsigned int Avcdec::vdec_get_status(
    UInt16* DEC_STATUS,
    UInt16* DISP_STATUS,
    UInt16* ERR_STATUS)
{
    UInt16 dec_status = 0;
    
    if (m_threadRunning)
        dec_status |= (1 << 6);  // Picture decode in progress
    
    if (m_streamSize == 0)
        dec_status |= (1 << 2);  // Stream empty
    
    *DEC_STATUS = dec_status;
    
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        *DISP_STATUS = m_frameQueue.empty() ? 0 : 1;
    }
    
    *ERR_STATUS = 0;
    
    std::cout << "vdec_get_status() -> DEC=0x" << std::hex << *DEC_STATUS 
              << " DISP=" << std::dec << (int)*DISP_STATUS 
              << " ERR=0x" << std::hex << *ERR_STATUS << std::dec << std::endl;
    
    return 0;
}

//============================================================================
// vdec_get_DecodedHandle() (Section 5.2.9)
//============================================================================

void* Avcdec::vdec_get_DecodedHandle()
{
    return nullptr;
}

//============================================================================
// vdec_release_pic_buffer() (Section 5.2.10)
//============================================================================

void Avcdec::vdec_release_pic_buffer(Byte* PIC_ADDR)
{
    std::cout << "vdec_release_pic_buffer()" << std::endl;
    m_currentFrame.clear();
}

//============================================================================
// vdec_YUV420toRGB24() (Section 5.2.11)
//============================================================================

int Avcdec::vdec_YUV420toRGB24(
    int Mode,
    unsigned char* iBGR,
    unsigned char* iYUV,
    int width,
    int height)
{
    std::cout << "vdec_YUV420toRGB24(" << width << "x" << height << ")" << std::endl;
    
    int ySize = width * height;
    int uvSize = ySize / 4;
    
    unsigned char* y_ptr = iYUV;
    unsigned char* u_ptr = iYUV + ySize;
    unsigned char* v_ptr = iYUV + ySize + uvSize;
    
    for (int i = 0; i < ySize; i++)
    {
        int x = i % width;
        int y = i / width;
        int u_idx = (y / 2) * (width / 2) + (x / 2);
        
        int Y = y_ptr[i];
        int U = u_ptr[u_idx];
        int V = v_ptr[u_idx];
        
        int R = Y + (1.402f * (V - 128));
        int G = Y - (0.344f * (U - 128)) - (0.714f * (V - 128));
        int B = Y + (1.772f * (U - 128));
        
        R = std::max(0, std::min(255, R));
        G = std::max(0, std::min(255, G));
        B = std::max(0, std::min(255, B));
        
        int rgb_idx = i * 3;
        iBGR[rgb_idx] = R;
        iBGR[rgb_idx + 1] = G;
        iBGR[rgb_idx + 2] = B;
    }
    
    return width * height * 3;
}

//============================================================================
// vdec_YUV420toRGB24_2() (Section 5.2.12)
//============================================================================

void Avcdec::vdec_YUV420toRGB24_2(
    unsigned char* y,
    unsigned char* u,
    unsigned char* v,
    unsigned char* rgb,
    int width,
    int height)
{
    std::cout << "vdec_YUV420toRGB24_2(" << width << "x" << height << ")" << std::endl;
    
    int ySize = width * height;
    
    for (int i = 0; i < ySize; i++)
    {
        int x = i % width;
        int y_idx = i / width;
        int u_idx = (y_idx / 2) * (width / 2) + (x / 2);
        
        int Y = y[i];
        int U = u[u_idx];
        int V = v[u_idx];
        
        int R = Y + (1.402f * (V - 128));
        int G = Y - (0.344f * (U - 128)) - (0.714f * (V - 128));
        int B = Y + (1.772f * (U - 128));
        
        R = std::max(0, std::min(255, R));
        G = std::max(0, std::min(255, G));
        B = std::max(0, std::min(255, B));
        
        int rgb_idx = i * 3;
        rgb[rgb_idx] = R;
        rgb[rgb_idx + 1] = G;
        rgb[rgb_idx + 2] = B;
    }
}

//============================================================================
// YUV420toRGB24_DX() (Section 5.2.13)
//============================================================================

int Avcdec::YUV420toRGB24_DX(
    int Mode,
    unsigned char* iBGR,
    unsigned char* iYUV,
    int width,
    int height)
{
    return vdec_YUV420toRGB24(Mode, iBGR, iYUV, width, height);
}

//============================================================================
// Private Methods
//============================================================================

void Avcdec::InitJMDecoder()
{
    std::cout << "InitJMDecoder()" << std::endl;
    
    // Create input parameters for JM
    InputParameters inputParams = {};
    
    // Set to memory mode (no file I/O)
    strcpy(inputParams.infile, "");
    strcpy(inputParams.outfile, "");
    strcpy(inputParams.reffile, "");
    
    // Configure JM parameters
    inputParams.FileFormat = 0;        // Annex B format
    inputParams.write_uv = 1;          // Write UV
    inputParams.bDisplayDecParams = 0;  // No display
    
    std::cout << "Opening JM decoder..." << std::endl;
    
    if (OpenDecoder(&inputParams) != DEC_OPEN_NOERR)
    {
        std::cout << "ERROR: OpenDecoder failed!" << std::endl;
        return;
    }
    
    m_jmContext.p_Dec = (void*)p_Dec;
    m_jmContext.p_Vid = (void*)(p_Dec ? p_Dec->p_Vid : nullptr);
    m_jmContext.initialized = 1;
    
    std::cout << "JM decoder initialized" << std::endl;
}

void Avcdec::DecodeThreadFunc()
{
    std::cout << "Decode thread started" << std::endl;
    
    while (m_threadRunning)
    {
        // Update JM globals
        {
            std::lock_guard<std::mutex> lock(m_streamMutex);
            g_memory_buffer = m_streamBuffer;
            g_memory_size = m_streamSize;
        }
        
        // Decode one frame
        int ret = DecodeOneFrame(p_Dec);
        
        if (ret == DEC_EOS)
        {
            std::cout << "  EOS reached" << std::endl;
            break;
        }
        
        if (ret == DEC_SUCCEED)
        {
            m_framesDecoded++;
            std::cout << "  Frame decoded: " << m_framesDecoded << std::endl;
            ProcessDecodedFrames();
        }
        
        // Small sleep to prevent CPU spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    std::cout << "Decode thread stopped" << std::endl;
}

void Avcdec::ProcessDecodedFrames()
{
    if (!p_Dec || !p_Dec->p_Vid)
        return;
    
    VideoParameters *p_Vid = p_Dec->p_Vid;
    
    // Get decoded picture
    if (pDecPicList)
    {
        int width = pDecPicList->iWidth;
        int height = pDecPicList->iHeight;
        
        if (width > 0 && height > 0 && pDecPicList->pY)
        {
            Frame frame;
            frame.width = width;
            frame.height = height;
            
            // Calculate size
            int ySize = width * height;
            int uvSize = ySize / 4;
            int totalSize = ySize + 2 * uvSize;
            
            frame.yuv.resize(totalSize);
            
            // Copy Y plane
            memcpy(frame.yuv.data(), pDecPicList->pY, ySize);
            
            // Copy U plane
            if (pDecPicList->pU)
                memcpy(frame.yuv.data() + ySize, pDecPicList->pU, uvSize);
            
            // Copy V plane
            if (pDecPicList->pV)
                memcpy(frame.yuv.data() + ySize + uvSize, pDecPicList->pV, uvSize);
            
            // Fill metadata
            frame.metadata.pic_width = width;
            frame.metadata.pic_height = height;
            frame.metadata.pic_type = 0;  // Placeholder
            frame.metadata.bit_depth = 8;
            frame.metadata.pts.input_pts = 0;
            
            // Queue frame
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_frameQueue.push(std::move(frame));
                m_picturesOutput++;
            }
            
            std::cout << "    Picture queued: " << width << "x" << height 
                      << " (total: " << m_picturesOutput << ")" << std::endl;
        }
    }
}

bool Avcdec::CheckBufferSpace(UInt32 needed_bytes)
{
    return (m_streamSize + needed_bytes <= m_streamCapacity);
}

void Avcdec::HandleEndOfAU()
{
    if (CheckBufferSpace(3))
    {
        m_streamBuffer[m_streamSize++] = 0x00;
        m_streamBuffer[m_streamSize++] = 0x00;
        m_streamBuffer[m_streamSize++] = 0x01;
        std::cout << "  END_OF_AU marker added" << std::endl;
    }
}

void Avcdec::Cleanup()
{
    std::cout << "Cleanup()" << std::endl;
    
    m_threadRunning = false;
    
    if (m_decodeThread.joinable())
    {
        m_decodeThread.join();
    }
    
    if (m_jmContext.initialized)
    {
        CloseDecoder();
        m_jmContext.initialized = 0;
    }
    
    if (m_streamBuffer)
    {
        delete[] m_streamBuffer;
        m_streamBuffer = nullptr;
    }
    
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        while (!m_frameQueue.empty())
        {
            m_frameQueue.pop();
        }
        m_currentFrame.clear();
    }
}