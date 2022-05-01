#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <list>

#include <imp/imp_audio.h>
#include <imp/imp_log.h>
#include <imp/imp_isp.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>
#include <imp/imp_osd.h>

#include "aacenc_lib.h"
#include "cJSON.h"

#ifdef SUPPORT_RGB555LE
#include "bgramapinfo_rgb555le.h"
#else
#include "bgramapinfo.h"
#endif
#include "logodata_100x100_bgra.h"

//SDK头文件
#include "wsrt_api.h"

//本程序的辅助函数
#include "utils.h"

using namespace std;

//是否使用libwsrt的编译开关
#define USE_WSRT_LIBRARY
#define ENABLE_OSD

#define TAG "WCAM"

#define BITRATE_720P_Kbs        1000

#define ENC_H264_CHANNEL		0
#define ENC_JPEG_CHANNEL		1

#define FS_CHN_NUM			    2  //MIN 1,MAX 2
#define SENSOR_CUBS_TYPE        TX_SENSOR_CONTROL_INTERFACE_I2C

//#define VIDEO_CODEC             IMP_ENC_PROFILE_HEVC_MAIN
#define VIDEO_CODEC             IMP_ENC_PROFILE_AVC_MAIN

#define CH0_INDEX               0
#define CH1_INDEX               1
#define CHN_ENABLE              1
#define CHN0_EN                 0
#define CHN1_EN                 1
#define CROP_EN					1

#define OSD_LETTER_NUM          24
#define OSD_REGION_WIDTH		16
#define OSD_REGION_HEIGHT		34
#define SENSOR_WIDTH			960
#define SENSOR_HEIGHT			540


struct chn_conf{
	unsigned int index;//0 for main channel ,1 for second channel
	unsigned int enable;
	IMPFSChnAttr fs_chn_attr;
	IMPCell framesource_chn;
	IMPCell imp_encoder;
};


char gconf_Sensor_Name[20] = {0};
int gconf_i2c_addr = 0;
int gconf_nrvbs = 0;
int gconf_FPS_Num = 0;
int gconf_FPS_Den = 0;
int gconf_Main_VideoWidth = 0;
int gconf_Main_VideoHeight = 0;
int gconf_Main_VideoWidth_ori = 0;
int gconf_Main_VideoHeight_ori = 0;
int gconf_Second_VideoWidth = 0;
int gconf_Second_VideoHeight = 0;

//日志打印模式：file\print\none，该值保存在该执行文件目录的config.uvc
char gconf_log_mode[16] = { 0 };

IMPSensorInfo sensor_info;

static const IMPEncoderRcMode S_RC_METHOD = IMP_ENC_RC_MODE_CBR;

static struct chn_conf chn[FS_CHN_NUM];
int grpNum = 0;
IMPRgnHandle* prHander;

static bool isQuit = false;

pthread_mutex_t g_mutex;

////////////////////////////////////////////////////////////////
#include <sys/mman.h>
#define REG_TCU_TCNT3				0x10002078
#define EXTAL_FREQ					(24 * 1000 * 1000)
#define TIMER_PRESCALE_1			512
#define TIMER_PRESCALE_2			16


long long timeum(){
    struct timeval tim;
    gettimeofday (&tim , NULL);
    return (long long)tim.tv_sec*1000000+tim.tv_usec;
}

static int sample_system_init()
{
	int ret = 0;

    IMP_LOG_DBG(TAG, "sample_system_init start\n");

	ret = IMP_ISP_Open();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "failed to open ISP\n");
		return -1;
	}

	ret = IMP_ISP_AddSensor(&sensor_info);
	if(ret < 0){
		IMP_LOG_ERR(TAG, "failed to AddSensor\n");
		return -1;
	}

	ret = IMP_ISP_EnableSensor();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "failed to EnableSensor\n");
		return -1;
	}

	ret = IMP_System_Init();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_System_Init failed\n");
		return -1;
	}

	/* enable turning, to debug graphics */
	ret = IMP_ISP_EnableTuning();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_ISP_EnableTuning failed\n");
		return -1;
	}

    ret = IMP_ISP_Tuning_SetSensorFPS(gconf_FPS_Num, gconf_FPS_Den);
    if (ret < 0){
        IMP_LOG_ERR(TAG, "failed to set sensor fps\n");
        return -1;
    }

	IMP_LOG_DBG(TAG, "ImpSystemInit success\n");

	return 0;
}

static int sample_system_exit()
{
	int ret = 0;

	IMP_LOG_DBG(TAG, "sample_system_exit start\n");

	IMP_System_Exit();

    ret = IMP_ISP_DisableSensor();
    if(ret < 0){
        IMP_LOG_ERR(TAG, "failed to DisableSensor\n");
        return -1;
    }

    ret = IMP_ISP_DelSensor(&sensor_info);
    if(ret < 0){
        IMP_LOG_ERR(TAG, "failed to DelSensor\n");
        return -1;
    }

	ret = IMP_ISP_DisableTuning();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_ISP_DisableTuning failed\n");
		return -1;
	}

    if(IMP_ISP_Close()){
        IMP_LOG_ERR(TAG, "failed to close ISP\n");
        return -1;
    }

	IMP_LOG_DBG(TAG, " sample_system_exit success\n");

	return 0;
}

static int sample_framesource_streamon()
{
	int ret = 0, i = 0;
	/* Enable channels */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_FrameSource_EnableChn(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_EnableChn(%d) error: %d\n", ret, chn[i].index);
				return -1;
			}
		}
	}
	return 0;
}

static int sample_framesource_streamoff()
{
	int ret = 0, i = 0;
	/* Enable channels */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable){
			ret = IMP_FrameSource_DisableChn(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_DisableChn(%d) error: %d\n", ret, chn[i].index);
				return -1;
			}
		}
	}
	return 0;
}

static int sample_framesource_init()
{
	int i, ret;
	printf("%s enter\n", __func__);
	for (i = 0; i <  FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_FrameSource_CreateChn(chn[i].index, &chn[i].fs_chn_attr);
			if(ret < 0){
				IMP_LOG_ERR(TAG, "IMP_FrameSource_CreateChn(chn%d) error !\n", chn[i].index);
				return -1;
			}

			ret = IMP_FrameSource_SetChnAttr(chn[i].index, &chn[i].fs_chn_attr);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_SetChnAttr(chn%d) error !\n",  chn[i].index);
				return -1;
			}
		}
	}
	printf("%s end\n", __func__);
	return 0;
}

static int sample_framesource_exit()
{
	int ret,i;

	for (i = 0; i <  FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			/*Destroy channel i*/
			ret = IMP_FrameSource_DestroyChn(i);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_DestroyChn() error: %d\n", ret);
				return -1;
			}
		}
	}
	return 0;
}

static int sample_encoder_init()
{
	int i, ret, chnNum = 0;
	IMPFSChnAttr *imp_chn_attr_tmp;
	IMPEncoderChnAttr channel_attr;

    for (i = 0; i <  FS_CHN_NUM; i++) {
        if (chn[i].enable) {
            imp_chn_attr_tmp = &chn[i].fs_chn_attr;
            chnNum = chn[i].index;

            memset(&channel_attr, 0, sizeof(IMPEncoderChnAttr));
            ret = IMP_Encoder_SetDefaultParam(&channel_attr, VIDEO_CODEC, S_RC_METHOD,
                    imp_chn_attr_tmp->picWidth, imp_chn_attr_tmp->picHeight,
                    imp_chn_attr_tmp->outFrmRateNum, imp_chn_attr_tmp->outFrmRateDen,
                    imp_chn_attr_tmp->outFrmRateNum * 2 / imp_chn_attr_tmp->outFrmRateDen, 1,
                    (S_RC_METHOD == IMP_ENC_RC_MODE_FIXQP) ? 50 : -1,
                    (uint64_t)BITRATE_720P_Kbs * (imp_chn_attr_tmp->picWidth * imp_chn_attr_tmp->picHeight) / (1280 * 720));

            if (ret < 0) {
                IMP_LOG_ERR(TAG, "IMP_Encoder_SetDefaultParam(%d) error !\n", chnNum);
                return -1;
            }

            ret = IMP_Encoder_CreateChn(chnNum, &channel_attr);
            if (ret < 0) {
                IMP_LOG_ERR(TAG, "IMP_Encoder_CreateChn(%d) error !\n", chnNum);
                return -1;
            }

            ret = IMP_Encoder_RegisterChn(chn[i].index, chnNum);
            if (ret < 0) {
                IMP_LOG_ERR(TAG, "IMP_Encoder_RegisterChn(%d, %d) error: %d\n", chn[i].index, chnNum, ret);
                return -1;
            }
        }
    }

	return 0;
}

