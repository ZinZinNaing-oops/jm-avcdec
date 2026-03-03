#include <stdio.h>
#include <cstdlib>
#include <string.h>
#include <iostream>
#include <vector>

#include "AvcDecoder.h"
extern "C" {
#include "../../jm_wrapper.h"
}

AvcDecoder::AvcDecoder() : m_last_memory_pos(0), m_eos_signaled(false)
{
    m_streamCapacity = 100 * 1024 * 1024;
    m_streamBuffer = new uint8_t[m_streamCapacity];
    m_streamSize = 0;
    m_started = false;
}

AvcDecoder::~AvcDecoder()
{
    if (m_started)
        jm_stop();
    delete[] m_streamBuffer;
}

bool AvcDecoder::vdec_start(UInt16 PLAY_MODE, UInt16 POST_PROCESS)
{
    std::cout << "=== DECODER STARTED ===\n";
    if (m_started)
        return false;

    m_streamSize = 0;
    m_last_memory_pos = 0;
    m_eos_signaled = false;
    m_finished = false;

    g_memory_buffer = m_streamBuffer;
    g_memory_size   = 0;
    g_memory_pos    = 0;

    uint8_t dummy = 0;
    if (jm_start_from_memory(&dummy, 0) != 0)
        return false;

    m_started = true;
    return true;
}

void AvcDecoder::vdec_stop()
{
    std::cout << "\n=== DECODER STOPPING ===\n";
    if (!m_started)
        return;

    // Flush all remaining frames from DPB
    if (!m_finished)
    {
        while (true)
        {
            int ret = jm_decode_one_frame();
            
            if (ret == 1)
            {
                m_finished = true;
                break;
            }
            
            if (ret == 2)
                break;
            
            if (ret != 0)
                break;

            // Capture frame immediately during flushing
            captureDecodedFrame();
        }
    }

    jm_stop();
    m_started = false;
    std::cout << "=== DECODER STOPPED ===\n";
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

    if (m_finished)
        return 0;

    // Append data
    if (payload && length > 0)
    {
        if (m_streamSize + length > m_streamCapacity)
        {
            std::cerr << "ERROR: Stream buffer overflow\n";
            return -1;
        }

        memcpy(m_streamBuffer + m_streamSize, payload, length);
        m_streamSize += length;

        printf("[Feed] Added %u bytes → Total: %zu bytes\n", length, m_streamSize);
    }

    // Update JM's buffer view
    g_memory_buffer = m_streamBuffer;
    g_memory_size = m_streamSize;

    // Try to decode frames
    decodeAvailable();

    // Handle EOS signal
    if (end_of_au == 1 && !m_eos_signaled)
    {
        printf("\n[EOS] End of stream signaled\n");
        m_eos_signaled = true;
        
        // Flush on EOS
        while (true)
        {
            int ret = jm_decode_one_frame();
            
            if (ret == 1)
            {
                m_finished = true;
                break;
            }
            
            if (ret == 2)
                break;
            
            if (ret != 0)
                break;

            captureDecodedFrame();
        }
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

        if (ret == 1)
        {
            m_finished = true;
            break;
        }

        if (ret == 2)
        {
            printf("[Decode] Need more data\n");
            break;
        }

        if (ret != 0)
        {
            printf("[Decode] Error: ret=%d\n", ret);
            break;
        }

        // Successfully decoded
        captureDecodedFrame();
        m_last_memory_pos = g_memory_pos;
    }

    // Conservative buffer compaction
    if (g_memory_pos > 5*1024*1024)
    {
        printf("[Buffer] Compacting: consumed %d bytes\n", g_memory_pos);
        
        memmove(m_streamBuffer, m_streamBuffer + g_memory_pos, m_streamSize - g_memory_pos);
        m_streamSize -= g_memory_pos;
        g_memory_pos = 0;
        g_memory_buffer = m_streamBuffer;
        g_memory_size = m_streamSize;
    }
}

void AvcDecoder::captureDecodedFrame()
{
    // Get frame dimensions FIRST
    int width  = jm_get_width();
    int height = jm_get_height();

    // Skip if no valid dimensions
    if (width == 0 || height == 0)
    {
        printf("[Capture] Invalid dimensions (%dx%d)\n", width, height);
        jm_consume_output();
        return;
    }

    // Now get frame data
    unsigned char* y = jm_get_y();
    unsigned char* u = jm_get_u();
    unsigned char* v = jm_get_v();

    if (!y || !u || !v)
    {
        printf("[Capture] No valid frame data (y=%p, u=%p, v=%p)\n", y, u, v);
        jm_consume_output();
        return;
    }

    int ySize  = width * height;
    int uvSize = ySize / 4;

    // Create and queue frame
    Frame frame;
    frame.width  = width;
    frame.height = height;
    frame.yuv.resize(ySize + 2 * uvSize);

    memcpy(frame.yuv.data(), y, ySize);
    memcpy(frame.yuv.data() + ySize, u, uvSize);
    memcpy(frame.yuv.data() + ySize + uvSize, v, uvSize);

    m_frameQueue.push(std::move(frame));
    printf("[Captured] ✓ Frame %dx%d queued (queue size: %zu)\n", width, height, m_frameQueue.size());

    jm_consume_output();
}

uint8_t* AvcDecoder::vdec_get_picture(int* width, int* height)
{
    if (!width || !height)
        return nullptr;

    if (m_frameQueue.empty())
        return nullptr;

    Frame frame = std::move(m_frameQueue.front());
    m_frameQueue.pop();

    m_buffer = std::move(frame.yuv);

    *width  = frame.width;
    *height = frame.height;

    return m_buffer.data();
}