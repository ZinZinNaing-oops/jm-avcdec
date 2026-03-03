#include <stdio.h>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <iterator>
#include <iostream>

#include "AvcDecoder.h"
extern "C" {
#include "../../jm_wrapper.h"
}

AvcDecoder::AvcDecoder() : m_nalu_current_pos(0)
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
    clearNALUBuffer();

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

    // Feed data to NALU buffer
    if (payload && length > 0) {
        feedNALUData(payload, length);
    }

    // Append dummy start code if END_OF_AU
    if (end_of_au == 1)
    {
        uint8_t dummy[3] = {0x00, 0x00, 0x01};
        feedNALUData(dummy, 3);
    }

    // Extract and process complete NALUs
    std::vector<uint8_t> nalu;
    while (hasCompleteNALU() && getNextNALU(nalu))
    {
        // Add NALU to JM stream buffer with start code
        if (m_streamSize + nalu.size() + 4 > m_streamCapacity) {
            std::cerr << "ERROR: Stream buffer overflow\n";
            return -1;
        }

        // Write start code
        m_streamBuffer[m_streamSize++] = 0x00;
        m_streamBuffer[m_streamSize++] = 0x00;
        m_streamBuffer[m_streamSize++] = 0x01;

        // Write NALU data
        memcpy(m_streamBuffer + m_streamSize, nalu.data(), nalu.size());
        m_streamSize += nalu.size();

        // Update JM memory
        g_memory_buffer = m_streamBuffer;
        g_memory_size   = m_streamSize;

        // Decode available frames
        decodeAvailable();
    }

    return length;
}

void AvcDecoder::decodeAvailable()
{
    if (m_streamSize == 0 || m_finished)
        return;
  
    while (true)
    {
        int ret = jm_decode_one_frame();

        if (ret == 1)   // JM end-of-stream signal
        {
            m_streamSize = 0;
            m_finished = true;
            break;
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

        // Remove data actually consumed by JM
        if (g_memory_pos > 0)
        {
            memmove(m_streamBuffer,
                    m_streamBuffer + g_memory_pos,
                    m_streamSize - g_memory_pos);

            m_streamSize -= g_memory_pos;

            g_memory_pos = 0;

            g_memory_buffer = m_streamBuffer;
            g_memory_size   = m_streamSize;
        }      
    }
}

void AvcDecoder::vdec_stop()
{
    std::cout << "=== DECODER STOPPED ===\n";
    m_started = false;
    clearNALUBuffer();
    m_streamSize = 0;
    jm_stop();
}

uint8_t* AvcDecoder::vdec_get_picture(int* width, int* height)
{
    // if (m_frameQueue.empty())
    //     return nullptr;

    // Frame frame = std::move(m_frameQueue.front());
    // m_frameQueue.pop();

    // m_buffer = std::move(frame.yuv);
    
    // int w = frame.width;
    // int h = frame.height;

    // m_y.assign(m_buffer.begin(), m_buffer.begin() + w * h);
    // m_u.assign(m_buffer.begin() + w * h, m_buffer.begin() + w * h + w * h / 4);
    // m_v.assign(m_buffer.begin() + w * h + w * h / 4, m_buffer.end());

    // *width  = w;
    // *height = h;

    // *y = m_y.data();
    // *u = m_u.data();
    // *v = m_v.data();

    // return m_buffer.data();

     if (m_frameQueue.empty())
        return nullptr;

    Frame& frame = m_frameQueue.front();

    *width  = frame.width;
    *height = frame.height;

    uint8_t* ptr = frame.yuv.data();

    m_frameQueue.pop();

    return ptr;
}

// Feed data into the buffer
void AvcDecoder::feedNALUData(const uint8_t* data, size_t length) 
{
    m_nalu_buffer.insert(m_nalu_buffer.end(), data, data + length);
}
    
// Find and extract next complete NALU
bool AvcDecoder::getNextNALU(std::vector<uint8_t>& nalu)
{
    nalu.clear();

    if (m_nalu_current_pos >= m_nalu_buffer.size())
        return false;

    // Find start code (00 00 00 01 or 00 00 01)
    size_t start_pos = findStartCode(m_nalu_current_pos);
    if (start_pos == std::string::npos)
        return false;

    // Skip start code
    size_t nalu_start = start_pos;
    if (start_pos + 3 < m_nalu_buffer.size() &&
        m_nalu_buffer[start_pos] == 0 && m_nalu_buffer[start_pos+1] == 0 && 
        m_nalu_buffer[start_pos+2] == 0 && m_nalu_buffer[start_pos+3] == 1)
    {
        nalu_start = start_pos + 4;  // Skip 00 00 00 01
    }
    else if (start_pos + 2 < m_nalu_buffer.size() &&
             m_nalu_buffer[start_pos] == 0 && m_nalu_buffer[start_pos+1] == 0 && 
             m_nalu_buffer[start_pos+2] == 1)
    {
        nalu_start = start_pos + 3;  // Skip 00 00 01
    }

    // Find next start code
    size_t next_start = findStartCode(nalu_start);
    if (next_start == std::string::npos)
        return false;

    // Extract NALU data (without start code)
    nalu.assign(m_nalu_buffer.begin() + nalu_start, m_nalu_buffer.begin() + next_start);
    m_nalu_current_pos = next_start;

    return !nalu.empty();
}
    
// Check if we have a complete NALU
bool AvcDecoder::hasCompleteNALU() 
{
    size_t start_pos = findStartCode(m_nalu_current_pos);
    if (start_pos == std::string::npos)
        return false;

    size_t nalu_start = (start_pos + 3 < m_nalu_buffer.size() && 
                        m_nalu_buffer[start_pos+3] == 1) ? start_pos + 4 : start_pos + 3;
    size_t next_start = findStartCode(nalu_start);

    return next_start != std::string::npos;
}

size_t AvcDecoder::findStartCode(size_t from_pos) 
{
    for (size_t i = from_pos; i + 2 < m_nalu_buffer.size(); i++)
    {
        // Check for 00 00 00 01
        if (m_nalu_buffer[i] == 0 && m_nalu_buffer[i+1] == 0 && 
            m_nalu_buffer[i+2] == 0 && i + 3 < m_nalu_buffer.size() &&
            m_nalu_buffer[i+3] == 1)
        {
            return i;
        }
        // Check for 00 00 01
        if (m_nalu_buffer[i] == 0 && m_nalu_buffer[i+1] == 0 && 
            m_nalu_buffer[i+2] == 1)
        {
            return i;
        }
    }
    return std::string::npos;   
}

void AvcDecoder::clearNALUBuffer()
{
    m_nalu_buffer.clear();
    m_nalu_current_pos = 0;
}