static int encoder_chn_exit(int encChn)
{
	int ret;
	IMPEncoderChnStat chn_stat;
	ret = IMP_Encoder_Query(encChn, &chn_stat);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_Query(%d) error: %d\n",
					encChn, ret);
		return -1;
	}

	if (chn_stat.registered) {
		ret = IMP_Encoder_UnRegisterChn(encChn);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_UnRegisterChn(%d) error: %d\n",
						encChn, ret);
			return -1;
		}

		ret = IMP_Encoder_DestroyChn(encChn);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_DestroyChn(%d) error: %d\n",
						encChn, ret);
			return -1;
		}
	}

	return 0;
}

static int sample_encoder_exit(void)
{
	int ret;

	ret = encoder_chn_exit(ENC_H264_CHANNEL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Encoder Channel %d exit  error: %d\n",
					ENC_H264_CHANNEL, ret);
		return -1;
	}

	ret = encoder_chn_exit(ENC_JPEG_CHANNEL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Encoder Channel %d exit  error: %d\n",
					ENC_JPEG_CHANNEL, ret);
		return -1;
	}

	ret = IMP_Encoder_DestroyGroup(0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_DestroyGroup(0) error: %d\n", ret);
		return -1;
	}

	return 0;
}

static int64_t last_video_ts = 0;
static int64_t last_audio_ts = 0;
static int64_t hold_video_ts = 0;
static int64_t hold_audio_ts = 0;
//static int64_t last_ts = 0;
static int audioPatPmtFreq = 0;



void *get_h264_stream(void *args)
{
	int i, j, ret;
	char stream_path[64];
    FILE *stream_fd = NULL;
    char *tmp_ptr;
    uint32_t frame_size;
    bool isKey;
    int64_t frame_timestamp;

	i = (int ) (*((int*)args));

	ret = IMP_Encoder_StartRecvPic(i);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", i);
		return ((void *)-1);
	}

    IMP_LOG_DBG(TAG, "OK\n");

//    char path[64] = "";
//    sprintf(path, "./o%d.h264", i);
//    stream_fd = fopen(path, "w");
//    if (stream_fd < 0)
//    {
//        printf("open %s error[%d] !\n", path, errno);
//        return NULL;
//    }
    
    while(!isQuit){
        ret = IMP_Encoder_PollingStream(i, 1000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "Polling stream timeout\n");
			continue;
		}

		IMPEncoderStream stream;
		/* Get H264 Stream */
		ret = IMP_Encoder_GetStream(i, &stream, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream() failed\n");
			return ((void *)-1);
		}

        //MY_LOG(D_VIDEO, "packCount=%d streamSize=%d", stream.packCount, stream.streamSize);
        frame_size = 0;
        isKey = false;
        for(j=0;j<stream.packCount;j++)
        {
            IMPEncoderPack *pack = &stream.pack[j];
//            MY_LOG("pack->offset=%d", pack->offset);
            if(pack->length){
                uint32_t remSize = stream.streamSize - pack->offset;
                if(remSize < pack->length){
                    frame_timestamp = pack->timestamp;

                    MY_LOG(D_VIDEO, "get video len1:%d", remSize);
//                    ret = mysrt_send((char *)(stream.virAddr + pack->offset), remSize);
//                    fwrite((char *)(stream.virAddr + pack->offset), 1, remSize, stream_fd);
//                    ret = put_pkt_data(E_STREAM_TYPE_VIDEO,
//                                       (pack->nalType.h265NalType == IMP_H265_NAL_SLICE_IDR_W_RADL)?true:false,
//                                       (char *)(stream.virAddr + pack->offset), remSize, pack->timestamp);
//                    if(ret<0){
//                        printf("----- 1111---srt_send failure----\n");
//                        continue;
//                    }
                    frame_size += remSize;
    
                    MY_LOG(D_VIDEO, "get video len2:%d", pack->length - remSize);
//                    ret = mysrt_send((char *)(stream.virAddr), pack->length - remSize);
//                    fwrite((char *)(stream.virAddr), 1, pack->length - remSize, stream_fd);
//                    ret = put_pkt_data(E_STREAM_TYPE_VIDEO,
//                                       (pack->nalType.h265NalType == IMP_H265_NAL_SLICE_IDR_W_RADL)?true:false,
//                                       (char *)(stream.virAddr), pack->length - remSize, pack->timestamp);
//                    if(ret<0){
//                        printf("----- 2222--- IMP_Encoder_ReleaseStream----\n");
//                        continue;
//                    }
                    frame_size += pack->length - remSize;
                    
                }
                else{
                    tmp_ptr = (char *)(stream.virAddr + pack->offset);
//                    ret = mysrt_send((char *)(stream.virAddr + pack->offset), pack->length);
//                    fwrite(tmp_ptr, 1, pack->length, stream_fd);
                    frame_size += pack->length;
                    frame_timestamp = pack->timestamp;
                    
                    //MY_LOG(D_VIDEO, "VIDEO_CODEC=%d", VIDEO_CODEC);
                    if(VIDEO_CODEC == IMP_ENC_PROFILE_HEVC_MAIN){
                        if(pack->nalType.h265NalType == IMP_H265_NAL_SLICE_IDR_W_RADL){
                            isKey = true;
                            MY_LOG(D_VIDEO, "h265 nalType=%d iskey=%d", pack->nalType.h265NalType, isKey);
                        }
                    }
                    else{
                        if(pack->nalType.h264NalType == IMP_H264_NAL_SLICE_IDR){
                            isKey = true;
                            //MY_LOG(D_VIDEO, "h264 nalType=%d iskey=%d", pack->nalType.h264NalType, isKey);
                       }
                    }
                    //MY_LOG(D_VIDEO, "get video len3:%d offset:%d|isKey:%d|nalType:%d|timestamp:%llu|%02x %02x %02x %02x %02x %02x",
                    //        pack->length, pack->offset, isKey, pack->nalType, frame_timestamp,
                    //        tmp_ptr[0], tmp_ptr[1], tmp_ptr[2], tmp_ptr[3], tmp_ptr[4], tmp_ptr[5]);

                }
            }

        }
        
        if(frame_size)
        {
            int64_t cur_ts = frame_timestamp;
            if (last_video_ts == 0) {
                last_video_ts = cur_ts / 1000;
            }
            frame_timestamp = cur_ts / 1000 - last_video_ts;
            //MY_LOG(D_VIDEO, "cur_ts: %lld last_video_ts: %lld, frame_timestamp: %lld", cur_ts, last_video_ts, frame_timestamp);

            if (frame_timestamp < hold_video_ts)
                MY_LOG(D_VIDEO, "video timestamp is error, hold_video_ts: %lld", hold_video_ts);
            hold_video_ts = frame_timestamp;


            //last_video_ts += 50;
            //frame_timestamp = last_video_ts;
          
            MY_LOG(D_VIDEO, "video timestamp=%lld", frame_timestamp);
            pthread_mutex_lock(&g_mutex);

            ret = wsrt_put_pkt(WS_STREAM_TYPE_VIDEO, isKey, (unsigned char*)stream.virAddr, frame_size, frame_timestamp);

            pthread_mutex_unlock(&g_mutex);
            if(ret<0){
                printf("----- 3333--- IMP_Encoder_ReleaseStream----\n");
                continue;
            }
        }
        IMP_Encoder_ReleaseStream(i, &stream);
	}

	ret = IMP_Encoder_StopRecvPic(i);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic() failed\n");
		return ((void *)-1);
	}

