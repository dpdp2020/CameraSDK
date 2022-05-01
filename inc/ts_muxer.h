//
// Created by hyt on 8/2/16.
//

#ifndef TS_MUXER_H
#define TS_MUXER_H

#include <stddef.h>
#include <stdint.h>

//#define USE_WSRT_LIBRARY

//#ifdef USE_WSRT_LIBRARY
//#include "wsrt_api.h"
//#endif


#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        AV_STREAM_TYPE_VIDEO,
        AV_STREAM_TYPE_AUDIO,
        AV_STREAM_NO_VIDEO,
        AV_STREAM_NO_AUDIO
    }av_stream_type_t;

//#ifndef USE_WSRT_LIBRARY

typedef enum {
    E_STREAM_TYPE_HEADER,
    E_STREAM_TYPE_VIDEO,
    E_STREAM_TYPE_AUDIO
}E_STREAM_TYPE;


typedef enum {
	AV_STREAM_CODEC_H264,
	AV_STREAM_CODEC_AAC_WITH_ADTS,
	AV_STREAM_CODEC_AAC,
	AV_STREAM_CODEC_H265,
	AV_STREAM_NO_CODEC
}av_stream_codec_t;

//#endif


typedef struct {
    av_stream_type_t type;
    av_stream_codec_t codec;
    uint8_t  audio_object_type;
    uint32_t audio_sample_rate;
    uint8_t  audio_channel_count;  // https://wiki.multimedia.cx/index.php?title=MPEG-4_Audio#Channel_Configurations
}av_stream_t;

#define AV_PACKET_FLAGS_KEY  0x01


/**
 *  * Undefined timestamp value
 *   *
 *    * Usually reported by demuxer that work on containers that do not provide
 *     * either pts or dts.
 *      */

#define   NOPTS_VALUE          ((int64_t)-1)

#define   TS_TIME_BASE        ((int64_t)90000)

#define  MAX_STREAM_NUM    4


typedef struct 
{
    int av_stream_index;
    uint8_t   flags;  // flag: bit8 for is_sync
    int64_t  pts;   // in 90khz
    int64_t  dts;   // in 90khz, -1 if not present
    uint8_t* data;  // for h264, NALU starts with 0x00000001
    size_t  size;
}av_packet_t;


typedef struct _ts_muxer ts_muxer_t;

typedef int (*avio_write_func)(void* avio_context, const uint8_t* buf, size_t size, E_STREAM_TYPE type);
typedef int (*avio_write_func_v218)(void* avio_context, const uint8_t* buf, size_t size, E_STREAM_TYPE type);
typedef int (*avio_send_func)(E_STREAM_TYPE type);

ts_muxer_t* new_ts_muxer(av_stream_t* av_streams, int av_stream_count);
void free_ts_muxer(ts_muxer_t* ts_muxer);

// to unset give NULL
int ts_muxer_set_avio_context(ts_muxer_t* ts_muxer, void* avio_context, avio_write_func write);
int ts_muxer_set_avio_context_v218(ts_muxer_t* ts_muxer, void* avio_context, avio_write_func write, avio_send_func avio_send);

// ask to write PAT PMT
int ts_muxer_write_header(ts_muxer_t* ts_muxer, E_STREAM_TYPE type);
int ts_muxer_write_packet(ts_muxer_t* ts_muxer, av_packet_t* av_packet);


#ifdef __cplusplus
}
#endif

#endif //FFMPEG_IVR_TS_MUXER_H_H
