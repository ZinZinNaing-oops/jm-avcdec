#include <stdio.h>
#include <string.h>

#include "global.h"
#include "annexb.h"
#include "mbuffer.h"
#include "h264decoder.h"
#include "configfile.h"

// forward declaration
extern int Configure(InputParameters *p_Inp, int ac, char *av[]);
int decode_one_frame(DecoderParams *pDecoder); 

static InputParameters inputParams;
DecodedPicList *pDecPicList = NULL;
static int decoder_opened = 0;

int jm_start(const char* bitstream_file)
{
    char *argv[3];
    int argc = 3;

    argv[0] = "decoder";
    argv[1] = "-i";

    if (bitstream_file != NULL)
        argv[2] = (char*)bitstream_file;
    else
        argv[2] = "../ip_only.264";   // not used in memory mode

    Configure(&inputParams, argc, argv);

    if (OpenDecoder(&inputParams) != DEC_OPEN_NOERR)
        return -1;

    decoder_opened = 1;
    return 0;
}

void jm_update_buffer(unsigned char* data, int size)
{
    g_memory_buffer = data;
    g_memory_size = size;
    g_memory_pos = 0;
}

int jm_start_from_memory(unsigned char* data, int size)
{
    printf("JM MEMORY MODE STARTED\n");
    g_memory_buffer = data;
    g_memory_size = size;
    g_memory_pos = 0;

    char *argv[1];
    int argc = 1;
    argv[0] = "decoder";

    Configure(&inputParams, argc, argv);

    if (OpenDecoder(&inputParams) != DEC_OPEN_NOERR)
        return -1;

    decoder_opened = 1;

    // OVERRIDE FILE MODE
    ANNEXB_t *annex_b = p_Dec->p_Vid->annex_b;

    if (annex_b->BitStreamFile != -1)
    {
        close(annex_b->BitStreamFile);
        annex_b->BitStreamFile = -1;
    }

    annex_b->bytesinbuffer = 0;
    annex_b->is_eof = FALSE;
    return 0;
}

int jm_decode_one_frame(void)
{
    int ret = decode_one_frame(p_Dec);

    if (ret == 1)
        return 1;

    if (p_Dec->p_Vid->pDecOuputPic)
    {
        pDecPicList = p_Dec->p_Vid->pDecOuputPic;
        return 0;
    }

    return 2;
}

void jm_stop(void)
{
    if (!decoder_opened)
        return;

    FinitDecoder(&pDecPicList);
    CloseDecoder();

    decoder_opened = 0;
}

void jm_consume_output(void)
{
    pDecPicList = NULL; 
}

int jm_get_width(void)
{
    if (!pDecPicList)
        return 0;

    return pDecPicList->iWidth;
}

int jm_get_height(void)
{
    if (!pDecPicList)
        return 0;

    return pDecPicList->iHeight;
}

unsigned char* jm_get_y(void)
{
    if (!pDecPicList)
        return NULL;

    return pDecPicList->pY;
}

unsigned char* jm_get_u(void)
{
    if (!pDecPicList)
        return NULL;

    return pDecPicList->pU;
}

unsigned char* jm_get_v(void)
{
    if (!pDecPicList)
        return NULL;

    return pDecPicList->pV;
}