//    fclose(stream_fd);
    printf("-----exit thread chn:%d----\n", i);

	return ((void *)0);
}


static int loadconfig(void)
{
	char *buf = NULL;
	char *str = NULL;
	char *linebuf = NULL;
	char *tmpbuf1 = NULL;
	char *tmpbuf2 = NULL;
	FILE *fp = NULL;
	buf = (char*)malloc(1024*3);
	if (NULL == buf) {
		printf("err: malloc err\n");
		return -1;
	}
	memset(buf, 0, 1024);
	linebuf = buf;
	tmpbuf1 = buf+1024*1;
	tmpbuf2 = buf+1024*2;
	sprintf(tmpbuf1, "./config.uvc");
	while(1){
		if(0==access(tmpbuf1, F_OK))
			break;
		else
			usleep(10000);
	}
	fp = fopen(tmpbuf1, "r");
	if (NULL == fp) {
		printf("err: fopen err\n");
		return -1;
	}

	while (NULL != (str = fgets(linebuf, 1024, fp))) {
		if (strlen(str) > 1000) {
			printf("err: str too length\n");
			return -1;
		} else if (2 != sscanf(linebuf, "%[^:]:%s", tmpbuf1, tmpbuf2)) {
			printf("warn: skip config  %s\n", linebuf);
			continue;
		}
		char *ch = strchr(tmpbuf1, ' ');
		if (ch) *ch = 0;
		if (0 == strcmp(tmpbuf1, "sensor_name")) {
			strncpy(gconf_Sensor_Name, tmpbuf2, 15);
		} else if (0 == strcmp(tmpbuf1, "i2c_addr")) {
			gconf_i2c_addr = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "nrvbs")) {
			gconf_nrvbs = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "fps_num")) {
			gconf_FPS_Num = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "fps_den")) {
			gconf_FPS_Den = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "width")) {
			gconf_Main_VideoWidth = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "height")) {
			gconf_Main_VideoHeight = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "second_width")) {
			gconf_Second_VideoWidth = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "second_height")) {
			gconf_Second_VideoHeight = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "width_ori")) {
			gconf_Main_VideoWidth_ori = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "height_ori")) {
			gconf_Main_VideoHeight_ori = strtol(tmpbuf2, NULL, 0);
		} else if (0 == strcmp(tmpbuf1, "log_mode")) {
            strncpy(gconf_log_mode, tmpbuf2, 15);
        }
	}

	sprintf(tmpbuf1, "%-16s:%s", "sensor_name", gconf_Sensor_Name);printf("%s\n", tmpbuf1);
	sprintf(tmpbuf1, "%-16s:0x%x", "i2c_addr", gconf_i2c_addr);printf("%s\n", tmpbuf1);
	sprintf(tmpbuf1, "%-16s:%d", "nrvbs", gconf_nrvbs);printf("%s\n", tmpbuf1);
	sprintf(tmpbuf1, "%-16s:%d", "fps_num", gconf_FPS_Num);printf("%s\n", tmpbuf1);
	sprintf(tmpbuf1, "%-16s:%d", "fps_den", gconf_FPS_Den);printf("%s\n", tmpbuf1);
	sprintf(tmpbuf1, "%-16s:%d", "width", gconf_Main_VideoWidth);printf("%s\n", tmpbuf1);
	sprintf(tmpbuf1, "%-16s:%d", "height", gconf_Main_VideoHeight);printf("%s\n", tmpbuf1);
	sprintf(tmpbuf1, "%-16s:%d", "width_ori", gconf_Main_VideoWidth_ori);printf("%s\n", tmpbuf1);
	sprintf(tmpbuf1, "%-16s:%d", "height_ori", gconf_Main_VideoHeight_ori);printf("%s\n", tmpbuf1);
	sprintf(tmpbuf1, "%-16s:%d", "second_width", gconf_Second_VideoWidth);printf("%s\n", tmpbuf1);
    sprintf(tmpbuf1, "%-16s:%d", "second_height", gconf_Second_VideoHeight); printf("%s\n", tmpbuf1);
    sprintf(tmpbuf1, "%-16s:%s", "log_mode", gconf_log_mode); printf("%s\n", tmpbuf1);

    struct chn_conf tmpchn[FS_CHN_NUM];
		
    tmpchn[0].index = CH0_INDEX;
    tmpchn[0].enable = CHN0_EN;
    /*.payloadType = IMP_ENC_PROFILE_HEVC_MAIN,*/
            
    tmpchn[0].fs_chn_attr.pixFmt = PIX_FMT_NV12;
    tmpchn[0].fs_chn_attr.outFrmRateNum = gconf_FPS_Num;
    tmpchn[0].fs_chn_attr.outFrmRateDen = gconf_FPS_Den;
    tmpchn[0].fs_chn_attr.nrVBs = gconf_nrvbs;// gconf_nrvbs,
    tmpchn[0].fs_chn_attr.type = FS_PHY_CHANNEL;

    tmpchn[0].fs_chn_attr.crop.enable = 1;
    tmpchn[0].fs_chn_attr.crop.top = 0;
    tmpchn[0].fs_chn_attr.crop.left = 0;
    tmpchn[0].fs_chn_attr.crop.width = gconf_Main_VideoWidth;
    tmpchn[0].fs_chn_attr.crop.height = gconf_Main_VideoHeight;

    tmpchn[0].fs_chn_attr.scaler.enable = 0;
    tmpchn[0].fs_chn_attr.scaler.outwidth = gconf_Main_VideoWidth;
    tmpchn[0].fs_chn_attr.scaler.outheight = gconf_Main_VideoHeight;

    tmpchn[0].fs_chn_attr.picWidth = gconf_Main_VideoWidth;
    tmpchn[0].fs_chn_attr.picHeight = gconf_Main_VideoHeight;
			
    tmpchn[0].framesource_chn =	{ DEV_ID_FS, 0, 0};
    tmpchn[0].imp_encoder = { DEV_ID_ENC, 0, 0};
		
		
    tmpchn[1].index = CH1_INDEX;
    tmpchn[1].enable = CHN1_EN;
    /*.payloadType = IMP_ENC_PROFILE_HEVC_MAIN,*/
    tmpchn[1].fs_chn_attr.pixFmt = PIX_FMT_NV12;
    tmpchn[1].fs_chn_attr.outFrmRateNum = gconf_FPS_Num;
    tmpchn[1].fs_chn_attr.outFrmRateDen = gconf_FPS_Den;
    tmpchn[1].fs_chn_attr.nrVBs = gconf_nrvbs;
    tmpchn[1].fs_chn_attr.type = FS_PHY_CHANNEL;

    tmpchn[1].fs_chn_attr.crop.enable = CROP_EN;
    tmpchn[1].fs_chn_attr.crop.top = 0;
    tmpchn[1].fs_chn_attr.crop.left = 0;
    tmpchn[1].fs_chn_attr.crop.width = gconf_Second_VideoWidth;
    tmpchn[1].fs_chn_attr.crop.height = gconf_Second_VideoHeight;

    tmpchn[1].fs_chn_attr.scaler.enable = 1;
    tmpchn[1].fs_chn_attr.scaler.outwidth = gconf_Second_VideoWidth;
    tmpchn[1].fs_chn_attr.scaler.outheight = gconf_Second_VideoHeight;

    tmpchn[1].fs_chn_attr.picWidth = gconf_Second_VideoWidth;
    tmpchn[1].fs_chn_attr.picHeight = gconf_Second_VideoHeight;

    tmpchn[1].framesource_chn =	{ DEV_ID_FS, 1, 0};
    tmpchn[1].imp_encoder = { DEV_ID_ENC, 1, 0};
		
	
	memcpy(chn, tmpchn, sizeof(struct chn_conf) * FS_CHN_NUM);

    memset(&sensor_info, 0, sizeof(IMPSensorInfo));
	memcpy(sensor_info.name, gconf_Sensor_Name, sizeof(gconf_Sensor_Name));
	sensor_info.cbus_type = SENSOR_CUBS_TYPE;
	memcpy(sensor_info.i2c.type, gconf_Sensor_Name, sizeof(gconf_Sensor_Name));
	sensor_info.i2c.addr = gconf_i2c_addr;

    free(buf);
	return 0;
}


