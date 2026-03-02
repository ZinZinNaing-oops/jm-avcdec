#include "AvcDecoder.h"
extern "C" {
#include "../../jm_wrapper.h"
}
#include <stdio.h>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <iterator>
#include <iostream>

AvcDecoder::AvcDecoder()
{
    DECPARAM_AVC param{};

    m_streamCapacity = 50 * 1024 * 1024;  // 50MB
    m_streamBuffer = new uint8_t[m_streamCapacity];
    m_streamSize = 0;
    m_started = false;
}

AvcDecoder::~AvcDecoder()
{
    jm_stop();
    delete[] m_streamBuffer;
}

bool AvcDecoder::vdec_start(UInt16 PLAY_MODE, UInt16 POST_PROCESS)
{
    std::cout << "=== DECODER STARTED ===\n";
    if (m_started)
        return false;

    m_streamSize = 0;
    m_startCodePositions.clear();

    g_memory_buffer = m_streamBuffer;
    g_memory_size   = 0;
    g_memory_pos    = 0;

    uint8_t dummy = 0;
    if (jm_start_from_memory(&dummy, 0) != 0)
        return false;

    m_started = true;
    return true;
}

unsigned int AvcDecoder::vdec_put_bs(
    uint8_t* payload,
    uint32_t length,
    uint16_t end_of_au,
    uint32_t pts,
    uint16_t err_flag,
    uint32_t err_sn_skip)
{
    if (!m_started) 
        return -1;

    if (m_finished){
        std::cout << "IGNORING INPUT: decoder already finished\n";
        return 0;
    }
        
    if (m_streamSize + length + 4 > m_streamCapacity)
        return -1;

    size_t oldSize = m_streamSize;

    // Append payload
    memcpy(m_streamBuffer + m_streamSize, payload, length);
    m_streamSize += length;

    // Append dummy start code if END_OF_AU
    if (end_of_au == 1)
    {
        m_streamBuffer[m_streamSize++] = 0x00;
        m_streamBuffer[m_streamSize++] = 0x00;
        m_streamBuffer[m_streamSize++] = 0x01;
    }

    // Update JM memory
    g_memory_buffer = m_streamBuffer;
    g_memory_size   = m_streamSize;

    // Scan only new area
    scanNewStartCodes(oldSize);

    // Decode only when full NAL exists
    if (hasFullNAL())
    {
        decodeAvailable();
    }

    return length;
}

void AvcDecoder::vdec_decode()
{
    while (true)
    {
        int ret = jm_decode_one_frame();

        if (ret == 0)
        {
            // frame ready
            int width  = jm_get_width();
            int height = jm_get_height();

            unsigned char* y = jm_get_y();
            unsigned char* u = jm_get_u();
            unsigned char* v = jm_get_v();

            if (!y || !u || !v)
                continue;

            int ySize  = width * height;
            int uvSize = ySize / 4;

            Frame frame;
            frame.width  = width;
            frame.height = height;
            frame.yuv.resize(ySize + 2 * uvSize);

            memcpy(frame.yuv.data(), y, ySize);
            memcpy(frame.yuv.data() + ySize, u, uvSize);
            memcpy(frame.yuv.data() + ySize + uvSize, v, uvSize);

            m_frameQueue.push(std::move(frame));

            continue;
        }

        if (ret == 1)
        {
            // EOF reached — but continue to flush DPB
            continue;
        }

        break; // no more frames
    }
}

void AvcDecoder::decodeAvailable()
{
    if (m_streamSize == 0 || m_finished)
        return;
  
    while (true)
    {
        if (!hasFullNAL())
            return;

        int ret = jm_decode_one_frame();

        if (ret == 1)   // JM end-of-stream signal
        {
            m_streamSize = 0;
            m_finished = true;
            break;      // STOP FOREVER
        }

        if (ret != 0)
            break;

        int width  = jm_get_width();
        int height = jm_get_height();

        unsigned char* y = jm_get_y();
        unsigned char* u = jm_get_u();
        unsigned char* v = jm_get_v();

        if (!y || !u || !v)
            break;

        int ySize  = width * height;
        int uvSize = ySize / 4;

        Frame frame;
        frame.width  = width;
        frame.height = height;
        frame.yuv.resize(ySize + 2 * uvSize);

        memcpy(frame.yuv.data(), y, ySize);
        memcpy(frame.yuv.data() + ySize, u, uvSize);
        memcpy(frame.yuv.data() + ySize + uvSize, v, uvSize);

        m_frameQueue.push(std::move(frame));

        jm_consume_output();
        // 🔥 Remove data actually consumed by JM
if (g_memory_pos > 0)
{
    memmove(m_streamBuffer,
            m_streamBuffer + g_memory_pos,
            m_streamSize - g_memory_pos);

    m_streamSize -= g_memory_pos;

    g_memory_pos = 0;

    g_memory_buffer = m_streamBuffer;
    g_memory_size   = m_streamSize;

    // You MUST rescan start codes now
    m_startCodePositions.clear();
    scanNewStartCodes(0);
}
        
    }

}

