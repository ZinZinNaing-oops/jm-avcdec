#include "AvcDecoder.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

//============================================================================
// JM External Functions and Variables
//============================================================================
extern "C" {
    #include "../../JM/ldecod/inc/global.h"
    #include "../../JM/ldecod/inc/annexb.h"
     #include "../../JM/ldecod/inc/image.h" 
    
    // Memory mode globals
    extern unsigned char* g_memory_buffer;
    extern int g_memory_size;
    extern int g_memory_pos;
    extern int g_memory_mode;
    
    // Memory mode functions
    extern void AnnexBMemoryModeInit(unsigned char *buffer, int size);
    extern void AnnexBMemoryModeReset(void);
    extern void AnnexBMemoryModeExit(void);
    
    // JM structures
    extern DecoderParams *p_Dec;
    extern DecodedPicList *pDecPicList;
    
    // JM functions
    extern int OpenDecoder(InputParameters *p_Inp);
    extern int DecodeOneFrame(DecoderParams *pDecoder);
    extern void FinitDecoder(DecodedPicList **pDecPicList);
    extern int CloseDecoder(void);
}

#define DEC_OPEN_NOERR  0
#define DEC_SUCCEED 0
#define DEC_EOS 1

//============================================================================
// Constructor - Initialize decoder
//============================================================================
Avcdec::Avcdec(DECPARAM_AVC *INPUT_PARAM)
    : m_streamBuffer(nullptr),
      m_streamCapacity(0),
      m_streamSize(0),
      m_started(false),
      m_finished(false),
      m_postprocess_enabled(false)
{
    std::cout << "Avcdec created" << std::endl;
    
    // Copy configuration
    m_config = *INPUT_PARAM;
    
    // Allocate stream buffer
    m_streamCapacity = m_config.bs_buf_size;
    m_streamBuffer = new Byte[m_streamCapacity];
    memset(m_streamBuffer, 0, m_streamCapacity);
    m_streamSize = 0;  
    std::cout << "  Buffer: " << m_streamCapacity / (1024*1024) << " MB" << std::endl;

    // ALLOCATE PICTURE BUFFERS 
    std::cout << "Display Picture Buffers" << std::endl;
    m_bufferCount = m_config.disp_buf_num;
    m_pictureBuffers = new PictureBuffer[m_bufferCount];
    
    // Calculate size per picture (YUV420)
    int pic_width = m_config.disp_max_width;
    int pic_height = m_config.disp_max_height;
    int ySize = pic_width * pic_height;
    int uvSize = ySize / 4;
    int pic_size = ySize + 2 * uvSize;  // YUV420 size
    
    // Allocate each picture buffer
    for (int i = 0; i < m_bufferCount; i++)
    {
        m_pictureBuffers[i].data = new Byte[pic_size];
        m_pictureBuffers[i].width = pic_width;
        m_pictureBuffers[i].height = pic_height;
        m_pictureBuffers[i].locked = false; 
        memset(m_pictureBuffers[i].data, 0, pic_size);
    }
}

//============================================================================
// Destructor - Cleanup decoder
//============================================================================
Avcdec::~Avcdec()
{
    std::cout << "Freeing buffers:" << std::endl;
    
    // Free stream buffer
    if (m_streamBuffer)
    {
        delete[] m_streamBuffer;
        m_streamBuffer = nullptr;
        std::cout << "  - Stream buffer freed" << std::endl;
    }
    
    // Free picture buffers ONLY if allocated
    if (m_pictureBuffers && m_bufferCount > 0)
    {
        for (int i = 0; i < m_bufferCount; i++)
        {
            if (m_pictureBuffers[i].data != nullptr)
            {
                delete[] m_pictureBuffers[i].data;
                m_pictureBuffers[i].data = nullptr;
            }
        }
        delete[] m_pictureBuffers;
        m_pictureBuffers = nullptr;
        
        std::cout << "  - Picture buffers freed" << std::endl;
    }
    
    // Clear queue
    while (!m_frameQueue.empty())
        m_frameQueue.pop();
    
    std::cout << "Cleanup complete" << std::endl;
}