static const char* fdkaac_error(AACENC_ERROR erraac)
{
    switch (erraac)
    {
    case AACENC_OK: return "No error";
    case AACENC_INVALID_HANDLE: return "Invalid handle";
    case AACENC_MEMORY_ERROR: return "Memory allocation error";
    case AACENC_UNSUPPORTED_PARAMETER: return "Unsupported parameter";
    case AACENC_INVALID_CONFIG: return "Invalid config";
    case AACENC_INIT_ERROR: return "Initialization error";
    case AACENC_INIT_AAC_ERROR: return "AAC library initialization error";
    case AACENC_INIT_SBR_ERROR: return "SBR library initialization error";
    case AACENC_INIT_TP_ERROR: return "Transport library initialization error";
    case AACENC_INIT_META_ERROR: return "Metadata library initialization error";
    case AACENC_ENCODE_ERROR: return "Encoding error";
    case AACENC_ENCODE_EOF: return "End of file";
    default: return "Unknown error";
    }
}



int aac_encodec_init(HANDLE_AACENCODER *handle)
{
    int aot = AOT_AAC_LC; // AOT_AAC_LC; (AOT_ER_AAC_LD;  AOT_ER_AAC_ELD; type eld 39 ld 23)
    int eld_sbr = 1;
    int vbr = 0;
    int bitrate = 64000;
    int sample_rate = AUDIO_SAMPLE_RATE_16000;
    int afterburner = 1;
    int channels = AUDIO_SOUND_MODE_MONO;
    
    if(aacEncOpen(handle, 0, channels) != AACENC_OK){
        MY_LOG(D_AUDIO, "aacEncOpen failure");
        return -1;
    }

    if (aacEncoder_SetParam(*handle, AACENC_AOT, aot) != AACENC_OK) {
        MY_LOG(D_AUDIO, "Unable to set the AOT\n");
        return -1;
    }
    if (aot == AOT_ER_AAC_ELD && eld_sbr) {
        if (aacEncoder_SetParam(*handle, AACENC_SBR_MODE, 1) != AACENC_OK) {
            MY_LOG(D_AUDIO, "Unable to set SBR mode for ELD\n");
            return -1;
        }
    }
    if (aacEncoder_SetParam(*handle, AACENC_SAMPLERATE, sample_rate) != AACENC_OK) {
        MY_LOG(D_AUDIO, "Unable to set the AOT\n");
        return -1;
    }
    if (aacEncoder_SetParam(*handle, AACENC_CHANNELMODE, MODE_1) != AACENC_OK) {
        MY_LOG(D_AUDIO, "Unable to set the channel mode\n");
        return -1;
    }
    if (aacEncoder_SetParam(*handle, AACENC_CHANNELORDER, 1) != AACENC_OK) {
        MY_LOG(D_AUDIO, "Unable to set the wav channel order\n");
        return -1;
    }
    if (vbr) {
        if (aacEncoder_SetParam(*handle, AACENC_BITRATEMODE, vbr) != AACENC_OK) {
            MY_LOG(D_AUDIO, "Unable to set the VBR bitrate mode\n");
            return -1;
        }
    } else {
        if (aacEncoder_SetParam(*handle, AACENC_BITRATE, bitrate) != AACENC_OK) {
            MY_LOG(D_AUDIO, "Unable to set the bitrate\n");
            return -1;
        }
    }
    if (aacEncoder_SetParam(*handle, AACENC_TRANSMUX, TT_MP4_ADTS  /*TT_MP4_ADTS TT_MP4_RAW*/) != AACENC_OK) {
        MY_LOG(D_AUDIO, "Unable to set the ADTS transmux\n");
        return -1;
    }//TT_MP4_RAW TT_MP4_LATM_MCP1 TT_MP4_LATM_MCP0 TT_MP4_LOAS is ok.
    if (aacEncoder_SetParam(*handle, AACENC_AFTERBURNER, afterburner) != AACENC_OK) {
        MY_LOG(D_AUDIO, "Unable to set the afterburner mode\n");
        return -1;
    }
    if (aacEncEncode(*handle, NULL, NULL, NULL, NULL) != AACENC_OK) {
        MY_LOG(D_AUDIO, "Unable to initialize the encoder\n");
        return -1;
    }
   
    return 0;
}

static uint8_t* pcm_buf = NULL;
static uint8_t* p_buf = NULL;
static uint16_t* convert_buf = NULL;

