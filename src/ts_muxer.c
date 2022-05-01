﻿#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "utils.h"
#include "ts_muxer.h"


// TS payload type
#define TS_MUXER_PAYLOAD_PAT          0
#define TS_MUXER_PAYLOAD_PMT          1
#define TS_MUXER_PAYLOAD_EMPTY_PES    2
#define TS_MUXER_PAYLOAD_H264_PES     10
#define TS_MUXER_PAYLOAD_AAC_PES      20
#define TS_MUXER_PAYLOAD_H265_PES     15

#define TS_MUXER_TX_PACKET_SIZE  188


#define TS_PTS_MAX_DELAY    63000  /*1/90K, 0.7sec*/

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define H264_NAL_TYPE_AUD   9

typedef struct {
    uint8_t                len;
    uint16_t             pid;
    uint8_t                payload_unit_start_indicator;
    uint8_t                continuity_count;

    int64_t             pcr;  // 90kHz time
    uint8_t                write_pcr;
    uint8_t                random_access_indicator;
    uint8_t                es_priority_indicator;

    uint8_t                header_stuffing_size;
    uint8_t                end_stuffing_size;

    uint8_t       buf[TS_MUXER_TX_PACKET_SIZE];

} ts_muxer_ts_packet_t;

// TS packet IEC 13818-1:2000 2.4.4.3
typedef struct {
    uint8_t                start;
    uint16_t              pid;
    size_t                size;
    size_t                remain;
    uint8_t                program_cnt;
    uint16_t             pmt_pid;
} ts_muxer_ts_pat_t;

typedef struct {
    uint8_t                start;
    uint16_t              pid;
    size_t                size;
    size_t                remain;

}ts_muxer_ts_pmt_t;

typedef struct {
    uint16_t  pid;
    uint8_t   header_ready;      // determine TS packet payload_unit_start_indicator
    uint8_t   is_IDR;  //鏄惁鏄甯?
    uint8_t   start;

    int64_t  dts;   // decoding time stamp in 90kHz unit
    int64_t  pts;   // presentation time stamp in 90kHz unit

    uint8_t   header_data[256];     // for header, PPS SPS etc
    size_t    header_len;
    uint8_t*  payload;
    size_t    payload_len;

    size_t    filled;       // number of bytes already filled in TS packets

} ts_muxer_video_pes_t;

typedef struct {
    uint16_t            pid;
    uint8_t             start;

    int64_t             pts;   // presentation time stamp in 90kHz unit

    uint8_t                header_data[256];     // PES header + ADTS header
    size_t                header_len;
    uint8_t*  payload;
    size_t    payload_len;

    size_t                filled;       // number of bytes already filled in TS packets

} ts_muxer_aac_pes_t;

// 1 byte stream type used in PMT table
#define TS_MUXER_STREAM_TYPE_H264 0x1B
#define TS_MUXER_STREAM_TYPE_AAC  0x0F
#define TS_MUXER_STREAM_TYPE_MP3  0x03
#define TS_MUXER_STREAM_TYPE_H265 0x24

typedef struct {
    int          stream_index;  // stream index of av_context_t.streams
    uint16_t             pid;   // PID of corresponding TS packet for this ES
    // 0 indicates this ES not exists
    uint8_t               stream_type;
    uint8_t               done;  // indicate whole stream is encoded in TS stream
    uint8_t               continuity_count; // for TS packet header
    ts_muxer_video_pes_t   pes;    // current PES, used to compose TS packet

} ts_muxer_video_stream_t;


typedef struct {
    int          stream_index;  // stream index of av_context_t.streams
    uint16_t             pid;   // PID of corresponding TS packet for this ES
    // 0 indicates this ES not exists
    av_stream_codec_t     stream_codec;
    uint8_t               stream_type;

    uint32_t            frame_count;              // count of AAC audio ADTS frame in one PES
    // each frame consists of one raw data block, which is 1024 samples (audio sample)
    uint8_t               done;

    uint8_t               object_type_indication;   // ObjectTypeIndication in DecoderConfigDescriptor
    uint8_t               aac_audio_object_type;    // audioObjectType from AudioSpecificConfig
    // used in ADTS fixed header
    uint8_t               aac_sampling_frequency_index;  // samplingFrequencyIndex from AudioSpecificConfig
    // used in ADTS fixed header
    uint8_t               aac_channel_config;       // channelConfiguration from AudioSpecificConfig

    uint8_t               continuity_count; // for TS packet header

    ts_muxer_aac_pes_t pes;    // current PES, used to compose TS packet

} ts_muxer_aac_stream_t;

typedef struct {
    uint16_t             pmt_pid;       // can be choose from 0x0020-0x1FFA
    uint16_t             pcr_pid;       // pid of stream which contain pcr
    ts_muxer_video_stream_t  video_stream;
    ts_muxer_aac_stream_t   audio_stream;
} ts_muxer_program_t;


struct _ts_muxer {
    av_stream_t* av_streams;
    int av_stream_count;
    void* avio_context;
    avio_write_func avio_write;
    avio_send_func avio_send;
    ts_muxer_program_t program;
    ts_muxer_ts_packet_t ts_packet;
    uint8_t pat_continuity_count;
    uint8_t pmt_continuity_count;
};

#define ts_muxer_set_32value(p, n)                                             \
    ((uint8_t *) (p))[0] = (uint8_t) ((n) >> 24);                               \
    ((uint8_t *) (p))[1] = (uint8_t) ((n) >> 16);                               \
    ((uint8_t *) (p))[2] = (uint8_t) ((n) >> 8);                                \
    ((uint8_t *) (p))[3] = (uint8_t)  (n)


uint32_t ts_muxer_crc32_table[256];
uint8_t ts_muxer_crc32_table_inited = 0;

void ts_muxer_init_crc32_table()
{
    if (ts_muxer_crc32_table_inited) {
        return;
    }

    uint32_t i, j, k;
    for( i = 0; i < 256; i++ )
    {
        k = 0;
        for( j = (i << 24) | 0x800000; j != 0x80000000; j <<= 1 )
            k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);

        ts_muxer_crc32_table[i] = k;
    }

    ts_muxer_crc32_table_inited = 1;
}

