// Copyright 2020 Fuzhou Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/sample_common.h"
#include "rkmedia_api.h"
#include "rkmedia_venc.h"
//SDK头文件
#include "wsrt_api.h"
#include "cJSON.h"

#ifdef SUPPORT_RGB555LE
#include "bgramapinfo_rgb555le.h"
#else
#include "bgramapinfo.h"
#endif


// for argb8888
#define TEST_ARGB32_PIX_SIZE 4
#define TEST_ARGB32_GREEN 0xFF00FF00
#define TEST_ARGB32_RED 0xFFFF0000
#define TEST_ARGB32_BLUE 0xFF0000FF
#define TEST_ARGB32_TRANS 0x00000000

#define OSD_LETTER_NUM          24
#define OSD_REGION_WIDTH		16
#define OSD_REGION_HEIGHT		34

//日志打印模式：file\print\none，该值保存在该执行文件目录的config.uvc
static char gconf_log_mode[16] = "print";
static bool quit = false;
static FILE *g_output_file;
static RK_S32 g_s32FrameCnt = -1;
static int64_t first_video_ts = 0;   //保存第一个时间戳


//==================================================================
//函数名：  align_value
//作  者：  吴江旻   
//日  期：  2022/4/28 
//功  能：  用于图像长宽大小对齐
//输入参数：
//          value：  输入长或宽的值
//          align：  按多少bits对齐，一般有8、16位对齐；0表示无需对齐；
//返回值： 类型（int)
//          返回对齐或不对齐的数值
//修改记录：
//==================================================================
static int align_value(int value, int align) {
    int ret_value = value;

    if (align && ((ret_value % align) == 0))
        return ret_value;

    if (align && (ret_value % align))
        ret_value = ((ret_value / align) + 1) * align;

    if (ret_value >= value)
        ret_value = align ? (ret_value - align) : value;

    if (ret_value < 0)
        ret_value = 0;

    return ret_value;
}

static void sigterm_handler(int sig) {
  fprintf(stderr, "signal %d\n", sig);
  quit = true;
}

void video_packet_cb(MEDIA_BUFFER mb) {
    static RK_S32 packet_cnt = 0;
    bool isKey = false;
    int ret;
    int64_t frame_timestamp, cur_ts;

    if (quit)
        return;

    const char *nalu_type = "Jpeg data";
    switch (RK_MPI_MB_GetFlag(mb)) {
        case VENC_NALU_IDRSLICE:
            nalu_type = "IDR Slice";
            isKey = true;
            break;
        case VENC_NALU_PSLICE:
            nalu_type = "P Slice";
            isKey = false;
            break;
        default:
            break;
    }

    if (g_output_file) {
    fwrite(RK_MPI_MB_GetPtr(mb), 1, RK_MPI_MB_GetSize(mb), g_output_file);
        printf("#Write packet-%d, %s, size %zu\n", packet_cnt, nalu_type,
                RK_MPI_MB_GetSize(mb));
    } else {
        cur_ts = RK_MPI_MB_GetTimestamp(mb);
        if (first_video_ts == 0) {
            first_video_ts = cur_ts / 1000;
        }
        frame_timestamp = cur_ts / 1000 - first_video_ts;
        ret = wsrt_put_pkt(WS_STREAM_TYPE_VIDEO, isKey, (unsigned char*)RK_MPI_MB_GetPtr(mb), RK_MPI_MB_GetSize(mb), frame_timestamp);
        //printf("#Get packet-%d, %s, size %zu timestamp %lld\n", packet_cnt, nalu_type, RK_MPI_MB_GetSize(mb), frame_timestamp);
    }
    RK_MPI_MB_TsNodeDump(mb);
    RK_MPI_MB_ReleaseBuffer(mb);

    packet_cnt++;
    if ((g_s32FrameCnt >= 0) && (packet_cnt > g_s32FrameCnt))
        quit = true;
}