static int handle_code_data_aac(HANDLE_AACENCODER *handle, uint8_t* read_buf, int read_len, uint8_t* outdata) {
    int ret = -1;
    int count = 0;
    static int n_copybytes = 0;

    int n_refrmlen = 0;
    int input_size = 2048;

    AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
    AACENC_InArgs  in_args = { 0 };
    AACENC_OutArgs out_args = { 0 };
    int in_identifier = IN_AUDIO_DATA;
    int in_size = 0, in_elem_size = 0;
    int out_identifier = OUT_BITSTREAM_DATA;
    int out_size = 0, out_elem_size = 0;
    int i = 0;
    void* in_ptr = NULL, * out_ptr = NULL;
    uint8_t aac_buf[2048] = { 0 };
    AACENC_ERROR err;


//    if (pcm_buf == NULL || p_buf == NULL || convert_buf == NULL) {
//        pcm_buf = indata;
//        p_buf = pcm_buf;
//        convert_buf = (uint16_t*)codata;
//    }
  
    //_logErr("input_size=%d\n",input_size);
    while (1) {
    
        MY_LOG(D_AUDIO, "1read_len: %d, n_copybytes: %d, input_size: %d", read_len, n_copybytes, input_size);
        if (n_copybytes < input_size) {
            if (input_size - n_copybytes >= read_len) {
                memcpy(p_buf, read_buf, read_len);
                p_buf += read_len;
                n_copybytes += read_len;
                MY_LOG(D_AUDIO, "2read_len: %d, n_copybytes: %d, input_size: %d", read_len, n_copybytes, input_size);
            }
            else if (input_size - n_copybytes < read_len) {
                //_logMsg("n_pcm_bufsize - n_copybytes = %d\n", n_pcm_bufsize - n_copybytes);
                memcpy(p_buf, read_buf, input_size - n_copybytes);
                n_refrmlen = read_len - (input_size - n_copybytes);
                n_copybytes = input_size;
                MY_LOG(D_AUDIO, "3read_len: %d, n_copybytes: %d, input_size: %d", read_len, n_copybytes, input_size);
            }
             //pcm_buf==p_buf:AudioByffer ,  n_copybytes:AudioBufferLen
            //_logMsg("n_copybytes=%d , input_size=%d ,frm.len=%d\n",n_copybytes, input_size, frm.len);
        }

        if (n_copybytes == input_size) {
            //fdk-aac
            //1bytes conver 2bytes
            for (i = 0; i < input_size / 2; i++) {
                const uint8_t* in = &pcm_buf[2 * i];
                convert_buf[i] = in[0] | (in[1] << 8);
            }

            in_ptr = convert_buf;
            in_size = input_size;
            in_elem_size = 2;

            //input data
            in_args.numInSamples = in_size / 2;
            in_buf.numBufs = 1;
            in_buf.bufs = &in_ptr;
            in_buf.bufferIdentifiers = &in_identifier;
            in_buf.bufSizes = &in_size;
            in_buf.bufElSizes = &in_elem_size;


            //out put data
            out_ptr = aac_buf;
            out_size = sizeof(aac_buf);
            out_elem_size = 1;
            out_buf.numBufs = 1;
            out_buf.bufs = &out_ptr;
            out_buf.bufferIdentifiers = &out_identifier;
            out_buf.bufSizes = &out_size;
            out_buf.bufElSizes = &out_elem_size;
            if ((err = aacEncEncode(*handle, &in_buf, &out_buf, &in_args,
                &out_args)) != AACENC_OK) {
                if (err == AACENC_ENCODE_EOF) {
                    MY_LOG(D_AUDIO, "aacEncEncode, err == AACENC_ENCODE_EOF");
                    break;
                }

                MY_LOG(D_AUDIO, "handle_code_data_aac  Encoding failed \n");
                return -1;
            }
            

            if (in_size > 0 && in_size < input_size) {
                MY_LOG(D_AUDIO, "err:%d,in_size:%d,out_args.numOutBytes:%d\n", err, in_size,
                    out_args.numOutBytes);
            }
            //_logErr("out_args.numOutBytes: %d\n\n",out_args.numOutBytes); //debug
            memcpy(outdata, aac_buf, out_args.numOutBytes);
            count = out_args.numOutBytes;
            MY_LOG(D_AUDIO, "handle_code_data_aac  in_size: %d, count: %d ", in_size, count);

            p_buf = pcm_buf;
            n_copybytes = 0;
            if (n_refrmlen > 0) {
                memset(p_buf, 0, input_size);
                memcpy(p_buf, (uint8_t*)read_buf + (read_len - n_refrmlen), n_refrmlen);
                p_buf += n_refrmlen;
                n_copybytes += n_refrmlen;
                n_refrmlen = 0;
            }

            MY_LOG(D_AUDIO, "handle_code_data_aac break1");
            break;
        }
        else {
            MY_LOG(D_AUDIO, "handle_code_data_aac break2");
            break;
        }
           
      

    }

    return count;
}

int aac_encoder(HANDLE_AACENCODER* handle, uint8_t* read_buf, int read_len, uint8_t* outdata) {
    int ret = -1;
    int count = 0;

    AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
    AACENC_InArgs  in_args = { 0 };
    AACENC_OutArgs out_args = { 0 };
    int in_identifier = IN_AUDIO_DATA;
    int in_size = 0, in_elem_size = 0;
    int out_identifier = OUT_BITSTREAM_DATA;
    int out_size = 0, out_elem_size = 0;
    int i = 0;
    void* in_ptr = NULL, * out_ptr = NULL;
    uint8_t aac_buf[2048] = { 0 };  //fdk-aac编码器中的输出缓存不能传参进来的outdata，需用aac_buf
    uint16_t convert_buff[2048] = { 0 };
    AACENC_ERROR err;

       
    //fdk-aac
    //1bytes conver 2bytes
    for (i = 0; i < read_len / 2; i++) {
        const uint8_t* in = &read_buf[2 * i];
        convert_buff[i] = in[0] | (in[1] << 8);
    }

    in_ptr = convert_buff;
    in_size = read_len;
    in_elem_size = 2;

    //input data
    in_args.numInSamples = in_size / 2;
    in_buf.numBufs = 1;
    in_buf.bufs = &in_ptr;
    in_buf.bufferIdentifiers = &in_identifier;
    in_buf.bufSizes = &in_size;
    in_buf.bufElSizes = &in_elem_size;


    //out put data
    out_ptr = aac_buf;
    out_size = sizeof(aac_buf);
    out_elem_size = 1;
    out_buf.numBufs = 1;
    out_buf.bufs = &out_ptr;
    out_buf.bufferIdentifiers = &out_identifier;
    out_buf.bufSizes = &out_size;
    out_buf.bufElSizes = &out_elem_size;
    if ((err = aacEncEncode(*handle, &in_buf, &out_buf, &in_args, &out_args)) != AACENC_OK) {
        if (err == AACENC_ENCODE_EOF) {
            MY_LOG(D_AUDIO, "aacEncEncode, err == AACENC_ENCODE_EOF");
        }

        MY_LOG(D_AUDIO, "handle_code_data_aac  Encoding failed \n");
        return -1;
    }
    memcpy(outdata, aac_buf, out_args.numOutBytes);
    count = out_args.numOutBytes;
    //MY_LOG(D_AUDIO, "aac_encodeer  in_size: %d, count: %d ", in_size, count);
    
    return count;
}

int aac_encoder_encode(HANDLE_AACENCODER* handle, const int8_t* input, const int input_len, int8_t* output, int* output_len)
{
    AACENC_ERROR ret;

    if (!handle)
    {
        return AACENC_INVALID_HANDLE;
    }

    //if (input_len != handle->pcm_frame_len)
    //{
    //    printf("input_len = %d no equal to need length = %d\n", input_len, handle->pcm_frame_len);
    //    return AACENC_UNSUPPORTED_PARAMETER;            // 每次都按帧长的数据进行编码
    //}

    AACENC_BufDesc  out_buf = { 0 };
    AACENC_InArgs   in_args = { 0 };

    // pcm数据输入配置
    in_args.numInSamples = input_len / 2; // 所有通道的加起来的采样点数，每个采样点是2个字节所以/2

    // pcm数据输入配置
    int     in_identifier = IN_AUDIO_DATA;
    int     in_elem_size = 2;
    //void* in_ptr = input;
    int     in_buffer_size = input_len;
    AACENC_BufDesc  in_buf = { 0 };
    in_buf.numBufs = 1;
    in_buf.bufs = (void**)&input;
    in_buf.bufferIdentifiers = &in_identifier;
    in_buf.bufSizes = &in_buffer_size;
    in_buf.bufElSizes = &in_elem_size;

    // 编码数据输出配置
    int out_identifier = OUT_BITSTREAM_DATA;
    int out_elem_size = 1;
    void* out_ptr = output;
    int out_buffer_size = *output_len;
    out_buf.numBufs = 1;
    out_buf.bufs = &out_ptr;
    out_buf.bufferIdentifiers = &out_identifier;
    out_buf.bufSizes = &out_buffer_size;        //一定要可以接收解码后的数据
    out_buf.bufElSizes = &out_elem_size;

    AACENC_OutArgs  out_args = { 0 };

    if ((ret = aacEncEncode(*handle, &in_buf, &out_buf, &in_args, &out_args)) != AACENC_OK)
    {
        MY_LOG(D_AUDIO, "aacEncEncode ret = 0x%x, error is %s\n", ret, fdkaac_error(ret));

        return ret;
    }
    *output_len = out_args.numOutBytes;

    return AACENC_OK;
}