uint32_t ts_muxer_calc_crc32(uint8_t *data, uint32_t nLen)
{
    uint32_t     i;
    uint32_t     i_crc = 0xffffffff;

    for( i = 0; i < nLen; i++ ) {
        i_crc = (i_crc << 8) ^
                ts_muxer_crc32_table[((i_crc >> 24) ^ data[i]) & 0xff];
    }
    return i_crc;
}


const u_int32_t ts_muxer_aac_sample_frequencies[] = {
        96000,
        88200,
        64000,
        48000,
        44100,
        32000,
        24000,
        22050,
        16000,
        12000,
        11025,
        8000,
        7350,
        0           // mark the end
};

/* TS packet IEC 13818-1:2000 2.4.3
 *
 */
int ts_muxer_prepare_ts_packet_info(ts_muxer_ts_packet_t *packet, uint8_t payload_type, void *payload,
                                    uint16_t pcr_pid,
                                    uint8_t continuity_count)
{
    ts_muxer_ts_pat_t *pat = NULL;
    ts_muxer_ts_pmt_t *pmt = NULL;
    ts_muxer_video_pes_t *pes = NULL;
    ts_muxer_aac_pes_t  *aac_pes = NULL;
    size_t           payload_remain;
    uint8_t           is_adaptation_filed=0;

    if (!packet || !payload) {
        return -1;
    }

    memset(packet, 0, sizeof(ts_muxer_ts_packet_t));

    packet->continuity_count = continuity_count;
    packet->len = 4;  // 1 byte sync, 2 bytes indicator and PID, 1 byte control and continuity count

    if (TS_MUXER_PAYLOAD_PAT == payload_type) 
	{
        pat = (ts_muxer_ts_pat_t*)payload;
        packet->pid = 0;
        packet->payload_unit_start_indicator = pat->start;
        if ((size_t)(188-packet->len) > pat->remain)
            packet->end_stuffing_size = 188-packet->len-pat->remain;
    } 
	else if (TS_MUXER_PAYLOAD_PMT == payload_type) 
	{
        pmt = (ts_muxer_ts_pmt_t*)payload;
        packet->pid = pmt->pid;
        packet->payload_unit_start_indicator = pmt->start;
        if ((size_t)(188-packet->len) > pmt->remain)
            packet->end_stuffing_size = 188-packet->len-pmt->remain;
    } 
	else if (TS_MUXER_PAYLOAD_H264_PES == payload_type || TS_MUXER_PAYLOAD_H265_PES == payload_type) 
	{
        pes = (ts_muxer_video_pes_t*)payload;
        packet->pid = pes->pid;
        packet->payload_unit_start_indicator = pes->start;
        if (pes->is_IDR) {
            packet->es_priority_indicator = 1;
        }
        // for access unit start and pcr 
        if (pes->start && packet->pid == pcr_pid) {
            if(pes->is_IDR){
                packet->random_access_indicator = 1;
            }
            // prepare PCR
            if(pes->dts != NOPTS_VALUE){
                packet->pcr = pes->dts - TS_PTS_MAX_DELAY; /* 63000 delay*/                
            }else{
                packet->pcr = pes->pts - TS_PTS_MAX_DELAY; /* 63000 delay*/
            }
            
            packet->write_pcr = 1;
            is_adaptation_filed = 1;
            packet->len += 8;
        }
        payload_remain = pes->header_len + pes->payload_len - pes->filled;
        if (payload_remain < 0) {
            return -1;
        }
        if (payload_remain < (size_t)188 - packet->len) 
		{
            if (188 - packet->len - payload_remain >= 3) {
                packet->header_stuffing_size = 188 - packet->len - payload_remain;
                if (!is_adaptation_filed) {
                    packet->header_stuffing_size -= 2; // need to add adaptation field header
                    is_adaptation_filed = 1;
                }
                packet->len = 188 - payload_remain;
            } else {
                // fill only 1 byte stuff here, and move the rest of the remain to next packet
                if (!is_adaptation_filed) {
                    packet->len += 3; // 2 byte adaption field header + 1 byte stuffing;
                    is_adaptation_filed = 1;
                    packet->header_stuffing_size = 1;
                } else {
                    packet->len += 3;
                    packet->header_stuffing_size = 3;
                }
            }
        }
        //MY_LOG(D_VIDEO, "pts: %lld, pcr: %lld, pid: %x, isStart: %d, payload_len: %d, payload_remain: %d(video)", 
        //    pes->pts, packet->pcr, pes->pid, pes->start, pes->payload_len, payload_remain);
    } 
	else if (TS_MUXER_PAYLOAD_AAC_PES == payload_type) 
	{
        aac_pes = (ts_muxer_aac_pes_t*)payload;
        packet->pid = aac_pes->pid;
        packet->payload_unit_start_indicator = aac_pes->start;
        payload_remain = aac_pes->header_len + aac_pes->payload_len - aac_pes->filled;
        if (aac_pes->start && packet->pid == pcr_pid) {
            packet->random_access_indicator = 1;
            // prepare PCR
            packet->pcr = aac_pes->pts - TS_PTS_MAX_DELAY; /* 63000 delay*/
            //MY_LOG(D_AUDIO, "pts: %lld, pcr: %lld(aac_pes->pts - TS_PTS_MAX_DELAY: %lld)(first packet)", 
            //    aac_pes->pts, packet->pcr, aac_pes->pts - TS_PTS_MAX_DELAY);
            packet->write_pcr = 1;
            is_adaptation_filed = 1;
            packet->len += 8;
        }
        if (payload_remain < 0) {
            return -1;
        }
        if (payload_remain < (size_t)188 - packet->len) {
            if (188 - packet->len - payload_remain >= 3) {
                packet->header_stuffing_size = 188 - packet->len - payload_remain;
                if (!is_adaptation_filed) {
                    packet->header_stuffing_size -= 2; // need to add adaptation field header
                    is_adaptation_filed = 1;
                }
                packet->len = 188 - payload_remain;
            } else {
                // fill only 1 byte stuff here, and move the rest of the remain to next packet
                if (!is_adaptation_filed) {
                    packet->len += 3; // 2 byte adaption field header + 1 byte stuffing;
                    is_adaptation_filed = 1;
                    packet->header_stuffing_size = 1;
                } else {
                    packet->len += 3;
                    packet->header_stuffing_size = 3;
                }
            }
        }
        //MY_LOG(D_AUDIO, "pts: %lld, pcr: %lld, pid: %x(pcr_pid: %x), isStart: %d, payload_len: %d, payload_remain: %d(audio)",
        //    aac_pes->pts, packet->pcr, aac_pes->pid, pcr_pid, aac_pes->start, aac_pes->payload_len, payload_remain);
    } 
	else if (TS_MUXER_PAYLOAD_EMPTY_PES == payload_type) 
	{
        packet->pid = *(uint16_t*)payload;
        packet->payload_unit_start_indicator = 0;
        packet->len += 2;  // 2 byte adaption field header
        packet->header_stuffing_size = 188 - packet->len;
        packet->len = 188;
    } 
	else 
	{
        return -1;
    }

    return 0;
}



