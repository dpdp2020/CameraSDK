/*
版权所有 (c) 2022-2023
V 1.0
作者: 吴江旻 <1749385659@qq.com;15018916607>
日期: 2022年3月27日
描述: API接口库功能函数

历史:
1、2022年3月27日
    初步创建

使用说明:


 */

#include <string.h>
#include <stdarg.h>
#include "utils.h"

long long get_timestamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    // printf(“second:%ld\n”,tv.tv_sec); //秒
    // printf(“millisecond:%ld\n”,tv.tv_sec1000 + tv.tv_usec/1000); //毫秒
    // printf(“microsecond:%ld\n”,tv.tv_sec1000000 + tv.tv_usec); //微秒
    long long temp_time = tv.tv_sec * 1000000 + tv.tv_usec;
    return temp_time;
}

//由struct timeval结构体数据（由gettimeofday获取到的）转换成可显示的时间字符串
char * get_local_time(char *time_str, int len)
{
    struct tm* ptm;
    char time_string[40];
    long milliseconds;
    struct timeval tv;

    gettimeofday(&tv, NULL);

    ptm = localtime (&(tv.tv_sec));

    /* 格式化日期和时间，精确到秒为单位。*/
    //strftime (time_string, sizeof(time_string), "%Y/%m/%d %H:%M:%S", ptm); //输出格式为: 2018/12/09 10:48:31.391
    //strftime (time_string, sizeof(time_string), "%Y|%m|%d %H-%M-%S", ptm); //输出格式为: 2018|12|09 10-52-28.302
    strftime (time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", ptm); //输出格式为: 2018-12-09 10:52:57.200
//    strftime (time_string, sizeof(time_string), "%Y\\%m\\%d %H-%M-%S", ptm); //输出格式为: 2018\12\09 10-52-28.302

    /* 从微秒计算毫秒。*/
    milliseconds = tv.tv_usec / 1000;

    /* 以秒为单位打印格式化后的时间日期，小数点后为毫秒。*/
    snprintf (time_str, len, "%s.%03ld", time_string, milliseconds);

    return time_str;
}

void mylog_print(const char *fmts, ...)
{
    if ( !fmts || fmts[0] == '\0' )
        return ;

    char buf[512];
    va_list ap;
    va_start(ap, fmts);
   
    vsprintf(buf, fmts, ap);
    size_t len = strlen(buf);
    if ( buf[len-1] == '\n' )
    {
        // Remove EOL character, should it happen to be at the end.
        // The EOL will be added at the end anyway.
        buf[len-1] = '\0';
    }
    va_end(ap);
    
//    printf("%s:%d[%s]  %s\n", __FILE__, __LINE__, __func__, buf);
    char time_str[64];
    printf("[%s]%s\n", get_local_time(time_str, sizeof(time_str)), buf);
    return ;
        
}


int pes_parse(unsigned char* p, unsigned int npos) {
    int pos = 0;
    int packet_start_code_prefix = (p[pos] << 16) | (p[pos + 1] << 8) | p[pos + 2];  //packet_start_code_prefix 24 bslbf
    pos += 3;
    int stream_id = p[pos]; //stream_id 8 uimsbf
    pos++;

    int PES_packet_length = ((unsigned int)p[pos] << 8) | p[pos + 1]; //PES_packet_length 16 uimsbf
    (void)PES_packet_length;
    pos += 2;

    if (0x00000001 != packet_start_code_prefix) {
        MY_LOG(D_ALL, "pes packet start code prefix(%06x) error, it must be 0x00 00 01", packet_start_code_prefix);
        return 255;
    }
    if (stream_id != 188//program_stream_map 1011 1100
        && stream_id != 190//padding_stream 1011 1110
        && stream_id != 191//private_stream_2 1011 1111
        && stream_id != 240//ECM 1111 0000
        && stream_id != 241//EMM 1111 0001
        && stream_id != 255//program_stream_directory 1111 1111
        && stream_id != 242//DSMCC_stream 1111 0010
        && stream_id != 248//ITU-T Rec. H.222.1 type E stream 1111 1000
        )
    {
        if (0x80 != (p[pos] & 0xc0)) {
            MY_LOG(D_ALL, "the first 2 bits:0x%02x error, it must be 0x80.", (p[pos] & 0xc0));
            return 255;
        }
        //skip 2bits//'10' 2 bslbf
        int PES_scrambling_control = (p[pos] & 30) >> 4; //PES_scrambling_control 2 bslbf
        (void)PES_scrambling_control;
        int PES_priority = (p[pos] & 0x08) >> 3; //PES_priority 1 bslbf
        (void)PES_priority;
        int data_alignment_indicator = (p[pos] & 0x04) >> 2;//data_alignment_indicator 1 bslbf
        (void)data_alignment_indicator;
        int copyright = (p[pos] & 0x02) >> 1; //copyright 1 bslbf
        (void)copyright;
        int original_or_copy = (p[pos] & 0x01);//original_or_copy 1 bslbf
        (void)original_or_copy;
        pos++;
        int PTS_DTS_flags = (p[pos] & 0xC0) >> 6; //PTS_DTS_flags 2 bslbf
        int ESCR_flag = (p[pos] & 0x20) >> 5; // ESCR_flag 1 bslbf
        int ES_rate_flag = (p[pos] & 0x10) >> 4;//ES_rate_flag 1 bslbf
        int DSM_trick_mode_flag = (p[pos] & 0x08) >> 3;//DSM_trick_mode_flag 1 bslbf
        int additional_copy_info_flag = (p[pos] & 0x04) >> 2; //additional_copy_info_flag 1 bslbf
        int PES_CRC_flag = (p[pos] & 0x02) >> 1; //PES_CRC_flag 1 bslbf
        int PES_extension_flag = (p[pos] & 0x01);//PES_extension_flag 1 bslbf
        pos++;
        int PES_header_data_length = p[pos]; //PES_header_data_length 8 uimsbf
        (void)PES_header_data_length;
        pos++;

        if (PTS_DTS_flags == 2) {
            // skip 4 bits '0010' 4 bslbf
            // PTS [32..30] 3 bslbf
            // marker_bit 1 bslbf
            // PTS [29..15] 15 bslbf
            // marker_bit 1 bslbf
            // PTS [14..0] 15 bslbf
            // marker_bit 1 bslbf
            unsigned long long pts = (((p[pos] >> 1) & 0x07) << 30) | (p[pos + 1] << 22) | (((p[pos + 2] >> 1) & 0x7F) << 15) | (p[pos + 3] << 7) | ((p[pos + 4] >> 1) & 0x7F);
            MY_LOG(D_ALL, "pts: %lld, stream_id: %x,  PES_packet_length: %d(1)", pts, stream_id, PES_packet_length);
            pos += 5;
        }
        if (PTS_DTS_flags == 3) {
            // '0011' 4 bslbf
            // PTS [32..30] 3 bslbf
            // marker_bit 1 bslbf
            //PTS [29..15] 15 bslbf
            //marker_bit 1 bslbf
            // PTS [14..0] 15 bslbf
            // marker_bit 1 bslbf
            unsigned long long pts = (((p[pos] >> 1) & 0x07) << 30) | (p[pos + 1] << 22) | (((p[pos + 2] >> 1) & 0x7F) << 15) | (p[pos + 3] << 7) | ((p[pos + 4] >> 1) & 0x7F);
            MY_LOG(D_ALL, "pts: %lld, stream_id: %x, PES_packet_length: %d(2)", pts, stream_id, PES_packet_length);
            pos += 5;
            // '0001' 4 bslbf
            // DTS [32..30] 3 bslbf
            // marker_bit 1 bslbf
            // DTS [29..15] 15 bslbf
            // marker_bit 1 bslbf
            // DTS [14..0] 15 bslbf
            // marker_bit 1 bslbf
            unsigned long long dts = (((p[pos] >> 1) & 0x07) << 30) | (p[pos + 1] << 22) | (((p[pos + 2] >> 1) & 0x7F) << 15) | (p[pos + 3] << 7) | ((p[pos + 4] >> 1) & 0x7F);
            pos += 5;
        }
        if (ESCR_flag == 1) {
            // reserved 2 bslbf
            // ESCR_base[32..30] 3 bslbf
            // marker_bit 1 bslbf
            // ESCR_base[29..15] 15 bslbf
            // marker_bit 1 bslbf
            // ESCR_base[14..0] 15 bslbf
            // marker_bit 1 bslbf
            // ESCR_extension 9 uimsbf
            // marker_bit 1 bslbf
            unsigned long long ESCR_base = ((((unsigned long long)p[pos] >> 3) & 0x07) << 30) | (((unsigned long long)p[pos] & 0x03) << 28) | ((unsigned long long)p[pos + 1] << 20) | ((((unsigned long long)p[pos + 2] >> 3) & 0x1F) << 15) | (((unsigned long long)p[pos + 2] & 0x3) << 13) | ((unsigned long long)p[pos + 3] << 5) | ((p[pos + 4] >> 3) & 0x1F);
            int ESCR_extension = ((p[pos + 4] & 0x03) << 7) | ((p[pos + 5] >> 1) & 0x7F);
            (void)ESCR_base;
            (void)ESCR_extension;
            pos += 6;
        }
        if (ES_rate_flag == 1) {
            // marker_bit 1 bslbf
            // ES_rate 22 uimsbf
            // marker_bit 1 bslbf
            int ES_rate = (p[pos] & 0x7F) << 15 | (p[pos + 1]) << 7 | (p[pos + 2] & 0x7F) >> 1;
            (void)ES_rate;
            pos += 3;
        }
        if (DSM_trick_mode_flag == 1) { // ignore
            int trick_mode_control = (p[pos] & 0xE0) >> 5;//trick_mode_control 3 uimsbf
            if (trick_mode_control == 0/*fast_forward*/) {
                // field_id 2 bslbf
                // intra_slice_refresh 1 bslbf
                // frequency_truncation 2 bslbf
            }
            else if (trick_mode_control == 1/*slow_motion*/) {
                //rep_cntrl 5 uimsbf
            }
            else if (trick_mode_control == 2/*freeze_frame*/) {
                // field_id 2 uimsbf
                // reserved 3 bslbf
            }
            else if (trick_mode_control == 3/*fast_reverse*/) {
                // field_id 2 bslbf
                // intra_slice_refresh 1 bslbf
                // frequency_truncation 2 bslbf
            }
            else if (trick_mode_control == 4/*slow_reverse*/) {
                // rep_cntrl 5 uimsbf
            }
            else {
                //reserved 5 bslbf
            }
            pos++;
        }
        if (additional_copy_info_flag == 1) { // ignore
            // marker_bit 1 bslbf
            // additional_copy_info 7 bslbf
            pos++;
        }
        if (PES_CRC_flag == 1) { // ignore
            // previous_PES_packet_CRC 16 bslbf
            pos += 2;
        }
        if (PES_extension_flag == 1) { // ignore
            int PES_private_data_flag = (p[pos] & 0x80) >> 7;// PES_private_data_flag 1 bslbf
            int pack_header_field_flag = (p[pos] & 0x40) >> 6;// pack_header_field_flag 1 bslbf
            int program_packet_sequence_counter_flag = (p[pos] & 0x20) >> 5;// program_packet_sequence_counter_flag 1 bslbf
            int P_STD_buffer_flag = (p[pos] & 0x10) >> 4; // P-STD_buffer_flag 1 bslbf
            // reserved 3 bslbf
            int PES_extension_flag_2 = (p[pos] & 0x01);// PES_extension_flag_2 1 bslbf
            pos++;

            if (PES_private_data_flag == 1) {
                // PES_private_data 128 bslbf
                pos += 16;
            }
            if (pack_header_field_flag == 1) {
                // pack_field_length 8 uimsbf
                // pack_header()
            }
            if (program_packet_sequence_counter_flag == 1) {
                // marker_bit 1 bslbf
                // program_packet_sequence_counter 7 uimsbf
                // marker_bit 1 bslbf
                // MPEG1_MPEG2_identifier 1 bslbf
                // original_stuff_length 6 uimsbf
                pos += 2;
            }
            if (P_STD_buffer_flag == 1) {
                // '01' 2 bslbf
                // P-STD_buffer_scale 1 bslbf
                // P-STD_buffer_size 13 uimsbf
                pos += 2;
            }
            if (PES_extension_flag_2 == 1) {
                // marker_bit 1 bslbf
                int PES_extension_field_length = (p[pos] & 0x7F);// PES_extension_field_length 7 uimsbf
                pos++;
                int i = 0;
                for (i = 0; i < PES_extension_field_length; i++) {
                    // reserved 8 bslbf
                    pos++;
                }
            }
        }

        //        for (int i = 0; i < N1; i++) {
                //stuffing_byte 8 bslbf
        //            rpos++;
        //        }
        //        for (int i = 0; i < N2; i++) {
                //PES_packet_data_byte 8 bslbf
        //        rpos++;
        //        }
        //*ret_pp = p + pos;
        //ret_size = 188 - (npos + pos);
    }
    else if (stream_id == 188//program_stream_map 1011 1100 BC
        || stream_id == 191//private_stream_2 1011 1111 BF
        || stream_id == 240//ECM 1111 0000 F0
        || stream_id == 241//EMM 1111 0001 F1
        || stream_id == 255//program_stream_directory 1111 1111 FF
        || stream_id == 242//DSMCC_stream 1111 0010 F2
        || stream_id == 248//ITU-T Rec. H.222.1 type E stream 1111 1000 F8
        ) {
        //        for (i = 0; i < PES_packet_length; i++) {
                 //PES_packet_data_byte 8 bslbf
        //         rpos++;
        //        }
        //*ret_pp = p + pos;
        //ret_size = 188 - (npos + pos);
        //fwrite(p, 1, 188-(npos+rpos), fd);
    }
    else if (stream_id == 190//padding_stream 1011 1110
        ) {
        //        for (i = 0; i < PES_packet_length; i++) {
                // padding_byte 8 bslbf
        //            rpos++;
        //*ret_pp = p + pos;
        //ret_size = 188 - (npos + pos);
        //        }
    }

    return pos;
}

unsigned char last_video_continuity_counter = 0;
unsigned char last_audio_continuity_counter = 0;

int parse_ts_data(unsigned char * buf, int len)
{
    int pos = 0;
    int npos = 0;
    int n = 0;
    unsigned char w_sync_byte;

    unsigned short w_transport_error_indicator;
    unsigned short w_payload_unit_start_indicator;
    unsigned short w_transport_priority;
    unsigned short w_PID ;

    unsigned char w_transport_scrambling_control ;
    unsigned char w_adaptation_field_control;
    unsigned char w_continuity_counter;

    unsigned char w_adaptation_field_length;

    unsigned char w_discontinuity_indicator;
    unsigned char w_random_access_indicator;
    unsigned char w_elementary_stream_priority_indicator;
    unsigned char w_PCR_flag;
    unsigned char w_OPCR_flag;
    unsigned char w_splicing_point_flag;
    unsigned char w_transport_private_data_flag;
    unsigned char w_adaptation_field_extension_flag;

    //if(PCR_flag == '1')
    unsigned long w_program_clock_reference_base;//33 bits
    unsigned short w_program_clock_reference_extension;//9bits
    //if (OPCR_flag == '1')
    unsigned long w_original_program_clock_reference_base;//33 bits
    unsigned short w_original_program_clock_reference_extension;//9bits
    //if (splicing_point_flag == '1')
    unsigned char w_splice_countdown;
    //if (transport_private_data_flag == '1') 
    unsigned char w_transport_private_data_length;
    unsigned char w_private_data_byte[256];
    //if (adaptation_field_extension_flag == '1')
    unsigned char w_adaptation_field_extension_length;
    unsigned char w_ltw_flag;
    unsigned char w_piecewise_rate_flag;
    unsigned char w_seamless_splice_flag;
    unsigned char w_reserved0;
    //if (ltw_flag == '1')
    unsigned short w_ltw_valid_flag;
    unsigned short w_ltw_offset;
    //if (piecewise_rate_flag == '1')
    unsigned int w_piecewise_rate;//22bits
    //if (seamless_splice_flag == '1')
    unsigned char w_splice_type;//4bits
    unsigned char w_DTS_next_AU1;//3bits
    unsigned char w_marker_bit1;//1bit
    unsigned short w_DTS_next_AU2;//15bit
    unsigned char w_marker_bit2;//1bit
    unsigned short w_DTS_next_AU3;//15bit
    unsigned char w_marker_bit3;//1bit
    //char *tmp_buf = (char*)malloc(600);

    //MY_LOG(D_ALL, "ts len: %d-----------------", len);
    
    while (pos < len)
    {
        //unsigned char* p = buf+pos;
        //int m = 0;
    
        //memset(tmp_buf, 0, 600);
        //for (m = 0; m < 188; m++)
        //{
        //    sprintf(tmp_buf + m*3, "%02x ", p[m]);
        //}
        //MY_LOG(D_ALL, "TS: %s", tmp_buf);

        w_sync_byte = buf[pos];
        pos++;

        w_transport_error_indicator = (buf[pos] & 0x80) >> 7;
        w_payload_unit_start_indicator = (buf[pos] & 0x40) >> 6;
        w_transport_priority = (buf[pos] & 0x20) >> 5;
        w_PID = ((buf[pos] << 8) | buf[pos + 1]) & 0x1FFF;
        pos += 2;

        w_transport_scrambling_control = (buf[pos] & 0xC0) >> 6;
        w_adaptation_field_control = (buf[pos] & 0x30) >> 4;
        w_continuity_counter = (buf[pos] & 0x0F);
        pos++;

        if (w_adaptation_field_control == 2
            || w_adaptation_field_control == 3) {
            // adaptation_field()
            w_adaptation_field_length = buf[pos];
            pos++;

            if (w_adaptation_field_length > 0) {
                w_discontinuity_indicator = (buf[pos] & 0x80) >> 7;
                w_random_access_indicator = (buf[pos] & 0x40) >> 6;
                w_elementary_stream_priority_indicator = (buf[pos] & 0x20) >> 5;
                w_PCR_flag = (buf[pos] & 0x10) >> 4;
                w_OPCR_flag = (buf[pos] & 0x08) >> 3;
                w_splicing_point_flag = (buf[pos] & 0x04) >> 2;
                w_transport_private_data_flag = (buf[pos] & 0x02) >> 1;
                w_adaptation_field_extension_flag = (buf[pos] & 0x01);
                pos++;

                if (w_PCR_flag == 1) { // PCR info
            //program_clock_reference_base 33 uimsbf
            //reserved 6 bslbf
            //program_clock_reference_extension 9 uimsbf
                    pos += 6;
                }
                if (w_OPCR_flag == 1) {
                    //original_program_clock_reference_base 33 uimsbf
                    //reserved 6 bslbf
                    //original_program_clock_reference_extension 9 uimsbf
                    pos += 6;
                }
                if (w_splicing_point_flag == 1) {
                    //splice_countdown 8 tcimsbf
                    pos++;
                }
                if (w_transport_private_data_flag == 1) {
                    //transport_private_data_length 8 uimsbf
                    w_transport_private_data_length = buf[pos];
                    pos++;
                    memcpy(w_private_data_byte, buf + pos, w_transport_private_data_length);
                    pos += w_transport_private_data_length;
                }
                if (w_adaptation_field_extension_flag == 1) {
                    //adaptation_field_extension_length 8 uimsbf
                    w_adaptation_field_extension_length = buf[pos];
                    pos++;
                    //ltw_flag 1 bslbf
                    w_ltw_flag = (buf[pos] & 0x80) >> 7;
                    //piecewise_rate_flag 1 bslbf
                    w_piecewise_rate_flag = (buf[pos] & 0x40) >> 6;
                    //seamless_splice_flag 1 bslbf
                    w_seamless_splice_flag = (buf[pos] & 0x20) >> 5;
                    //reserved 5 bslbf
                    pos++;
                    if (w_ltw_flag == 1) {
                        //ltw_valid_flag 1 bslbf
                        //ltw_offset 15 uimsbf
                        pos += 2;
                    }
                    if (w_piecewise_rate_flag == 1) {
                        //reserved 2 bslbf
                        //piecewise_rate 22 uimsbf
                        pos += 3;
                    }
                    if (w_seamless_splice_flag == 1) {
                        //splice_type 4 bslbf
                        //DTS_next_AU[32..30] 3 bslbf
                        //marker_bit 1 bslbf
                        //DTS_next_AU[29..15] 15 bslbf
                        //marker_bit 1 bslbf
                        //DTS_next_AU[14..0] 15 bslbf
                        //marker_bit 1 bslbf
                        pos += 5;
                    }
                }
            }
            npos += sizeof(w_adaptation_field_length) + w_adaptation_field_length;
            pos = npos;//must consider the 'stuffing_byte' in adaptation field
        }
        if (w_adaptation_field_control == 1
            || w_adaptation_field_control == 3) {
            // data_byte with placeholder
            // payload parser
            if (w_PID == 0x1000 || w_PID == 0x1001) {
                if (w_payload_unit_start_indicator) {
             
                    unsigned int ret_size = 0;
                    unsigned long long dts = 0;
                    unsigned long long pts = 0;

                    npos += 4;
                    //p = buf + npos;
                    //MY_LOG(D_ALL, "pos: %d, npos: %d| %x %x %x %x", 
                    //    pos, npos, p[0], p[1], p[2], p[3]);
                    int ret = pes_parse(buf + npos, npos);
                    if (ret > 188) {
                        MY_LOG(D_ALL, "pes length(%d) error", ret);
                        
                    }
                   

               
                }
                
            }
        }

        //MY_LOG(D_ALL, "w_PID: %x, w_payload_unit_start_indicator: %d, w_continuity_counter: %d", 
        //    w_PID, w_payload_unit_start_indicator, w_continuity_counter);
       
        if (w_PID == 0x1000) {
            if (w_continuity_counter != ((last_video_continuity_counter==15)?0:(last_video_continuity_counter + 1)))
                MY_LOG(D_ALL, "continuity_counter is error: %d/%d", w_continuity_counter, last_video_continuity_counter);
            last_video_continuity_counter = w_continuity_counter;
        }
        else if (w_PID == 0x1001) {
            if (w_continuity_counter != ((last_audio_continuity_counter == 15) ? 0 : (last_audio_continuity_counter + 1)))
                MY_LOG(D_ALL, "continuity_counter is error: %d/%d", w_continuity_counter, last_audio_continuity_counter);
            last_audio_continuity_counter = w_continuity_counter;
        }
            

        pos += (++n * 188 - pos);
        npos = pos;
    }
  
    //free(tmp_buf);
   
    return 0;

}

//跟踪音频数据
int mylog_print_bin(unsigned char* buf, int len)
{
    char data_log[2048] = { 0 };
    unsigned char* data_bin = buf;
    int data_length = len;
    char* p = data_log;
    int i = 0;
    for (i = 0; i < data_length && i <= 16; i++) {
        sprintf(p, "%02x ", data_bin[i]);
        p += 3;
    }

    if (strlen(data_log) > 0)
        MY_LOG(D_ALL, "data: %s", data_log);

    return 0;
}