/** 
* 添加ADTS头部 这里只是为了以后rtmp拉流时存储aac文件做准备 
* 
* @param packet ADTS header 的 byte[]，长度为7 
* @param packetLen 该帧的长度，包括header的长度 
* @param profile 0-Main profile, 1-AAC LC，2-SSR 
* @param freqIdx 采样率 
                    0: 96000 Hz 
                    1: 88200 Hz 
                    2: 64000 Hz 
                    3: 48000 Hz 
                    4: 44100 Hz 
                    5: 32000 Hz 
                    6: 24000 Hz 
                    7: 22050 Hz 
                    8: 16000 Hz 
                    9: 12000 Hz 
                    10: 11025 Hz 
                    11: 8000 Hz 
                    12: 7350 Hz 
                    13: Reserved 
                    14: Reserved 
                    15: frequency is written explictly 
* @param chanCfg 通道 
*                   1：mono
                    2：L+R 
                    3：C+L+R */ 
void add_adts_to_packet(int8_t* packet, int packetLen, int profile, int freqIdx, int chanCfg) 
{ 
    /* 
    int profile = 2; // AAC LC 
    int freqIdx = 8; // 16000Hz 
    int chanCfg = 1; // 1 Channel 
    */ 
   

    packet[0] = 0xFF; 
    packet[1] = 0xF9; 
    //packet[2] = (((profile - 1) << 6) + (freqIdx << 2) + (chanCfg >> 2)); 
    //packet[3] = (((chanCfg & 3) << 6) + (packetLen >> 11)); 
    //packet[4] = ((packetLen & 0x7FF) >> 3); 
    //packet[5] = (((packetLen & 7) << 5) + 0x1F); 

    packet[2] = ((profile - 1) << 6) | ((freqIdx & 0x0F) << 2) | ((chanCfg & 0x04) >> 2);
    packet[3] = ((chanCfg & 0x03) << 6) | ((packetLen >> 11) & 0x03);
    packet[4] = (packetLen >> 3) & 0xFF;
    packet[5] = ((packetLen & 0x07) << 5) | 0x1F;

    packet[6] = 0xFC; 
    MY_LOG(D_AUDIO, "adts packetLen: %d|2:%x 3:%x 4:%x 5:%x", 
        packetLen, packet[2], packet[3], packet[4], packet[5]);
}


void* get_audio_stream_v32(void* argv)
{
    int ret = 0;
    int devID = 1;
    int chnID = 0;

    HANDLE_AACENCODER   hAacEncoder;
    AACENC_InfoStruct info = { 0 };

    //uint8_t adts_buf[7] = { 0 };
    uint8_t aac_buf[1024] = { 0 };
    int aac_len = 0;
    uint8_t read_buf[4096] = { 0 };
    int read_len = 0;
    int remain_len = 0;

    int64_t timeStamp = 0;


    if (aac_encodec_init(&hAacEncoder) != 0) {
        MY_LOG(D_AUDIO, "acc encodec init failure");
        return -1;
    }
    MY_LOG(D_AUDIO, "acc_encodec_init finish hAacEncoder=%d", hAacEncoder);

    if (aacEncInfo(hAacEncoder, &info) != AACENC_OK) {
        MY_LOG(D_AUDIO, "Unable to get the encoder info");
        return -1;
    }

    for (int k = 0; k < info.confSize; k++)
        MY_LOG(D_AUDIO, "confbuf%d:%x", k, info.confBuf[k]);

    int input_size = info.inputChannels * 2 * info.frameLength;
    //MY_LOG(D_AUDIO, "input_size=%d", input_size);

    //if (pcm_buf == NULL || p_buf == NULL || convert_buf == NULL) {
    //    pcm_buf = (uint8_t*)malloc(input_size);
    //    convert_buf = (int16_t*)malloc(input_size);
    //    p_buf = pcm_buf;
    //}
    //MY_LOG(D_AUDIO, "pcm_buf=%x", pcm_buf);

    //add_adts_to_packet(adts_buf, sizeof(adts_buf), 2, 8, 1);

    int64_t frame_timestamp, cur_ts;

    while (!isQuit) {
        /* Step 6: get audio record frame. */
        ret = IMP_AI_PollingFrame(devID, chnID, 1000);
        if (ret != 0) {
            MY_LOG(D_AUDIO, "Audio Polling Frame Data error");
            return NULL;
        }
        IMPAudioFrame frm;
        ret = IMP_AI_GetFrame(devID, chnID, &frm, BLOCK);
        if (ret != 0) {
            MY_LOG(D_AUDIO, "Audio Get Frame Data error");
            return NULL;
        }

        //MY_LOG(D_AUDIO, "1frm.len: %d", frm.len);

        memcpy(read_buf + read_len, frm.virAddr, frm.len);
        read_len +=  frm.len;
        

        if (read_len >= input_size)
        {
            //aac_len = handle_code_data_aac(&hAacEncoder, read_buf, read_len, aac_buf);
            aac_len = aac_encoder(&hAacEncoder, read_buf, read_len, aac_buf);
            //MY_LOG(D_AUDIO, "aac_len: %d", aac_len);
            //ret = aac_encoder_encode(&hAacEncoder, read_buf, input_size, aac_buf, &aac_len);
            //if (ret == AACENC_OK) {
            if (aac_len > 0) {
                //mylog_print_bin(aac_buf, aac_len);

                cur_ts = frm.timeStamp;
                if (last_audio_ts == 0) {
                    last_audio_ts = cur_ts / 1000;
                }
                frame_timestamp = cur_ts / 1000 - last_audio_ts;
                //MY_LOG(D_AUDIO, "cur_ts: %lld last_audio_ts: %lld, frame_timestamp: %lld", cur_ts, last_audio_ts, frame_timestamp);

                if (frame_timestamp < hold_audio_ts)
                    MY_LOG(D_VIDEO, "audio timestamp is error, hold_audio_ts: %lld", hold_audio_ts);
                hold_audio_ts = frame_timestamp;

                pthread_mutex_lock(&g_mutex);

                ret = wsrt_put_pkt(WS_STREAM_TYPE_AUDIO, true, (unsigned char*)aac_buf, aac_len, frame_timestamp);

                pthread_mutex_unlock(&g_mutex);
                if (ret < 0) {
                    MY_LOG(D_AUDIO, "----- put_pkt_data audio failure----");

                }

            }
            if (read_len > input_size)
            {
                memmove(read_buf, read_buf + input_size, read_len - input_size);
                remain_len = read_len - input_size;
                read_len = remain_len;
            }
            else
                read_len = 0;

            //MY_LOG(D_AUDIO, "remain_len: %d, read_len: %d", remain_len, read_len);
        }

        /* Step 8: release the audio record frame. */
        ret = IMP_AI_ReleaseFrame(devID, chnID, &frm);
        if (ret != 0) {
            MY_LOG(D_AUDIO, "Audio release frame data error\n");
            return;
        }

    }

    aacEncClose(&hAacEncoder);

    //free(convert_buf);
    //free(pcm_buf);

    //    fclose(aac_save);
    //    fclose(raw_save);

}