/* TS packet IEC 13818-1:2000 2.4.3
 *
 */
uint8_t* ts_muxer_enc_packet_header(ts_muxer_ts_packet_t *packet, uint8_t *buf)
{
    uint32_t    len, stuff_len;

    // buf should always be large enough for the packet content
    if (!packet || !buf) {
        return NULL;
    }

    *buf++ = 0x47;  // 1 byte sync
    // 2 bytes indicators and PID
    *buf++ = packet->pid >> 8 | (packet->payload_unit_start_indicator ? 0x40 : 0);
    *buf++ = packet->pid;
    // continuity count and adaptation field control
    if (packet->write_pcr || packet->header_stuffing_size) {
        *buf++ = packet->continuity_count | 0x30;
        //MY_LOG(D_ALL, "pid: %x, continuity_count: %d(1)", packet->pid, packet->continuity_count);
        len = 1+(packet->write_pcr?6:0)+packet->header_stuffing_size;    // adaptation_field_length = flag + PCR + stuffing
        *buf++ = len;
        *buf++ = 0 | (packet->random_access_indicator?0x40:0) | (packet->es_priority_indicator?0x20:0) |
                (packet->write_pcr?0x10:0); // 1 byte indicators
        if (packet->write_pcr) {
            // write 6bytes PCR
            *buf++ = packet->pcr >> 25;
            *buf++ = packet->pcr >> 17;
            *buf++ = packet->pcr >> 9;
            *buf++ = packet->pcr >> 1;
            *buf++ = ((packet->pcr & 1) << 7) | 0x7E;
            *buf++ = 0;
        }
        // stuff
        stuff_len = packet->header_stuffing_size;
        while (stuff_len > 0) {
            *buf++ = 0xFF;
            stuff_len--;
        }
    } else {
        *buf++ = packet->continuity_count | 0x10;
        //MY_LOG(D_ALL, "pid: %x, continuity_count: %d(2)", packet->pid, packet->continuity_count);
    }

    return buf;
}


/* TS packet IEC 13818-1:2000 2.4.4.3
 *
 */