//============================================================================
// vdec_start() - Start decoding
//============================================================================

void Avcdec::vdec_start(UInt16 PLAY_MODE, UInt16 POST_PROCESS)
{
    std::cout << "vdec_start()" << std::endl;
    
    m_started = true;
    m_finished = false;
    m_postprocess_enabled = (POST_PROCESS != 0);
    
    // Setup memory mode globals for JM
    AnnexBMemoryModeInit(m_streamBuffer, 0);

    // Initialize JM decoder
    InitJMDecoder();  
}

//============================================================================
// 5.2.4 vdec_stop() - Stop decoding
//============================================================================

int Avcdec::vdec_stop()
{
    std::cout << "vdec_stop()" << std::endl;
    
    m_started = false;
    m_finished = true;
    
    return 0;
}

//============================================================================
// 5.2.5 vdec_postprocess() - Set post-processing
//============================================================================

void Avcdec::vdec_postprocess(UInt16 TYPE)
{
    std::cout << "vdec_postprocess()" << std::endl;
    m_postprocess_enabled = (TYPE != 0);
}

//============================================================================
// 5.2.6 vdec_put_bs() - Feed H.264 bitstream data
//============================================================================
unsigned int Avcdec::vdec_put_bs(
    Byte* PAYLOAD,
    UInt32 LENGTH,
    UInt16 END_OF_AU,
    UInt32 PTS,
    UInt16 ERR_FLAG,
    UInt32 ERR_SN_SKIP)
{
    std::cout << "vdec_put_bs(" << LENGTH << " bytes)" << std::endl;
    
    // Check if decoder started
    if (!m_started)
    {
        std::cout << "  ERROR: Decoder not started" << std::endl;
        return (unsigned int)-1;
    }
    
    // Accumulate data in stream buffer
    if (PAYLOAD && LENGTH > 0)
    {
        // Check buffer space
        if (!CheckBufferSpace(LENGTH))
        {
            std::cout << "  ERROR: Buffer overflow" << std::endl;
            return (unsigned int)-1;
        }
        
        // Copy data to buffer
        memcpy(m_streamBuffer + m_streamSize, PAYLOAD, LENGTH);
        m_streamSize += LENGTH;
        
        std::cout << "  Total buffer: " << m_streamSize << " bytes" << std::endl;
    }
    
    // Update JM's buffer view
    g_memory_size = m_streamSize;
    
    // Handle END_OF_AU marker
    if (END_OF_AU == 1)
    {
        std::cout << "  END_OF_AU marker received" << std::endl;
        HandleEndOfAU();
        
        // Decode buffer when END_OF_AU received
        DecodeBuffer();
    }
    
    return LENGTH;
}

//============================================================================
// vdec_get_picture() - Get decoded picture
//============================================================================
Byte* Avcdec::vdec_get_picture(PICMETAINFO_AVC* PIC_METAINFO)
{
    {
        std::lock_guard<std::mutex> lock(m_frameQueueMutex);
        
        if (m_frameQueue.empty())
        {
            std::cout << "  vdec_get_picture(): No frames available" << std::endl;
            return NULL;
        }
        
        QueuedFrame frame = m_frameQueue.front();
        m_frameQueue.pop();
        
        std::cout << "  vdec_get_picture(): Retrieved frame " << m_frameCount 
                  << " (" << frame.width << "x" << frame.height << ")" << std::endl;
        
        if (PIC_METAINFO)
        {
            PIC_METAINFO->pic_width = frame.width;
            PIC_METAINFO->pic_height = frame.height;
            PIC_METAINFO->pic_type = frame.metadata.pic_type;
            PIC_METAINFO->bit_depth = frame.metadata.bit_depth;
        }
        
        m_frameCount++;
        return frame.data;
    }
}

//============================================================================
// vdec_get_status() - Get decoder status
//============================================================================