int audio_ai_init()
{
    int ret = 0;

    /* Step 1: set public attribute of AI device. */
    int devID = 1;
    IMPAudioIOAttr attr;
    attr.samplerate = AUDIO_SAMPLE_RATE_16000;
    attr.bitwidth = AUDIO_BIT_WIDTH_16;
    attr.soundmode = AUDIO_SOUND_MODE_MONO;
    attr.frmNum = 40;
    attr.numPerFrm = 960;
    attr.chnCnt = 1;
    MY_LOG(D_AUDIO, "samplerate=%d", attr.samplerate);
    ret = IMP_AI_SetPubAttr(devID, &attr);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "set ai %d attr err: %d\n", devID, ret);
        return -1;
    }

    memset(&attr, 0x0, sizeof(attr));
    ret = IMP_AI_GetPubAttr(devID, &attr);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "get ai %d attr err: %d\n", devID, ret);
        return -1;
    }

    MY_LOG(D_AUDIO, "Audio In GetPubAttr samplerate : %d", attr.samplerate);
    MY_LOG(D_AUDIO, "Audio In GetPubAttr   bitwidth : %d", attr.bitwidth);
    MY_LOG(D_AUDIO, "Audio In GetPubAttr  soundmode : %d", attr.soundmode);
    MY_LOG(D_AUDIO, "Audio In GetPubAttr     frmNum : %d", attr.frmNum);
    MY_LOG(D_AUDIO, "Audio In GetPubAttr  numPerFrm : %d", attr.numPerFrm);
    MY_LOG(D_AUDIO, "Audio In GetPubAttr     chnCnt : %d", attr.chnCnt);

    /* Step 2: enable AI device. */
    ret = IMP_AI_Enable(devID);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "enable ai %d err\n", devID);
        return -1;
    }

    /* Step 3: set audio channel attribute of AI device. */
    int chnID = 0;
    IMPAudioIChnParam chnParam;
    chnParam.usrFrmDepth = 20;  // 40;
    ret = IMP_AI_SetChnParam(devID, chnID, &chnParam);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "set ai %d channel %d attr err: %d\n", devID, chnID, ret);
        return -1;
    }

    memset(&chnParam, 0x0, sizeof(chnParam));
    ret = IMP_AI_GetChnParam(devID, chnID, &chnParam);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "get ai %d channel %d attr err: %d\n", devID, chnID, ret);
        return -1;
    }

    MY_LOG(D_AUDIO, "Audio In GetChnParam usrFrmDepth : %d", chnParam.usrFrmDepth);

    /* Step 4: enable AI channel. */
    ret = IMP_AI_EnableChn(devID, chnID);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "Audio Record enable channel failed\n");
        return -1;
    }

    /* Step 5: Set audio channel volume. */
    int chnVol = 60;
    ret = IMP_AI_SetVol(devID, chnID, chnVol);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "Audio Record set volume failed\n");
        return -1;
    }

    ret = IMP_AI_GetVol(devID, chnID, &chnVol);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "Audio Record get volume failed\n");
        return -1;
    }
    MY_LOG(D_AUDIO, "Audio In GetVol    vol : %d", chnVol);

    int aigain = 28;
    ret = IMP_AI_SetGain(devID, chnID, aigain);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "Audio Record Set Gain failed\n");
        return -1;
    }

    ret = IMP_AI_GetGain(devID, chnID, &aigain);
    if(ret != 0) {
        MY_LOG(D_AUDIO, "Audio Record Get Gain failed\n");
        return -1;
    }
    MY_LOG(D_AUDIO, "Audio In GetGain    gain : %d", aigain);

    return ret;
}


int do_business_threads()
{
    unsigned int i;
    int ret;
    pthread_t video_thread_id[FS_CHN_NUM];
    pthread_t audio_thread_id;
    pthread_t wrap_and_send_thead_id;

    //MY_LOG(D_AUDIO, "Start media wrap and send......");
    //ret = pthread_create(&wrap_and_send_thead_id, NULL, wrap_and_send, NULL);
    //if (ret != 0) {
    //    MY_LOG(D_ALL, "[ERROR] %s: pthread_create wrap and send failed\n", __func__);
    //    return -1;
    //}

    MY_LOG(D_VIDEO, "Start video stream......");
    for (i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable && (i == CHN_ENABLE)) {
            //ret = pthread_create(&video_thread_id[i], NULL, get_h264_stream_to_queue, &chn[i].index);
            ret = pthread_create(&video_thread_id[i], NULL, get_h264_stream, &chn[i].index);
            if (ret < 0) {
                MY_LOG(D_VIDEO, "Create Chn%d get_h264_stream", chn[i].index);
            }

        }
    }


    MY_LOG(D_AUDIO, "Start audio stream......");
    //ret = pthread_create(&audio_thread_id, NULL, get_audio_stream_to_queue, NULL);
    //ret = pthread_create(&audio_thread_id, NULL, get_audio_stream, NULL);
    ret = pthread_create(&audio_thread_id, NULL, get_audio_stream_v32, NULL);
    if (ret != 0) {
        MY_LOG(D_AUDIO, "[ERROR] %s: pthread_create Audio Record failed\n", __func__);
        return -1;
    }


    return 0;
}

int request_key_frame()
{
    int ret = 0;
    //设置首发关键帧
    if ((ret = IMP_Encoder_RequestIDR(CHN_ENABLE)) != 0)
        MY_LOG(D_VIDEO, "IMP_Encoder_RequestIDR failure");

    MY_LOG(D_VIDEO, "IMP_Encoder_RequestIDR success");

    return ret;
}

static int osd_show(void)
{
    int ret;

    ret = IMP_OSD_ShowRgn(prHander[0], grpNum, 1);
    if (ret != 0) {
        IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn() timeStamp error\n");
        return -1;
    }
   


    return 0;
}
static void* update_thread(void* p)
{
    int ret;

    /*generate time*/
    char DateStr[40];
    struct tm* currDate;
    unsigned i = 0, j = 0;
    void* dateData = NULL;
    uint32_t* data = p;
    IMPOSDRgnAttrData rAttrData;

    struct tm* ptm;
    char time_string[40];
    long milliseconds;
    struct timeval tv;


    ret = osd_show();
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "OSD show error\n");
        return NULL;
    }

  

    while (!isQuit) {
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
        rAttrData.picData.pData = data;
        IMP_OSD_UpdateRgnAttrData(prHander[0], &rAttrData);

        //sleep(1);
        usleep(90*1000);
    }
    free(data);

    return NULL;
}

IMPRgnHandle* sample_osd_init(int grpNum)
{
    int ret;
    IMPRgnHandle* prHander;
    IMPRgnHandle rHanderFont;
   

    prHander = malloc(sizeof(IMPRgnHandle));
    if (prHander <= 0) {
        IMP_LOG_ERR(TAG, "malloc() error !\n");
        return NULL;
    }

    rHanderFont = IMP_OSD_CreateRgn(NULL);
    if (rHanderFont == INVHANDLE) {
        IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn TimeStamp error !\n");
        return NULL;
    }



    ret = IMP_OSD_RegisterRgn(rHanderFont, grpNum, NULL);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
        return NULL;
    }



    IMPOSDRgnAttr rAttrFont;
    memset(&rAttrFont, 0, sizeof(IMPOSDRgnAttr));
    rAttrFont.type = OSD_REG_PIC;
    rAttrFont.rect.p0.x = 10;
    rAttrFont.rect.p0.y = 10;
    rAttrFont.rect.p1.x = rAttrFont.rect.p0.x + OSD_LETTER_NUM * OSD_REGION_WIDTH - 1;   //p0 is start，and p1 well be epual p0+width(or heigth)-1
    rAttrFont.rect.p1.y = rAttrFont.rect.p0.y + OSD_REGION_HEIGHT - 1;
#ifdef SUPPORT_RGB555LE
    rAttrFont.fmt = PIX_FMT_RGB555LE;
#else
    rAttrFont.fmt = PIX_FMT_BGRA;