void AvcDecoder::vdec_stop()
{
    std::cout << "=== DECODER STOPPED ===\n";
    if (!m_started)
        return;

    jm_stop();
    m_started = false;
}

uint8_t* AvcDecoder::vdec_get_picture(int* width, int* height)
{
    if (m_frameQueue.empty())
        return nullptr;

    Frame& frame = m_frameQueue.front();

    *width  = frame.width;
    *height = frame.height;

    uint8_t* ptr = frame.yuv.data();

    m_frameQueue.pop();

    return ptr;
}

void AvcDecoder::scanNewStartCodes(size_t oldSize)
{
    for (size_t i = (oldSize >= 2 ? oldSize - 2 : 0);
         i + 2 < m_streamSize;
         ++i)
    {
        if (m_streamBuffer[i]     == 0x00 &&
            m_streamBuffer[i + 1] == 0x00 &&
            m_streamBuffer[i + 2] == 0x01)
        {
            m_startCodePositions.push_back(i);
        }
    }
}

bool AvcDecoder::hasFullNAL()
{
    return m_startCodePositions.size() >= 2;
}
/*----------------------------------------
  Open bitstream from file (load into memory)
----------------------------------------*/
bool AvcDecoder::open(const char* filename)
{
    // Load file into memory buffer
    std::ifstream file(filename, std::ios::binary);
    if (!file)
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }

    m_buffer.assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());

    if (m_buffer.empty())
    {
        std::cerr << "File is empty." << std::endl;
        return false;
    }

    // Start JM decoder in memory mode
    if (jm_start_from_memory(
            reinterpret_cast<unsigned char*>(m_buffer.data()),
            static_cast<int>(m_buffer.size())) != 0)
    {
        std::cerr << "JM decoder failed to start." << std::endl;
        return false;
    }

    return true;
}

void AvcDecoder::decodeInternal()
{
    while (true)
    {
        int ret = jm_decode_one_frame();

        if (ret == 1)  // end of stream
            break;

        if (ret != 0)
            break;

        int width = jm_get_width();
        int height = jm_get_height();

        unsigned char* y = jm_get_y();
        unsigned char* u = jm_get_u();
        unsigned char* v = jm_get_v();

        if (!y || !u || !v)
            break;

        int ySize = width * height;
        int uvSize = ySize / 4;

        Frame frame;
        frame.width = width;
        frame.height = height;
        frame.yuv.resize(ySize + uvSize * 2);

        memcpy(frame.yuv.data(), y, ySize);
        memcpy(frame.yuv.data() + ySize, u, uvSize);
        memcpy(frame.yuv.data() + ySize + uvSize, v, uvSize);

        m_frameQueue.push(std::move(frame));
    }
}

uint8_t* AvcDecoder::vdec_get_picture()
{
    if (m_frameQueue.empty())
        return nullptr;

    Frame& frame = m_frameQueue.front();

    return frame.yuv.data();
}

/*----------------------------------------
  Decode one frame
----------------------------------------*/
bool AvcDecoder::decode_one_frame(
    uint8_t** y,
    uint8_t** u,
    uint8_t** v,
    int* width,
    int* height)
{
    if (!y || !u || !v || !width || !height)
        return false;

    while (true)
    {
        int ret = jm_decode_one_frame();
        printf("jm_decode_one_frame ret = %d\n", ret);

        // End of stream
        if (ret == 1)
            return false;

        // Picture ready
        if (ret == 0)
        {
            if (jm_get_y() != NULL)
                break;
            else
                continue;
        }
        // ret == 2 → no picture yet
        // continue decoding
    }

    uint8_t* srcY = jm_get_y();

    uint8_t* srcU = jm_get_u();

    uint8_t* srcV = jm_get_v();

    if (!srcY || !srcU || !srcV)
    {
        printf("No output picture available\n");
        return false;
    }

    int w = jm_get_width();

    int h = jm_get_height();

    int ySize  = w * h;
    int uvSize = (w / 2) * (h / 2);  // 4:2:0 only

    m_y.resize(ySize);
    m_u.resize(uvSize);
    m_v.resize(uvSize);

    memcpy(m_y.data(), srcY, ySize);
    memcpy(m_u.data(), srcU, uvSize);
    memcpy(m_v.data(), srcV, uvSize);
    jm_consume_output();
    *width  = w;
    *height = h;

    *y = m_y.data();
    *u = m_u.data();
    *v = m_v.data();

    return true;
}

/*----------------------------------------
  Close decoder
----------------------------------------*/
void AvcDecoder::close()
{
    jm_stop();
}