unsigned int Avcdec::vdec_get_status(
    UInt16* DEC_STATUS,
    UInt16* DISP_STATUS,
    UInt16* ERR_STATUS)
{
    UInt16 dec_status = 0;
    
    // Set status flags
    if (m_started && !m_finished)
        dec_status |= (1 << 6);  // Decoding in progress
    
    *DEC_STATUS = dec_status;
    //*DISP_STATUS = m_frameInfoQueue.empty() ? 0 : 1;
    *ERR_STATUS = 0;
    
    return 0;
}

//============================================================================
// 5.2.9 vdec_get_DecodedHandle() - Get picture ready handle
//============================================================================

void* Avcdec::vdec_get_DecodedHandle()
{
    return nullptr;
}

//============================================================================
// 5.2.10 vdec_release_pic_buffer() - Release picture buffer
//============================================================================

void Avcdec::vdec_release_pic_buffer(Byte* PIC_ADDR)
{
    if (!PIC_ADDR)
        return;
    
    std::cout << "  vdec_release_pic_buffer(): Releasing buffer " << (void*)PIC_ADDR << std::endl;
    
    // Mark buffer as available
    for (int i = 0; i < m_bufferCount; i++)
    {
        if (m_pictureBuffers[i].data == PIC_ADDR)
        {
            m_pictureBuffers[i].locked = false;
            std::cout << "    ✓ Buffer " << i << " marked as available" << std::endl;
            return;
        }
    }
    
    std::cout << "    WARNING: Buffer address not found" << std::endl;
}

//============================================================================
// 5.2.11 vdec_YUV420toRGB24() - Convert YUV420 to RGB24
//============================================================================