#endif
    rAttrFont.data.picData.pData = NULL;
    ret = IMP_OSD_SetRgnAttr(rHanderFont, &rAttrFont);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr TimeStamp error !\n");
        return NULL;
    }

    IMPOSDGrpRgnAttr grAttrFont;

    if (IMP_OSD_GetGrpRgnAttr(rHanderFont, grpNum, &grAttrFont) < 0) {
        IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Logo error !\n");
        return NULL;

    }
    memset(&grAttrFont, 0, sizeof(IMPOSDGrpRgnAttr));
    grAttrFont.show = 0;

    /* Disable Font global alpha, only use pixel alpha. */
    grAttrFont.gAlphaEn = 1;
    grAttrFont.fgAlhpa = 0xff;
    grAttrFont.layer = 3;
    if (IMP_OSD_SetGrpRgnAttr(rHanderFont, grpNum, &grAttrFont) < 0) {
        IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Logo error !\n");
        return NULL;
    }


    ret = IMP_OSD_Start(grpNum);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IMP_OSD_Start TimeStamp, Logo, Cover and Rect error !\n");
        return NULL;
    }

    prHander[0] = rHanderFont;
    
    return prHander;
}

int sample_osd_exit(IMPRgnHandle* prHander, int grpNum)
{
    int ret;

    ret = IMP_OSD_ShowRgn(prHander[0], grpNum, 0);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close timeStamp error\n");
    }


    ret = IMP_OSD_UnRegisterRgn(prHander[0], grpNum);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn timeStamp error\n");
    }


    IMP_OSD_DestroyRgn(prHander[0]);
 
    ret = IMP_OSD_DestroyGroup(grpNum);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IMP_OSD_DestroyGroup(0) error\n");
        return -1;
    }
    free(prHander);
    prHander = NULL;

    return 0;
}

void Stop(int signo)
{
    printf("begin stop!!!\n");

    wsrt_close();

    isQuit = true;
    sample_framesource_streamoff();
    int ret = sample_osd_exit(prHander, grpNum);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "OSD exit failed\n");
        return -1;
    }
    sample_encoder_exit();
    sample_framesource_exit();
    sample_system_exit();

    wsrt_cleanup();

    printf("end stop!!!\n");

    _exit(0);

}

int main(int argc, char **argv)
{
    MY_LOG(D_ALL, "-------------begin------------");
	int ret = 0;
	int i = 0;
    long long last_time;
    
    signal(SIGINT, Stop);
    
    pthread_mutex_init(&g_mutex, NULL); 
  
	ret = loadconfig();
    if (0 != ret){
        MY_LOG(D_ALL, "load config failed!!!");
        return -1;
    }
    MY_LOG(D_VIDEO, "Load config finish");
    
	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
        MY_LOG(D_VIDEO, "IMP_System_Init() failed");
		return -1;
	}
    MY_LOG(D_VIDEO, "Step.1 System init complete");


    last_time = timeum();
    /* Step.2 FrameSource init */
	ret = sample_framesource_init();
	if (ret < 0) {
        MY_LOG(D_VIDEO, "FrameSource init failed\n");
		return -1;
	}
    MY_LOG(D_VIDEO, "Step.2 FrameSource init complete");

    
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_CreateGroup(chn[i].index);
			if (ret < 0) {
                MY_LOG(D_VIDEO, "IMP_Encoder_CreateGroup(%d) error !\n", i);
				return -1;
			}
		}
	}
    MY_LOG(D_VIDEO, "IMP_Encoder_CreateGroup complete");

	/* Step.3 Encoder init */
	ret = sample_encoder_init();
	if (ret < 0) {
        MY_LOG(D_VIDEO, "Encoder init failed\n");
		return -1;
	}
    MY_LOG(D_VIDEO, "Step.3 Encoder init complete");


    if (IMP_OSD_CreateGroup(grpNum) < 0) {
        IMP_LOG_ERR(TAG, "IMP_OSD_CreateGroup(%d) error !\n", grpNum);
        return -1;
    }

    /* Step.4 OSD init */
    prHander = sample_osd_init(grpNum);
    if (prHander <= 0) {
        IMP_LOG_ERR(TAG, "OSD init failed\n");
        return -1;
    }


  
   
	/* Step.4 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
            /* Step.5 Bind */
       

#ifdef ENABLE_OSD
            IMPCell osdcell = { DEV_ID_OSD, grpNum, 0 };
            ret = IMP_System_Bind(&chn[i].framesource_chn, &osdcell);
            if (ret < 0) {
                IMP_LOG_ERR(TAG, "Bind FrameSource channel0 and OSD failed\n");
                return -1;
            }

            ret = IMP_System_Bind(&osdcell, &chn[i].imp_encoder);
			//ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
                MY_LOG(D_VIDEO, "Bind FrameSource channel%d and Encoder failed\n",i);
				return -1;
			}
#else
            ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
            //ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
            if (ret < 0) {
                MY_LOG(D_VIDEO, "Bind FrameSource channel%d and Encoder failed\n", i);
                return -1;
            }
#endif
		}
	}
    MY_LOG(D_VIDEO, "Step.4 Bind complete");

#ifdef ENABLE_OSD
    /* Step.6 Create OSD bgramap update thread */
    pthread_t tid;
#ifdef SUPPORT_RGB555LE
    uint32_t* timeStampData = malloc(OSD_LETTER_NUM * OSD_REGION_HEIGHT * OSD_REGION_WIDTH * sizeof(uint16_t));
#else
    uint32_t* timeStampData = malloc(OSD_LETTER_NUM * OSD_REGION_HEIGHT * OSD_REGION_WIDTH * sizeof(uint32_t));
#endif
    if (timeStampData == NULL) {
        IMP_LOG_ERR(TAG, "valloc timeStampData error\n");
        return -1;
    }

    ret = pthread_create(&tid, NULL, update_thread, timeStampData);
    if (ret) {
        IMP_LOG_ERR(TAG, "thread create error\n");
        return -1;
    }
    IMP_FrameSource_SetFrameDepth(0, 0);
#endif

    /* Step.7 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
        MY_LOG(D_VIDEO, "ImpStreamOn failed");
		return -1;
	}
    MY_LOG(D_VIDEO, "Step.5 Stream On complete");

    /* Step.6 init audio device */
    ret = audio_ai_init();
    if (ret < 0) {
        MY_LOG(D_AUDIO, "init audio failed");
        return -1;
    }
    MY_LOG(D_AUDIO, "Step.5 init audio device complete");


#if 0
    wsrt_parameters_t av_params;
    av_params.type = WS_STREAM_BOTH;
    av_params.video_codec = WS_STREAM_CODEC_H264;
    av_params.audio_codec = WS_STREAM_CODEC_AAC_WITH_ADTS;
    av_params.audio_sample_rate = AUDIO_SAMPLE_RATE_16000;
    av_params.audio_channel_count = AUDIO_SOUND_MODE_MONO;

    wsrt_startup(&av_params, "srt://www.baozan.cloud:10080?streamid=#!::h=live/livestream,m=publish", "print", request_key_frame);
#else

    cJSON* root = cJSON_CreateObject();
    if (!root)
    {
        printf("Error before: [%s]\n", cJSON_GetErrorPtr());
        return -1;
    }

    cJSON* item = cJSON_CreateNumber(WS_STREAM_BOTH);
    cJSON_AddItemToObject(root, "stream_type", item);
    item = cJSON_CreateNumber(WS_STREAM_CODEC_H264);
    cJSON_AddItemToObject(root, "video_codec", item);
    item = cJSON_CreateNumber(WS_STREAM_CODEC_AAC_WITH_ADTS);
    cJSON_AddItemToObject(root, "audio_codec", item);
    item = cJSON_CreateNumber(AUDIO_SAMPLE_RATE_16000);
    cJSON_AddItemToObject(root, "audio_sample_rate", item);
    item = cJSON_CreateNumber(AUDIO_SOUND_MODE_MONO);
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
#endif

	

    ret = do_business_threads();
    if (0 != ret) {
        MY_LOG(D_ALL, "open business threads failure ");
        return -1;
    }
    MY_LOG(D_ALL, "Step.5 open business threads complete");

    while (!isQuit) {
        sleep(1);
    }
    MY_LOG(D_ALL, "-------------main Finish------------");

	return 0;
}