int ts_muxer_enc_psi(struct _ts_muxer *ts, E_STREAM_TYPE type)
{
    ts_muxer_ts_packet_t   *packet;
    uint8_t                *buf;
    uint32_t              crc32;
    ts_muxer_ts_pat_t     pat;
    ts_muxer_ts_pmt_t     pmt;
//    int i;

    if (NULL == ts) {
        return -1;
    }

    packet = &ts->ts_packet;

    // PAT
    memset(packet->buf, 0, TS_MUXER_TX_PACKET_SIZE);
    buf = packet->buf;
    pat.start = 1;
    pat.pid = 0;
    pat.pmt_pid = ts->program.pmt_pid;
    pat.size = pat.remain = 17;   // because we have only one program, so we know how largs PAT should be

    if (0 != ts_muxer_prepare_ts_packet_info(packet, TS_MUXER_PAYLOAD_PAT, &pat, 
                                             ts->program.pcr_pid, ts->pat_continuity_count)) {
        return -1;
    }

    ts->pat_continuity_count++;
    if (ts->pat_continuity_count > 0x0F) {
        ts->pat_continuity_count = 0;
    }

    buf = ts_muxer_enc_packet_header(packet, buf);

    *buf++ = 0x00; // pointer
    *buf++ = 0x00; // table ID: 0x00 for program association section
    // 12 bit length 0x00D = 13
    *buf++ = 0xB0;
    *buf++ = 0x0D;
    // 16 bit user defined transport_stream_id
    *buf++ = 0x00;
    *buf++ = 0x00;
    // 2 bit reserved 1, 5 bit version 0, 1 bit current_next_indicator 1
    *buf++ = 0xC1;
    *buf++ = 0; // 1 byte section number
    *buf++ = 0; // 1 byte current section number
    // 2 byte program number, we have 1 only
    *buf++ = 0;
    *buf++ = 1;
    // 3 bit reserved 1, 13 bit PMT PID
    *buf++ = (pat.pmt_pid) >> 8 | 0xE0;
    *buf++ = pat.pmt_pid;
    // CRC32
    crc32 = ts_muxer_calc_crc32(buf - 12, 12);
    ts_muxer_set_32value(buf, crc32);
    buf += 4;
    // all stuffing afterwards
    while (buf < packet->buf+TS_MUXER_TX_PACKET_SIZE) {
        *buf++ = 0xFF;
    }

    //pthread_mutex_lock(&g_ts_mutex);
    if (ts->avio_write(ts->avio_context, packet->buf, TS_MUXER_TX_PACKET_SIZE, type) < 0) {
        MY_LOG(D_VIDEO, "Failed to write PAT packet to avio context");
    }
    //pthread_mutex_unlock(&g_ts_mutex);

    // PMT
    memset(packet->buf, 0, TS_MUXER_TX_PACKET_SIZE);
    buf = packet->buf;
    pmt.start = 1;
    pmt.pid = ts->program.pmt_pid;
    pmt.size = pmt.remain = 27; // we know the exact size because we are sure we will have at most 2 streams

    if (0 != ts_muxer_prepare_ts_packet_info(packet, TS_MUXER_PAYLOAD_PMT, &pmt, 
                                             ts->program.pcr_pid, ts->pmt_continuity_count)) {
        return -1;
    }

    ts->pmt_continuity_count++;
    if (ts->pmt_continuity_count > 0x0F) {
        ts->pmt_continuity_count = 0;
    }

    buf = ts_muxer_enc_packet_header(packet, buf);

    // encoding PMT
    *buf++ = 0x00; // pointer
    *buf++ = 0x02; // table ID: 0x00 for TS_program_map_section
    // 12 bit length 0x00D = 13
    *buf++ = 0xB0;
    if (0 == ts->program.video_stream.stream_type && 0 == ts->program.audio_stream.stream_type) {
        return -1;
    } else if (0 == ts->program.video_stream.stream_type || 0 == ts->program.audio_stream.stream_type) {
        *buf++ = 0x12;
    } else {
        *buf++ = 0x17;
    }
    // 2 bytes program number
    *buf++ = 0x00;
    *buf++ = 0x01;
    // 2 bit reserved 1, 5 bit version 0, 1 bit current_next_indicator 1
    *buf++ = 0xC1;
    *buf++ = 0; // 1 byte section number
    *buf++ = 0; // 1 byte current section number
    // 3 bit reserved and 13 bit of PCR_PID (usually the video stream PID)

    *buf++ = 0xE0 | (ts->program.pcr_pid >> 8);
    *buf++ = ts->program.pcr_pid;

    // 4 bit reserved 1, 12 bit program_info_length
    // don't known what this descriptor is, put 0 here
    *buf++ = 0xF0;
    *buf++ = 0;
    // h264 stream, 1 byte stream type
    if (0 != ts->program.video_stream.stream_type) {
        *buf++ = ts->program.video_stream.stream_type;
        *buf++ = 0xE0 | (ts->program.video_stream.pid >> 8);
        *buf++ = ts->program.video_stream.pid;
        *buf++ = 0xF0;   // don't known what descriptor is, just ignore it.
        *buf++ = 0x00;
    }
    // audio stream
    if (0 != ts->program.audio_stream.stream_type) {
        *buf++ = ts->program.audio_stream.stream_type;
        *buf++ = 0xE0 | (ts->program.audio_stream.pid >> 8);
        *buf++ = ts->program.audio_stream.pid;
        *buf++ = 0xF0;   // don't known what descriptor is, just ignore it.
        *buf++ = 0x00;
    }
    // CRC32
    if (0 == ts->program.video_stream.stream_type || 0 == ts->program.audio_stream.stream_type) {
        crc32 = ts_muxer_calc_crc32(buf - 17, 17);
    } else {
        crc32 = ts_muxer_calc_crc32(buf - 22, 22);
    }
    ts_muxer_set_32value(buf, crc32);
    buf += 4;
    // all stuffing afterwards
    while (buf <= packet->buf+TS_MUXER_TX_PACKET_SIZE) {
        *buf++ = 0xFF;
    }
/*    
    fprintf(stderr, "dump ts header(size:%d)\n", TS_MUXER_TX_PACKET_SIZE);

    for(i = 0;i<TS_MUXER_TX_PACKET_SIZE;i++){
        fprintf(stderr, "%02hhx ",packet->buf[i]);
    }

    fprintf(stderr, "\n");
*/
    //pthread_mutex_lock(&g_ts_mutex);
    if (ts->avio_write(ts->avio_context, packet->buf, TS_MUXER_TX_PACKET_SIZE, type) < 0) {
        MY_LOG(D_VIDEO, "Failed to write PMT packet to avio context");
    }
    //pthread_mutex_unlock(&g_ts_mutex);

    if (type == E_STREAM_TYPE_HEADER && ts->avio_send != NULL)
        ts->avio_send(type);
    return 0;
}


/* PES IEC 13818-1 2.4.3.7
 *
 */
int ts_muxer_prepare_video_pes(ts_muxer_video_stream_t *stream, ts_muxer_video_pes_t *pes, av_packet_t *av_packet)
{
    uint8_t    *buf;

    if (NULL == pes || NULL == stream || NULL == av_packet) 
	{
        return -1;
    }

    memset(pes, 0, sizeof(ts_muxer_video_pes_t));
    pes->start = 1;
    pes->is_IDR = av_packet->flags & AV_PACKET_FLAGS_KEY; 
    pes->pid = stream->pid;
    pes->dts = av_packet->dts;
    if(pes->dts != NOPTS_VALUE){
        pes->dts += TS_PTS_MAX_DELAY;
    }
    pes->pts = av_packet->pts + TS_PTS_MAX_DELAY; //寤惰繜鏃堕棿榛樿涓?.7s
    pes->payload = av_packet->data;  //鏁版嵁
    pes->payload_len = av_packet->size;  //鏁版嵁闀垮害
    pes->filled = 0;
    
    //MY_LOG(D_VIDEO, "pts: %lld, payload_len: %d(video)", pes->pts, pes->payload_len);


    // PES header
    buf = pes->header_data;  
    // 3 bytes packet_start_code_prefix
    *buf++ = 0x00;
    *buf++ = 0x00;
    *buf++ = 0x01;
    *buf++ = 0xE0; // 1 byte stream_id
    // 2 byte PES length, let's make it 0 for simplicity
    *buf++ = 0x00;
    *buf++ = 0x00;
    *buf++ = 0x80;
    if (pes->dts == NOPTS_VALUE) 
	{
        // no DTS
        *buf++ = 0x80;
        *buf++ = 0x05;  // 1 byte PES_header_data_length


        // 5 bytes PTS        
        *buf++ = 0x21 | ((pes->pts >> 29) & 0x0E);
        *buf++ = pes->pts >> 22;
        *buf++ = (pes->pts >> 14) | 0x01;
        *buf++ = pes->pts >> 7;
        *buf++ = (pes->pts << 1) | 0x01;   

    } 
	else 
	{
        // PTS and DTS
        *buf++ = 0xC0;
        *buf++ = 0x0A;  // 1 byte PES_header_data_length
        
        // 5 bytes PTS        
        *buf++ = 0x31 | ((pes->pts >> 29) & 0x0E);
        *buf++ = pes->pts >> 22;
        *buf++ = (pes->pts >> 14) | 0x01;
        *buf++ = pes->pts >> 7;
        *buf++ = (pes->pts << 1) | 0x01;   
        
        //5 byte DTS
        *buf++ = 0x11 | ((pes->dts >> 29) & 0x0E);
        *buf++ = pes->dts >> 22;
        *buf++ = (pes->dts >> 14) | 0x01;
        *buf++ = pes->dts >> 7;
        *buf++ = (pes->dts << 1) | 0x01;     

    }

    // PES body
    if(av_packet->size >= 5 && av_packet->data[0] == 0 &&
       av_packet->data[1] == 0 && av_packet->data[2] == 0 &&
       av_packet->data[3] == 1 && (av_packet->data[4] & 0x1F) != H264_NAL_TYPE_AUD)
    {
        //MY_LOG(D_VIDEO, "H264_NAL_TYPE_AUD =%x", av_packet->data[4]);
        // AU delimiter
        ts_muxer_set_32value(buf, 1);
        buf += 4;
        *buf++ = 0x09;
        *buf++ = 0xf0;

        //*buf++ = 0x46;
        //*buf++ = 0x01;
        //*buf++ = 0x50;
    }
    
//    if (pes->is_IDR) {
//        *buf++ = 0x10;
//    } else {
//        *buf++ = 0x30;
//    }
    

    // let's add synchronization byte sequence before VCL NALU
    //ts_muxer_set_32value(buf, 1);
    //buf += 4;

    pes->header_len = buf - pes->header_data;

    return 0;
}