static RK_CHAR optstr[] = "?::a::w:h:c:o:e:d:I:M:";
static const struct option long_options[] = {
    {"aiq", optional_argument, NULL, 'a'},
    {"device_name", required_argument, NULL, 'd'},
    {"width", required_argument, NULL, 'w'},
    {"height", required_argument, NULL, 'h'},
    {"frame_cnt", required_argument, NULL, 'c'},
    {"output_path", required_argument, NULL, 'o'},
    {"encode", required_argument, NULL, 'e'},
    {"camid", required_argument, NULL, 'I'},
    {"multictx", required_argument, NULL, 'M'},
    {"fps", required_argument, NULL, 'f'},
    {"hdr_mode", required_argument, NULL, 'h' + 'm'},
    {"vi_buf_cnt", required_argument, NULL, 'b' + 'c'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

static void print_usage(const RK_CHAR *name) {
  printf("usage example:\n");
#ifdef RKAIQ
  printf("\t%s [-a [iqfiles_dir]] [-w 1920] "
         "[-h 1080]"
         "[-c 150] "
         "[-d rkispp_scale0] "
         "[-e 0] "
         "[-I 0] "
         "[-M 0] "
         "[-o output.h264] \n",
         name);
  printf("\t-a | --aiq: enable aiq with dirpath provided, eg:-a "
         "/oem/etc/iqfiles/, "
         "set dirpath emtpty to using path by default, without this option aiq "
         "should run in other application\n");
  printf("\t-M | --multictx: switch of multictx in isp, set 0 to disable, set "
         "1 to enable. Default: 0\n");
  printf("\t--fps fps of vi.\n");
  printf("\t--hdr_mode [normal hdr2 hdr3].\n");
  printf("\t--vi_buf_cnt buffer count of vi.\n");
#else
  printf("\t%s [-w 1920] "
         "[-h 1080]"
         "[-c 150] "
         "[-I 0] "
         "[-d rkispp_scale0] "
         "[-e 0] "
         "[-o output.h264] \n",
         name);
#endif
  printf("\t-w | --width: VI width, Default:1920\n");
  printf("\t-h | --heght: VI height, Default:1080\n");
  printf("\t-c | --frame_cnt: frame number of output, Default:150\n");
  printf("\t-I | --camid: camera ctx id, Default 0\n");
  printf("\t-d | --device_name set pcDeviceName, Default:rkispp_scale0, "
         "Option:[rkispp_scale0, rkispp_scale1, rkispp_scale2]\n");
  printf(
      "\t-e | --encode: encode type, Default:h264, Value:h264, h265, mjpeg\n");
  printf("\t-o | --output_path: output path, Default:NULL\n");
}

int request_key_frame()
{
    int ret = 0;
    //设置首发关键帧
    //if ((ret = IMP_Encoder_RequestIDR(CHN_ENABLE)) != 0)
    //    MY_LOG(D_VIDEO, "IMP_Encoder_RequestIDR failure");

    //MY_LOG(D_VIDEO, "IMP_Encoder_RequestIDR success");

    return ret;
}



static void* osd_update_thread()
{
    int ret;

    /*generate time*/
    char DateStr[40];
    struct tm* currDate;
    unsigned i = 0, j = 0;
    void* dateData = NULL;
    
    int test_cnt = 0;
    int bitmap_width = 0;
    int bitmap_height = 0;
    int wxh_size = 0;

    struct tm* ptm;
    char time_string[40];
    long milliseconds;
    struct timeval tv;

    uint32_t* data = malloc(OSD_LETTER_NUM * OSD_REGION_HEIGHT * OSD_REGION_WIDTH * sizeof(uint32_t));

    BITMAP_S BitMap;
    BitMap.enPixelFormat = PIXEL_FORMAT_ARGB_8888;
    BitMap.u32Width = OSD_LETTER_NUM * OSD_REGION_WIDTH;
    BitMap.u32Height = OSD_REGION_HEIGHT;  // OSD_REGION_HEIGHT;
    //BitMap.pData = malloc(wxh_size * TEST_ARGB32_PIX_SIZE);
    BitMap.pData = data;    

    OSD_REGION_INFO_S RngInfo;
    RngInfo.enRegionId = REGION_ID_0;    //get_random_value(REGION_ID_7 + 1, 0);
    RngInfo.u32PosX = 16;   // get_random_value(video_width - bitmap_width, 16);
    RngInfo.u32PosY = 16;   // get_random_value(video_height - bitmap_height, 16);
    RngInfo.u32Width = align_value(BitMap.u32Width, 16);
    RngInfo.u32Height = align_value(BitMap.u32Height, 16); 
    RngInfo.u8Enable = 1;
    RngInfo.u8Inverse = 0;
    printf("#%03d ENABLE RGN[%d]: <%d, %d, %d, %d> for 90ms...\n", test_cnt,
        RngInfo.enRegionId, RngInfo.u32PosX, RngInfo.u32PosY,
        RngInfo.u32Width, RngInfo.u32Height);


    while (!quit) {
        int penpos_t = 0;
        int fontadv = 0;

        gettimeofday(&tv, NULL);
        ptm = localtime(&(tv.tv_sec));

        //从微秒计算毫秒。
        milliseconds = tv.tv_usec / 1000;

        strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", ptm);
        memset(DateStr, 0, sizeof(DateStr));
        //以秒为单位打印格式化后的时间日期，小数点后为毫秒。
        snprintf(DateStr, sizeof(DateStr), "%s:%03ld", time_string, milliseconds);
        //MY_LOG(D_VIDEO, "time_string=%s DateStr=%s", time_string, DateStr);

        //=================================================
        for (i = 0; i < OSD_LETTER_NUM; i++) {
            switch (DateStr[i]) {
            case '0' ... '9':
                dateData = (void*)gBgramap[DateStr[i] - '0'].pdata;
                fontadv = gBgramap[DateStr[i] - '0'].width;
                penpos_t += gBgramap[DateStr[i] - '0'].width;
                break;
            case '-':
                dateData = (void*)gBgramap[10].pdata;
                fontadv = gBgramap[10].width;
                penpos_t += gBgramap[10].width;
                break;
            case ' ':
                dateData = (void*)gBgramap[11].pdata;
                fontadv = gBgramap[11].width;
                penpos_t += gBgramap[11].width;
                break;
            case ':':
                dateData = (void*)gBgramap[12].pdata;
                fontadv = gBgramap[12].width;
                penpos_t += gBgramap[12].width;
                break;

            default:
                break;
            }
#ifdef SUPPORT_RGB555LE
            for (j = 0; j < OSD_REGION_HEIGHT; j++) {
                memcpy((void*)((uint16_t*)data + j * OSD_LETTER_NUM * OSD_REGION_WIDTH + penpos_t),
                    (void*)((uint16_t*)dateData + j * fontadv), fontadv * sizeof(uint16_t));
            }
#else
            for (j = 0; j < OSD_REGION_HEIGHT; j++) {
                memcpy((void*)((uint32_t*)data + j * OSD_LETTER_NUM * OSD_REGION_WIDTH + penpos_t),
                    (void*)((uint32_t*)dateData + j * fontadv), fontadv * sizeof(uint32_t));
            }

#endif
        }
        //if (bitmap_width < 64)
            //bitmap_width = 64;
        //if (bitmap_height < 64)
            //bitmap_height = 64;

        //wxh_size = bitmap_width * bitmap_height;
        
        //if (!BitMap.pData) {
        //    printf("ERROR: no mem left for argb8888(%d)!\n",
        //        wxh_size * TEST_ARGB32_PIX_SIZE);
        //    break;
        //}
        //set_argb8888_buffer((RK_U32*)BitMap.pData, wxh_size, TEST_ARGB32_GREEN);
       

        ret = RK_MPI_VENC_RGN_SetBitMap(0, &RngInfo, &BitMap);
        if (ret) {
            printf("ERROR: set rgn bitmap(enable) failed! ret=%d\n", ret);
            
            break;
        }


        //usleep(100000);
        //printf("     DISABLE RGN[%d]!\n", RngInfo.enRegionId);
        //RngInfo.u8Enable = 0;
        //ret = RK_MPI_VENC_RGN_SetBitMap(0, &RngInfo, &BitMap);
        //if (ret) {
        //    printf("ERROR: set rgn bitmap(disable) failed! ret=%d\n", ret);
        //    break;
        //}


        //sleep(1);
        usleep(90 * 1000);
    }

    // free data
    free(BitMap.pData);
    BitMap.pData = NULL;

    return NULL;
}

int main(int argc, char *argv[]) {
    RK_U32 u32Width = 1920;
    RK_U32 u32Height = 1080;
    RK_CHAR *pDeviceName = "rkispp_scale0";
    RK_CHAR *pOutPath = NULL;
    RK_CHAR *pIqfilesPath = NULL;
    CODEC_TYPE_E enCodecType = RK_CODEC_TYPE_H264;
    RK_CHAR *pCodecName = "H264";
    RK_S32 s32CamId = 0;
    RK_U32 u32BufCnt = 3;
    #ifdef RKAIQ
    RK_BOOL bMultictx = RK_FALSE;
    RK_U32 u32Fps = 30;
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
    #endif

    int c;
    int ret = 0;
    while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
        const char *tmp_optarg = optarg;
        switch (c) {
            case 'a':
                if (!optarg && NULL != argv[optind] && '-' != argv[optind][0]) {
                    tmp_optarg = argv[optind++];
                }
                if (tmp_optarg) {
                    pIqfilesPath = (char *)tmp_optarg;
                } else {
                    pIqfilesPath = "/oem/etc/iqfiles";
                }
                break;
            case 'w':
                u32Width = atoi(optarg);
                break;
            case 'h':
                u32Height = atoi(optarg);
                break;
            case 'c':
                g_s32FrameCnt = atoi(optarg);
                break;
            case 'o':
                pOutPath = optarg;
                break;
            case 'd':
                pDeviceName = optarg;
                break;
            case 'e':
                if (!strcmp(optarg, "h264")) {
                    enCodecType = RK_CODEC_TYPE_H264;
                    pCodecName = "H264";
                } else if (!strcmp(optarg, "h265")) {
                    enCodecType = RK_CODEC_TYPE_H265;
                    pCodecName = "H265";
                } else if (!strcmp(optarg, "mjpeg")) {
                    enCodecType = RK_CODEC_TYPE_MJPEG;
                    pCodecName = "MJPEG";
                } else {
                    printf("ERROR: Invalid encoder type.\n");
                    return 0;
                }
                break;
            case 'I':
                s32CamId = atoi(optarg);
                break;
            #ifdef RKAIQ
            case 'M':
                if (atoi(optarg)) {
                    bMultictx = RK_TRUE;
                }
                break;
            case 'f':
                u32Fps = atoi(optarg);
                printf("#u32Fps = %u.\n", u32Fps);
                break;
            case 'h' + 'm':
                if (strcmp(optarg, "normal") == 0) {
                    hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
                } else if (strcmp(optarg, "hdr2") == 0) {
                    hdr_mode = RK_AIQ_WORKING_MODE_ISP_HDR2;
                } else if (strcmp(optarg, "hdr3") == 0) {
                    hdr_mode = RK_AIQ_WORKING_MODE_ISP_HDR3;
                } else {
                    print_usage(argv[0]);
                    return 0;
                }
                printf("#hdr_mode = %u.\n", hdr_mode);
                break;
            #endif
            case 'b' + 'c':
                u32BufCnt = atoi(optarg);
                printf("#vi buffer conunt = %u.\n", u32BufCnt);
                break;
            case '?':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    printf("#Device: %s\n", pDeviceName);
    printf("#CodecName:%s\n", pCodecName);
    printf("#Resolution: %dx%d\n", u32Width, u32Height);
    printf("#Frame Count to save: %d\n", g_s32FrameCnt);
    printf("#Output Path: %s\n", pOutPath);
    printf("#CameraIdx: %d\n\n", s32CamId);
    #ifdef RKAIQ
    printf("#bMultictx: %d\n\n", bMultictx);
    printf("#Aiq xml dirpath: %s\n\n", pIqfilesPath);
    #endif

    if (pIqfilesPath) {
        #ifdef RKAIQ
        SAMPLE_COMM_ISP_Init(s32CamId, hdr_mode, bMultictx, pIqfilesPath);
        SAMPLE_COMM_ISP_Run(s32CamId);
        SAMPLE_COMM_ISP_SetFrameRate(s32CamId, u32Fps);
        #endif
    }

    if (pOutPath) {
        g_output_file = fopen(pOutPath, "w");
        if (!g_output_file) {
            printf("ERROR: open file: %s fail, exit\n", pOutPath);
            return 0;
        }
    }

    RK_MPI_SYS_Init();
    VI_CHN_ATTR_S vi_chn_attr;
    vi_chn_attr.pcVideoNode = pDeviceName;
    vi_chn_attr.u32BufCnt = u32BufCnt;
    vi_chn_attr.u32Width = u32Width;
    vi_chn_attr.u32Height = u32Height;
    vi_chn_attr.enPixFmt = IMAGE_TYPE_NV12;
    vi_chn_attr.enBufType = VI_CHN_BUF_TYPE_MMAP;
    vi_chn_attr.enWorkMode = VI_WORK_MODE_NORMAL;
    ret = RK_MPI_VI_SetChnAttr(s32CamId, 0, &vi_chn_attr);
    ret |= RK_MPI_VI_EnableChn(s32CamId, 0);
    if (ret) {
        printf("ERROR: create VI[0] error! ret=%d\n", ret);
        return 0;
    }

    VENC_CHN_ATTR_S venc_chn_attr;
    memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
    switch (enCodecType) {
        case RK_CODEC_TYPE_H265:
        venc_chn_attr.stVencAttr.enType = RK_CODEC_TYPE_H265;
        venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
        venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = 30;
        venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = u32Width * u32Height;
        // frame rate: in 30/1, out 30/1.
        venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = 30;
        venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = 30;
        break;
        case RK_CODEC_TYPE_MJPEG:
        venc_chn_attr.stVencAttr.enType = RK_CODEC_TYPE_MJPEG;
        venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
        venc_chn_attr.stRcAttr.stMjpegCbr.fr32DstFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stMjpegCbr.fr32DstFrameRateNum = 30;
        venc_chn_attr.stRcAttr.stMjpegCbr.u32SrcFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stMjpegCbr.u32SrcFrameRateNum = 30;
        venc_chn_attr.stRcAttr.stMjpegCbr.u32BitRate = u32Width * u32Height * 8;
        break;
        case RK_CODEC_TYPE_H264:
        default:
        venc_chn_attr.stVencAttr.enType = RK_CODEC_TYPE_H264;
        venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = 30;
        venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = u32Width * u32Height;
        // frame rate: in 30/1, out 30/1.
        venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = 30;
        venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = 30;
        break;
    }
    venc_chn_attr.stVencAttr.imageType = IMAGE_TYPE_NV12;
    venc_chn_attr.stVencAttr.u32PicWidth = u32Width;
    venc_chn_attr.stVencAttr.u32PicHeight = u32Height;
    venc_chn_attr.stVencAttr.u32VirWidth = u32Width;
    venc_chn_attr.stVencAttr.u32VirHeight = u32Height;
    venc_chn_attr.stVencAttr.u32Profile = 77;
    ret = RK_MPI_VENC_CreateChn(0, &venc_chn_attr);
    if (ret) {
        printf("ERROR: create VENC[0] error! ret=%d\n", ret);
        return 0;
    }

    MPP_CHN_S stEncChn;
    stEncChn.enModId = RK_ID_VENC;
    stEncChn.s32DevId = 0;
    stEncChn.s32ChnId = 0;
    ret = RK_MPI_SYS_RegisterOutCb(&stEncChn, video_packet_cb);
    if (ret) {
        printf("ERROR: register output callback for VENC[0] error! ret=%d\n", ret);
        return 0;
    }

    RK_MPI_VENC_RGN_Init(0, NULL);

    MPP_CHN_S stSrcChn;
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 0;
    MPP_CHN_S stDestChn;
    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;
    ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (ret) {
        printf("ERROR: Bind VI[0] and VENC[0] error! ret=%d\n", ret);
        return 0;
    }

    printf("%s initial finish\n", __func__);
    signal(SIGINT, sigterm_handler);

    pthread_t tid;
    ret = pthread_create(&tid, NULL, osd_update_thread, NULL);
    if (ret) {
        printf("osd_update_thread create error\n");
        return 0;
    }

    //wsrt_parameters_t av_params;
    //av_params.type = WS_STREAM_NO_AUDIO;
    //av_params.video_codec = WS_STREAM_CODEC_H264;
    //av_params.audio_codec = WS_STREAM_CODEC_AAC_WITH_ADTS;
    //av_params.audio_sample_rate = AUDIO_SAMPLE_RATE_16000;
    //av_params.audio_channel_count = AUDIO_SOUND_MODE_MONO;
    
    //[
    //{
    //    "stream_type": "%d",         //WS_STREAM_NO_AUDIO
    //    "video_codec" : "%d",         //WS_STREAM_CODEC_H264
    //    "audio_codec" : "%d",
    //    "audio_sample_rate" : "%d",
    //    "audio_channel_count" : "%d",

    //    "uri" : "srt://www.baozan.cloud:10080?streamid=#!::h=live/livestream,m=publish",
    //    "display_log_mode" : "print",
    //    "key_frame_func" : ""
    //}
    //]

    cJSON* root = cJSON_CreateObject();
    if (!root)
    {
        printf("Error before: [%s]\n", cJSON_GetErrorPtr());
        return -1;
    }

    cJSON* item = cJSON_CreateNumber(WS_STREAM_NO_AUDIO);
    cJSON_AddItemToObject(root, "stream_type", item);
    item = cJSON_CreateNumber(WS_STREAM_CODEC_H264);
    cJSON_AddItemToObject(root, "video_codec", item);
    item = cJSON_CreateNumber(-1);
    cJSON_AddItemToObject(root, "audio_codec", item);
    item = cJSON_CreateNumber(-1);
    cJSON_AddItemToObject(root, "audio_sample_rate", item);
    item = cJSON_CreateNumber(-1);
    cJSON_AddItemToObject(root, "audio_channel_count", item);
    item = cJSON_CreateString("srt://www.baozan.cloud:10080?streamid=#!::h=live/livestream,m=publish");
    cJSON_AddItemToObject(root, "uri", item);
    item = cJSON_CreateString(gconf_log_mode);
    cJSON_AddItemToObject(root, "display_log_mode", item);
    item = cJSON_CreateNumber((int)request_key_frame);
    cJSON_AddItemToObject(root, "key_frame_func", item);
    printf("request_key_frame(): %x\n", (int)request_key_frame);

    char* params = cJSON_Print(root);
    wsrt_startup(params);
    cJSON_Delete(root);
    free(params);

    while (!quit) {
        usleep(500000);
    }

    if (g_output_file)
        fclose(g_output_file);

    printf("%s exit!\n", __func__);


    wsrt_close();

    // unbind first
    ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (ret) {
    printf("ERROR: UnBind VI[0] and VENC[0] error! ret=%d\n", ret);
    return 0;
    }
    // destroy venc before vi
    ret = RK_MPI_VENC_DestroyChn(0);
    if (ret) {
        printf("ERROR: Destroy VENC[0] error! ret=%d\n", ret);
        return 0;
    }
    // destroy vi
    ret = RK_MPI_VI_DisableChn(s32CamId, 0);
    if (ret) {
        printf("ERROR: Destroy VI[0] error! ret=%d\n", ret);
        return 0;
    }

    if (pIqfilesPath) {
        #ifdef RKAIQ
        SAMPLE_COMM_ISP_Stop(s32CamId);
        #endif
    }

    wsrt_cleanup();

    return 0;
}