int Avcdec::vdec_YUV420toRGB24(
    int Mode,
    unsigned char* iBGR,
    unsigned char* iYUV,
    int width,
    int height)
{
    int ySize = width * height;
    int uvSize = ySize / 4;
    
    unsigned char* y_ptr = iYUV;
    unsigned char* u_ptr = iYUV + ySize;
    unsigned char* v_ptr = iYUV + ySize + uvSize;
    
    // Convert each pixel
    for (int i = 0; i < ySize; i++)
    {
        int x = i % width;
        int y = i / width;
        int u_idx = (y / 2) * (width / 2) + (x / 2);
        
        int Y = y_ptr[i];
        int U = u_ptr[u_idx];
        int V = v_ptr[u_idx];
        
        // YUV to RGB conversion formula
        int R = Y + (1.402f * (V - 128));
        int G = Y - (0.344f * (U - 128)) - (0.714f * (V - 128));
        int B = Y + (1.772f * (U - 128));
        
        // Clamp to valid range
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
// 5.2.12 vdec_YUV420toRGB24_2() - Fast YUV420 to RGB24
//============================================================================

void Avcdec::vdec_YUV420toRGB24_2(
    unsigned char* y,
    unsigned char* u,
    unsigned char* v,
    unsigned char* rgb,
    int width,
    int height)
{
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
// 5.2.13 YUV420toRGB24_DX() - YUV420 to BGR24 for DirectDraw
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
    std::cout << "  InitJMDecoder()" << std::endl;
    
    // Create JM input parameters
    InputParameters inputParams = {};
    
    // Set empty file paths (memory mode)
    strcpy(inputParams.infile, "");
    strcpy(inputParams.outfile, "");
    strcpy(inputParams.reffile, "");
    
    inputParams.FileFormat = 0;
    inputParams.write_uv = 0;
    inputParams.bDisplayDecParams = 0;
    
    // Open decoder
    if (OpenDecoder(&inputParams) != DEC_OPEN_NOERR)
    {
        std::cout << "  ERROR: OpenDecoder failed" << std::endl;
        return;
    }
    
    std::cout << "  JM decoder initialized" << std::endl;
}

void Avcdec::DecodeBuffer()
{
    if (!p_Dec)
    {
        std::cout << "  ERROR: p_Dec is NULL" << std::endl;
        return;
    }
    
    int frame_count = 0;
    int picture_count = 0;
    g_memory_pos = 0;
    
    std::cout << "  Starting decode loop..." << std::endl;
    
    while (true)
    {
        int ret = DecodeOneFrame(p_Dec);
        
        if (ret == DEC_EOS)
        {
            std::cout << "  ========== END OF STREAM ==========" << std::endl;
            break;
        }
        
        if (ret == DEC_SUCCEED)
        {
            frame_count++;
            std::cout << "\n  ===== Frame " << frame_count << " Decoded =====" << std::endl;
            
            // ========== RETRIEVE DECODED PICTURE ==========
            if (p_Dec->p_Vid)
            {
                VideoParameters *p_Vid = p_Dec->p_Vid;
                
                // Get the head of the decoded picture list
                DecodedPicList *pDecPic = p_Vid->pDecOuputPic;
                
                if (pDecPic)
                {
                    std::cout << "    pDecPic found: iWidth=" << pDecPic->iWidth 
                              << " iHeight=" << pDecPic->iHeight 
                              << " POC=" << pDecPic->iPOC 
                              << " bValid=" << pDecPic->bValid << std::endl;
                    
                    // ========== VALIDATE PICTURE ==========
                    if (pDecPic->bValid == 1 && 
                        pDecPic->iWidth > 0 && 
                        pDecPic->iHeight > 0 && 
                        pDecPic->pY != NULL)
                    {
                        std::cout << "    ✓ Picture is VALID and READY" << std::endl;
                        std::cout << "      Width:  " << pDecPic->iWidth << std::endl;
                        std::cout << "      Height: " << pDecPic->iHeight << std::endl;
                        std::cout << "      Y buffer: " << (void*)pDecPic->pY << std::endl;
                        std::cout << "      U buffer: " << (void*)pDecPic->pU << std::endl;
                        std::cout << "      V buffer: " << (void*)pDecPic->pV << std::endl;
                        
                        // ========== PROCESS THE PICTURE ==========
                        ProcessDecodedPicture(pDecPic);
                        picture_count++;
                        
                        // Mark picture as consumed
                        pDecPic->bValid = 0;
                        
                        std::cout << "    Picture marked as consumed" << std::endl;
                    }
                    else
                    {
                        std::cout << "    ✗ Picture NOT valid" << std::endl;
                        if (pDecPic->bValid == 0)
                            std::cout << "      - Not marked valid (bValid=0)" << std::endl;
                        if (pDecPic->iWidth <= 0 || pDecPic->iHeight <= 0)
                            std::cout << "      - Invalid dimensions: " << pDecPic->iWidth 
                                      << "x" << pDecPic->iHeight << std::endl;
                        if (pDecPic->pY == NULL)
                            std::cout << "      - Y buffer is NULL" << std::endl;
                    }
                }
                else
                {
                    std::cout << "    ERROR: pDecPic is NULL" << std::endl;
                }
            }
        }
        else
        {
            std::cout << "  Decode error or no more frames. ret=" << ret << std::endl;
            break;
        }
    }
    
    std::cout << "\n  ========== DECODE COMPLETE ==========" << std::endl;
    std::cout << "  Total frames decoded: " << frame_count << std::endl;
    std::cout << "  Total pictures retrieved: " << picture_count << std::endl;
}

void Avcdec::ProcessDecodedPicture(DecodedPicList *pPic)
{
    if (!pPic || !pPic->pY)
    {
        std::cout << "      ERROR: Invalid picture pointer" << std::endl;
        return;
    }
    
    std::cout << "    ========== PROCESSING PICTURE ==========" << std::endl;
    
    int width = pPic->iWidth;
    int height = pPic->iHeight;
    int ySize = width * height;
    int uvSize = ySize / 4;
    int totalSize = ySize + 2 * uvSize;
    
    std::cout << "      Picture size: " << width << "x" << height << std::endl;
    std::cout << "      Y plane: " << ySize << " bytes" << std::endl;
    std::cout << "      U plane: " << uvSize << " bytes" << std::endl;
    std::cout << "      V plane: " << uvSize << " bytes" << std::endl;
    std::cout << "      Total: " << totalSize << " bytes" << std::endl;
    
    // ========== GET AVAILABLE DISPLAY BUFFER ==========
    PictureBuffer* buffer = GetAvailableBuffer();
    if (!buffer)
    {
        std::cout << "      ERROR: No available display buffer" << std::endl;
        return;
    }
    
    std::cout << "      Using display buffer" << std::endl;
    
    // ========== COPY YUV DATA TO DISPLAY BUFFER ==========
    if (pPic->pY)
    {
        std::cout << "      Copying Y plane..." << std::endl;
        memcpy(buffer->data, pPic->pY, ySize);
    }
    
    if (pPic->pU)
    {
        std::cout << "      Copying U plane..." << std::endl;
        memcpy(buffer->data + ySize, pPic->pU, uvSize);
    }
    else
    {
        std::cout << "      WARNING: U plane is NULL, filling with 128" << std::endl;
        memset(buffer->data + ySize, 128, uvSize);
    }
    
    if (pPic->pV)
    {
        std::cout << "      Copying V plane..." << std::endl;
        memcpy(buffer->data + ySize + uvSize, pPic->pV, uvSize);
    }
    else
    {
        std::cout << "      WARNING: V plane is NULL, filling with 128" << std::endl;
        memset(buffer->data + ySize + uvSize, 128, uvSize);
    }
    
    // ========== UPDATE BUFFER METADATA ==========
    buffer->width = width;
    buffer->height = height;
    buffer->poc = pPic->iPOC;
    buffer->locked = true;
    
    std::cout << "      ✓ Picture data copied to display buffer successfully" << std::endl;
    std::cout << "      Buffer address: " << (void*)buffer->data << std::endl;
    
    // ========== QUEUE FRAME FOR APPLICATION ==========
    QueueFrameForDisplay(buffer);
    
    std::cout << "    ========== END PROCESSING ==========" << std::endl;
}

bool Avcdec::CheckBufferSpace(UInt32 needed_bytes)
{
    return (m_streamSize + needed_bytes <= m_streamCapacity);
}

void Avcdec::HandleEndOfAU()
{
    // Add dummy start code to signal access unit end
    if (CheckBufferSpace(3))
    {
        m_streamBuffer[m_streamSize++] = 0x00;
        m_streamBuffer[m_streamSize++] = 0x00;
        m_streamBuffer[m_streamSize++] = 0x01;
        g_memory_size = m_streamSize;
    }
}

Avcdec::PictureBuffer* Avcdec::GetAvailableBuffer()
{
    if (!m_pictureBuffers || m_bufferCount == 0)
    {
        std::cout << "      ERROR: Picture buffers not allocated" << std::endl;
        return NULL;
    }
    
    // Find first unlocked buffer
    for (int i = 0; i < m_bufferCount; i++)
    {
        if (!m_pictureBuffers[i].locked)
        {
            std::cout << "      Using buffer " << i << " (unlocked)" << std::endl;
            return &m_pictureBuffers[i];
        }
    }
    
    // All buffers locked - warning and reuse first one
    std::cout << "      WARNING: All buffers locked, reusing buffer 0" << std::endl;
    std::cout << "      Application may not be calling vdec_release_pic_buffer()" << std::endl;
    
    // Force clear the first buffer
    m_pictureBuffers[0].locked = false;
    return &m_pictureBuffers[0];
}

void Avcdec::QueueFrameForDisplay(PictureBuffer* buffer)
{
    if (!buffer)
        return;
    
    std::cout << "      Queueing frame for application..." << std::endl;
    
    QueuedFrame frame;
    frame.data = buffer->data;
    frame.width = buffer->width;
    frame.height = buffer->height;
    frame.poc = buffer->poc;
    frame.metadata.pic_width = buffer->width;
    frame.metadata.pic_height = buffer->height;
    frame.metadata.pic_type = 0;
    frame.metadata.bit_depth = 8;
    
    {
        std::lock_guard<std::mutex> lock(m_frameQueueMutex);
        m_frameQueue.push(frame);
    }
    
    std::cout << "      ✓ Frame queued! Queue size: " << m_frameQueue.size() << std::endl;
}