int ts_muxer_enc_video_packet(ts_muxer_t* ts, ts_muxer_video_stream_t* video_stream, av_packet_t* av_packet) 
{

    ts_muxer_video_pes_t* pes;
    ts_muxer_ts_packet_t* ts_packet;
    uint8_t* pos;
    size_t len;

    if (NULL == ts || NULL == video_stream || NULL == av_packet) 
	{
        return -1;
    }
    pes = &video_stream->pes;
    ts_packet = &ts->ts_packet;

    if (0 != ts_muxer_prepare_video_pes(video_stream, pes, av_packet)) 
	{
        return -1;
    }

    //pthread_mutex_lock(&g_ts_mutex);
    int ts_count = 0;
    while (pes->header_len + pes->payload_len > pes->filled && pes->header_len != 0)  //灏嗗垎鐗囧瓨鍏ュ寘
	{

        // prepare av_packet
        if (0 != ts_muxer_prepare_ts_packet_info(ts_packet, video_stream->stream_type == TS_MUXER_STREAM_TYPE_H265?TS_MUXER_PAYLOAD_H265_PES:TS_MUXER_PAYLOAD_H264_PES, pes, 
                                                 ts->program.pcr_pid, video_stream->continuity_count)) 
        {
            MY_LOG(D_VIDEO, "Failed to prepare TS av_packet for H264 frame\n");
            //pthread_mutex_unlock(&g_ts_mutex);
            return -1;
        }

        // encode TS av_packet
        pos = ts_muxer_enc_packet_header(ts_packet, ts_packet->buf);
        if (NULL == pos) {
            MY_LOG(D_VIDEO, "Failed to encode TS av_packet for H.264 PES\n");
            //pthread_mutex_unlock(&g_ts_mutex);
            return -1;
        }
        //MY_LOG(D_VIDEO, "pos: %x, ts_packet->buf: %x(video)", pos, ts_packet->buf);

        // fill PES into TS av_packet
        pes->start = 0;
        if (pes->filled < pes->header_len) 
		{
            // fill PES header part
            len = MIN(pes->header_len - pes->filled, TS_MUXER_TX_PACKET_SIZE+ts_packet->buf-pos);
            memcpy(pos, pes->header_data+pes->filled, len);
            pos += len;
            pes->filled += len;
            //MY_LOG(D_VIDEO, "len: %d(video0)", len);
        }

        if (pes->filled >= pes->header_len) 
		{
            // fill h264 NALU
            len = MIN(pes->payload_len+pes->header_len-pes->filled, TS_MUXER_TX_PACKET_SIZE+ts_packet->buf-pos);
            if (len) {
                memcpy(pos, pes->payload+(pes->filled-pes->header_len), len);
                pos += len;
                pes->filled += len;
                //MY_LOG(D_VIDEO, "len: %d(video1)", len);
            }
        }

        // there should not be end stuffing for PES TS av_packet
        video_stream->continuity_count++;
        if (video_stream->continuity_count > 0x0F) 
		{
            video_stream->continuity_count = 0;
        }
        //MY_LOG(D_VIDEO, "pid: %x, video continuity_count++: %d", video_stream->pid, video_stream->continuity_count);

       
        ts_count++;

        if (ts->avio_write(ts->avio_context, ts_packet->buf, TS_MUXER_TX_PACKET_SIZE, E_STREAM_TYPE_VIDEO) < 0)
		{
            MY_LOG(D_VIDEO, "Failed to write packet to avio context\n");
        }
    }
    //pthread_mutex_unlock(&g_ts_mutex);
    //MY_LOG(D_AUDIO, "video ts_count: %d, pes->header_len: %d,  pes->payload_len: %d\n", ts_count, pes->header_len, pes->payload_len);
    //处理完一个完整音频帧后发送
    if (ts_count > 0 && ts->avio_send != NULL) {
        if (ts->avio_send(E_STREAM_TYPE_VIDEO) < 0) {
            MY_LOG(D_VIDEO, "Failed to write h264 packet to avio context");
        }
    }


    return 0;
}


/* IEC 14496-3:3005 1.a.3.2
 * One PES for each chunk, which contains multiple samples
 * and one ADTS for each frame
 *
 * PES IEC 13818-1 2.4.3.7
 */
