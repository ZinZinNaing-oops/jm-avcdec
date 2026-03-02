#ifndef JM_WRAPPER_H
#define JM_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

// forward declare instead of including global.h
typedef struct decoder_params DecoderParams;

extern unsigned char* g_memory_buffer;
extern int g_memory_size;
extern int g_memory_pos;

int jm_start(const char* bitstream_file);
void jm_update_buffer(unsigned char* data, int size);
int jm_start_from_memory(unsigned char* data, int size);
int jm_decode_one_frame(void);
void jm_stop(void);
void jm_consume_output(void);
int jm_get_width(void);
int jm_get_height(void);


unsigned char* jm_get_y(void);
unsigned char* jm_get_u(void);
unsigned char* jm_get_v(void);

#ifdef __cplusplus
}
#endif

#endif