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

NALUParser :: NALUParser() : current_pos(0) {}

// Feed data into the buffer
void NALUParser:: feed_data(const uint8_t* data, size_t length) {
        buffer.insert(buffer.end(), data, data + length);
    }
    
    // Find and extract next complete NALU
    bool NALUParser::get_next_nalu(std::vector<uint8_t>& nalu) {
        nalu.clear();
        
        if (current_pos >= buffer.size()) {
            return false;
        }
        
        // Find start code (00 00 00 01 or 00 00 01)
        size_t start_pos = find_start_code(current_pos);
        if (start_pos == std::string::npos) {
            return false;
        }
        
        // Skip start code
        size_t nalu_start = start_pos;
        if (buffer[start_pos] == 0 && buffer[start_pos+1] == 0 && 
            buffer[start_pos+2] == 0 && buffer[start_pos+3] == 1) {
            nalu_start = start_pos + 4;  // Skip 00 00 00 01
        } else if (buffer[start_pos] == 0 && buffer[start_pos+1] == 0 && 
                   buffer[start_pos+2] == 1) {
            nalu_start = start_pos + 3;  // Skip 00 00 01
        }
        
        // Find next start code
        size_t next_start = find_start_code(nalu_start);
        if (next_start == std::string::npos) {
            // No more NALUs, might be incomplete
            return false;
        }
        
        // Extract NALU data (without start code)
        nalu.assign(buffer.begin() + nalu_start, buffer.begin() + next_start);
        current_pos = next_start;
        
        return !nalu.empty();
    }
    
    // Check if we have a complete NALU
    bool NALUParser::has_complete_nalu() {
        size_t start_pos = find_start_code(current_pos);
        if (start_pos == std::string::npos) {
            return false;
        }
        
        size_t nalu_start = (buffer[start_pos+3] == 1) ? start_pos + 4 : start_pos + 3;
        size_t next_start = find_start_code(nalu_start);
        
        return next_start != std::string::npos;
    }
size_t NALUParser:: find_start_code(size_t from_pos) {
        for (size_t i = from_pos; i + 3 < buffer.size(); i++) {
            // Check for 00 00 00 01
            if (buffer[i] == 0 && buffer[i+1] == 0 && 
                buffer[i+2] == 0 && buffer[i+3] == 1) {
                return i;
            }
            // Check for 00 00 01
            if (i + 2 < buffer.size() && 
                buffer[i] == 0 && buffer[i+1] == 0 && buffer[i+2] == 1) {
                return i;
            }
        }
        return std::string::npos;
    }

void NALUParser::NALUParserClear() {
        buffer.clear();
        current_pos = 0;
    }