int ts_muxer_prepare_aac_pes(ts_muxer_aac_stream_t *stream, ts_muxer_aac_pes_t *pes, av_packet_t *av_packet)
{
    uint8_t                  *buf;
    size_t                   adts_frame_len;

    if (NULL == stream || NULL == pes || NULL == pes) {
        return -1;
    }

    if (pes->header_len + pes->payload_len != 0
        && pes->header_len + pes->payload_len > pes->filled) {
        // not all filled
        return 0;
    }

    // fill PES header
    memset(pes, 0, sizeof(ts_muxer_aac_pes_t));
    pes->start = 1;

    pes->pid = stream->pid;
    pes->pts = av_packet->pts + TS_PTS_MAX_DELAY;
    pes->payload = av_packet->data;
    pes->payload_len = av_packet->size;
    pes->filled = 0;
    //MY_LOG(D_AUDIO, "pts: %lld, payload_len: %d(audio)", pes->pts, pes->payload_len);

    // PES header
    buf = pes->header_data;
    // 3 bytes packet_start_code_prefix
    *buf++ = 0x00;
    *buf++ = 0x00;
    *buf++ = 0x01;
    *buf++ = 0xC0; // 1 byte stream_id for MPEG audio
    // 2 byte PES length, let's leave it 0 and fiil it at the end of this function
    *buf++ = 0x00;
    *buf++ = 0x00;
    *buf++ = 0x80;
    *buf++ = 0x80;
    *buf++ = 0x05;  // 1 byte PES_header_data_length
    // 5 bytes PTS
    *buf++ = 0x21 | ((pes->pts >> 29) & 0x0E);
    *buf++ = pes->pts >> 22;
    *buf++ = (pes->pts >> 14) | 0x01;
    *buf++ = pes->pts >> 7;
    *buf++ = (pes->pts << 1) | 0x01;

    pes->header_len = 14;

    if (AV_STREAM_CODEC_AAC_WITH_ADTS == stream->stream_codec) {
        // no need to encode ADTS header
    } 
	else 
	{
        // ADTS header
        //buf = pes->adts_header_fix;
        *buf++ = 0xFF;
        if (stream->object_type_indication == 0x40) 
			{
            // MPEG-4 AAC
            *buf++ = 0xF1;
            if (stream->aac_audio_object_type <= 4 && stream->aac_audio_object_type > 0) {
                *buf = (stream->aac_audio_object_type - 1) << 6;
            } else if (stream->aac_audio_object_type > 4) {
                *buf = 3 << 6;
            } else {
                // 0 ??
                return -1;
            }
        } 
		else 
		{
            if (stream->object_type_indication == 0x69
                || stream->object_type_indication == 0x6B) {
                // mp3 (MPEG-1 layer III or MPEG-2 layer III)
                MY_LOG(D_VIDEO, "flv we don't support MP3 audio currently");
                return -1;
            } else {
                // MPEG-2 AAC
                *buf++ = 0xF9;
                if (stream->object_type_indication == 0x66) {
                    *buf = 0 << 6;
                } else if (stream->object_type_indication == 0x67) {
                    *buf = 1 << 6;
                } else if (stream->object_type_indication == 0x68) {
                    *buf = 2 << 6;
                } else {
                    // unknown
                    return -1;
                }
            }
        }
        *buf |= (stream->aac_sampling_frequency_index & 0x0F) << 2;
        if (stream->aac_channel_config > 7) 
		{
            stream->aac_channel_config = 0;
        }
        *buf |= (stream->aac_channel_config & 0x07) >> 2;
        buf++;
        *buf = (stream->aac_channel_config & 0x07) << 6;

        adts_frame_len = 7 + pes->payload_len;
		//cseg_log(CSEG_LOG_ERROR, "the payload len = %d\n",pes->payload_len);
        if (adts_frame_len >= 1 << 13) 
		{
            MY_LOG(D_VIDEO, "flv ADTS frame too large\n");
            return -1;
        }

        *buf |= (adts_frame_len >> 11) & 0x03;
        buf++;
        *buf++ = adts_frame_len >> 3;
        *buf = adts_frame_len << 5;
        *buf |= 0x1F;
        buf++;
        *buf = 0xFC;

        pes->header_len += 7;
    }

    // finally PES length
    if (pes->header_len+pes->payload_len-6+1 >= 1<<16) {
        MY_LOG(D_VIDEO, "FLV AAC audio PES too large");
        return -1;
    }
    pes->header_data[4] = (pes->header_len + pes->payload_len - 6)>>8;
    pes->header_data[5] = pes->header_len + pes->payload_len - 6;

    return 0;
}


int ts_muxer_enc_aac_packet(ts_muxer_t* ts, ts_muxer_aac_stream_t* aac_stream, av_packet_t* av_packet) {

    ts_muxer_aac_pes_t* pes;
    ts_muxer_ts_packet_t* ts_packet;
    uint8_t* pos;
    size_t len;

    if (NULL == ts || NULL == aac_stream || NULL == av_packet) {
        return -1;
    }

    pes = &aac_stream->pes;
    ts_packet = &ts->ts_packet;

    if (0 != ts_muxer_prepare_aac_pes(aac_stream, pes, av_packet)) {
        return -1;
    }

    //pthread_mutex_lock(&g_ts_mutex);
    //uint8_t* pdata = (uint8_t*)malloc(10 * TS_MUXER_TX_PACKET_SIZE);
    int ts_count = 0;
    while (pes->header_len + pes->payload_len > pes->filled && pes->header_len != 0) {

        // prepare packet
        if (0 != ts_muxer_prepare_ts_packet_info(ts_packet, TS_MUXER_PAYLOAD_AAC_PES, pes, 
                                                 ts->program.pcr_pid, aac_stream->continuity_count)) {
            MY_LOG(D_VIDEO, "Failed to prepare TS packet for AAC frame");
            //free(pdata);
            //pthread_mutex_unlock(&g_ts_mutex);
            return -1;
        }

        // encode TS packet
        pos = ts_muxer_enc_packet_header(ts_packet, ts_packet->buf);
        if (NULL == pos) {
            MY_LOG(D_VIDEO, "Failed to encode TS packet for AAC PES");
            //free(pdata);
            //pthread_mutex_unlock(&g_ts_mutex);
            return -1;
        }
        //MY_LOG(D_AUDIO, "pos: %x, ts_packet->buf: %x(audio)", pos, ts_packet->buf);

        // fill PES into TS packet
        pes->start = 0;
        if (pes->filled < pes->header_len) {
            // fill PES header part
            len = MIN(pes->header_len - pes->filled, TS_MUXER_TX_PACKET_SIZE+ts_packet->buf-pos);
            memcpy(pos, pes->header_data+pes->filled, len);
            pos += len;
            pes->filled += len;
            //MY_LOG(D_AUDIO, "len: %d(audio0)", len);
        }

        if (pes->filled >= pes->header_len) {
            len = MIN(pes->payload_len+pes->header_len-pes->filled, TS_MUXER_TX_PACKET_SIZE+ts_packet->buf-pos);
            if (len) {
                memcpy(pos, pes->payload+(pes->filled-pes->header_len), len);
                pos += len;
                pes->filled += len;
            }
            //MY_LOG(D_AUDIO, "len: %d(audio1)", len);
        }

        // there should not be end stuffing for PES TS packet
        aac_stream->continuity_count++;
        if (aac_stream->continuity_count > 0x0F) {
            aac_stream->continuity_count = 0;
        }
        //MY_LOG(D_AUDIO, "pid: %x, audio continuity_count++: %d", aac_stream->pid, aac_stream->continuity_count);

        //memcpy(pdata + (ts_count * TS_MUXER_TX_PACKET_SIZE), ts_packet->buf, TS_MUXER_TX_PACKET_SIZE);
        ts_count++;

        if (ts->avio_write(ts->avio_context, ts_packet->buf, TS_MUXER_TX_PACKET_SIZE, E_STREAM_TYPE_AUDIO) < 0) {
            MY_LOG(D_VIDEO, "Failed to write AAC packet to avio context");
        }
        
    }
    //pthread_mutex_unlock(&g_ts_mutex);
    //MY_LOG(D_AUDIO, "audio ts_count: %d, pes->header_len: %d,  pes->payload_len: %d\n", ts_count, pes->header_len, pes->payload_len);
    //处理完一个完整音频帧后发送
    if (ts_count > 0 && ts->avio_send != NULL) {
        if (ts->avio_send(E_STREAM_TYPE_AUDIO) < 0) {
            MY_LOG(D_VIDEO, "Failed to send AAC packet to avio context");
        }
    }

    //free(pdata);
    return 0;

}


ts_muxer_t* new_ts_muxer(av_stream_t* av_streams, int stream_count) {

    struct _ts_muxer* ts_muxer;

    if (av_streams == NULL) {
        MY_LOG(D_VIDEO, "NULL av_streams");
        return NULL;
    }

    if (stream_count == 0) {
        MY_LOG(D_VIDEO, "Stream count can not be 0");
        return NULL;
    }

    ts_muxer_init_crc32_table();

    ts_muxer = (struct _ts_muxer *)malloc(sizeof(struct _ts_muxer));
    if (ts_muxer == NULL) {
        MY_LOG(D_VIDEO, "Failed to malloc ts_muxer");
        return NULL;
    }
    memset(ts_muxer, 0, sizeof(struct _ts_muxer));

    ts_muxer->av_streams = av_streams;
    ts_muxer->av_stream_count = stream_count;
    
    MY_LOG(D_VIDEO, "stream_type=%d", ts_muxer->program.video_stream.stream_type);

    return ts_muxer;
}


void free_ts_muxer(ts_muxer_t* ts_muxer) {

    if (NULL == ts_muxer) {
        return;
    }

    free(ts_muxer);
}


int ts_muxer_set_avio_context_v218(ts_muxer_t* ts_muxer, void* avio_context, avio_write_func_v218 avio_write, avio_send_func avio_send) {
    if (NULL == ts_muxer) {
        MY_LOG(D_VIDEO, "Failed to set avio context, ts_muxer is NULL");
        return -1;
    }

    ts_muxer->avio_context = avio_context;
    ts_muxer->avio_write = avio_write;
    ts_muxer->avio_send = avio_send;

    return 0;
}

int ts_muxer_set_avio_context(ts_muxer_t* ts_muxer, void* avio_context, avio_write_func avio_write) {

    if (NULL == ts_muxer) {
        MY_LOG(D_VIDEO, "Failed to set avio context, ts_muxer is NULL");
        return -1;
    }

    ts_muxer->avio_context = avio_context;
    ts_muxer->avio_write = avio_write;
    ts_muxer->avio_send = NULL;

    return 0;
}


int ts_muxer_write_header(ts_muxer_t* ts_muxer, E_STREAM_TYPE type) {

    av_stream_t* av_stream;
    int i, j;

    if (NULL == ts_muxer || NULL == ts_muxer->av_streams || 0 == ts_muxer->av_stream_count) {
        return -1;
        
    }

    if (NULL == ts_muxer->avio_context || NULL == ts_muxer->avio_write) {
        return 0;
    }

    // find first video and audio stream
    for (i=0; i<ts_muxer->av_stream_count; i++) {
        av_stream = &ts_muxer->av_streams[i];
/*        
        fprintf(stderr, "av_stream->type:%d, av_stream->codec:%d, test--------------------------\n", 
                (int)av_stream->type, (int)av_stream->codec);            
*/
        if (AV_STREAM_TYPE_VIDEO == av_stream->type && (AV_STREAM_CODEC_H265 == av_stream->codec || AV_STREAM_CODEC_H264 == av_stream->codec)) 
		{

            // found H264/HEVC stream
            if (0 == ts_muxer->program.video_stream.pid) 
			{
                ts_muxer->program.pmt_pid = 0x0FF0;
                ts_muxer->program.video_stream.stream_index = i;
                ts_muxer->program.video_stream.pid = 0x1000;
                ts_muxer->program.audio_stream.stream_codec = av_stream->codec;
				if (AV_STREAM_CODEC_H265 == av_stream->codec)
				{
	                ts_muxer->program.video_stream.stream_type = TS_MUXER_STREAM_TYPE_H265;  // for H.265
				}
				else
				{
	                ts_muxer->program.video_stream.stream_type = TS_MUXER_STREAM_TYPE_H264;  // for H.264
				}

            }
        } 
		else if (AV_STREAM_TYPE_AUDIO == av_stream->type
                   && (AV_STREAM_CODEC_AAC == av_stream->codec || AV_STREAM_CODEC_AAC_WITH_ADTS == av_stream->codec)) 
        {

            if (0 == ts_muxer->program.audio_stream.pid) {
                ts_muxer->program.pmt_pid = 0x0FF0;
                ts_muxer->program.audio_stream.stream_index = i;
                ts_muxer->program.audio_stream.pid = 0x1001;
                ts_muxer->program.audio_stream.stream_codec = av_stream->codec;
                ts_muxer->program.audio_stream.stream_type = TS_MUXER_STREAM_TYPE_AAC;
                ts_muxer->program.audio_stream.object_type_indication = 0x40;
                // https://wiki.multimedia.cx/index.php?title=MPEG-4_Audio#Audio_Object_Types
                ts_muxer->program.audio_stream.aac_audio_object_type = av_stream->audio_object_type;
                ts_muxer->program.audio_stream.aac_channel_config = av_stream->audio_channel_count;
                for (j = 0; ts_muxer_aac_sample_frequencies[j] != 0 && ts_muxer_aac_sample_frequencies[j] != av_stream->audio_sample_rate; j++) 
				{
                }
                if (ts_muxer_aac_sample_frequencies[j] == 0) 
				{
                    MY_LOG(D_AUDIO, "Invalid audio sample frequency %d", av_stream->audio_sample_rate);
                    return -1;
                }
				
                ts_muxer->program.audio_stream.aac_sampling_frequency_index = j;
				//printf("audio_sample_rate %d  index= %d\n",av_stream->audio_sample_rate,j);
            }
        }
    }
    
    if (type == E_STREAM_TYPE_VIDEO) {
        ts_muxer->program.pcr_pid = ts_muxer->program.video_stream.pid;
        
    }else if(type == E_STREAM_TYPE_AUDIO) {
        ts_muxer->program.pcr_pid = ts_muxer->program.audio_stream.pid;
    }

    //if (0 != ts_muxer->program.video_stream.pid) {
    //    ts_muxer->program.pcr_pid = ts_muxer->program.video_stream.pid;

    //}
    //else {
    //    ts_muxer->program.pcr_pid = ts_muxer->program.audio_stream.pid;
    //}

    if (0 == ts_muxer->program.pmt_pid) {
        MY_LOG(D_VIDEO, "ts.%d:failed find either video or audio stream",__LINE__);
        return -1;
    }

    // write PAT and PMT
    //pthread_mutex_lock(&g_ts_mutex);
    ts_muxer_enc_psi(ts_muxer, type);
    //pthread_mutex_unlock(&g_ts_mutex);

    return 0;
}


int ts_muxer_write_packet(ts_muxer_t* ts_muxer, av_packet_t* av_packet) 
{

    av_stream_t* av_stream;

    if (NULL == ts_muxer) 
	{
        return -1;
    }

    if (NULL == av_packet)
	{
        // flush data to file, nothing to do
        return 0;
    }

    if (ts_muxer == NULL || av_packet == NULL || ts_muxer->av_streams == NULL) 
	{
        return -1;
    }

    if (NULL == ts_muxer->avio_context || NULL == ts_muxer->avio_write) 
	{
        return 0;
    }

    if (av_packet->av_stream_index >= ts_muxer->av_stream_count) 
	{

        MY_LOG(D_VIDEO, "Invalid stream count %d in av packet", av_packet->av_stream_index);
        return -1;
    }

    av_stream = &ts_muxer->av_streams[av_packet->av_stream_index];  //鍚屾pkt鐨刬ndex鍒癱seg->ts_muxer->av_streams[index]
    if (av_stream == NULL) 
	{
        MY_LOG(D_VIDEO, "stream %d is NULL in av context", av_packet->av_stream_index);
        return -1;
    }

    if (av_stream->type == AV_STREAM_TYPE_VIDEO
        && ((av_stream->codec == AV_STREAM_CODEC_H264 && ts_muxer->program.video_stream.stream_type == TS_MUXER_STREAM_TYPE_H264)
        ||(av_stream->codec == AV_STREAM_CODEC_H265 && ts_muxer->program.video_stream.stream_type == TS_MUXER_STREAM_TYPE_H265))
        && ts_muxer->program.video_stream.stream_index == av_packet->av_stream_index) 

    {
        //MY_LOG(D_VIDEO, "video packet stream_type=%d", ts_muxer->program.video_stream.stream_type);
        return ts_muxer_enc_video_packet(ts_muxer, &ts_muxer->program.video_stream, av_packet); //鍙戦€?
    } 
	else if (av_stream->type == AV_STREAM_TYPE_AUDIO
               && av_stream->codec == AV_STREAM_CODEC_AAC
               && ts_muxer->program.audio_stream.stream_type == TS_MUXER_STREAM_TYPE_AAC
               && ts_muxer->program.audio_stream.stream_index == av_packet->av_stream_index) 
    {
        return ts_muxer_enc_aac_packet(ts_muxer, &ts_muxer->program.audio_stream, av_packet);
        
    } 
	else if (av_stream->type == AV_STREAM_TYPE_AUDIO
               && av_stream->codec == AV_STREAM_CODEC_AAC_WITH_ADTS
               && ts_muxer->program.audio_stream.stream_type == TS_MUXER_STREAM_TYPE_AAC
               && ts_muxer->program.audio_stream.stream_index == av_packet->av_stream_index) 
    {
        return ts_muxer_enc_aac_packet(ts_muxer, &ts_muxer->program.audio_stream, av_packet);
    
    } 
	else if (av_stream->type == AV_STREAM_NO_VIDEO || av_stream->type == AV_STREAM_NO_AUDIO)
	{
		return 0;
	}
	else 
	{
        MY_LOG(D_VIDEO, "Unprepared stream type %d stream codec %d\n", av_stream->type, av_stream->codec);
        MY_LOG(D_VIDEO, "program.video_stream.stream_type:%d, program.video_stream.stream_index: %d\n",
                ts_muxer->program.video_stream.stream_type,
                ts_muxer->program.video_stream.stream_index);
        return -1;
    }
}
