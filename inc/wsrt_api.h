/*
版权所有 (c) 2022-2023
V 1.0
作者: 吴江旻 <1749385659@qq.com;15018916607>
日期: 2022年4月4日
描述: 封装SRT的API接口库函数

历史:
1、2022年4月4日
	初步创建

使用说明:

 
 */


#ifndef INC__SRT_API_H
#define INC__SRT_API_H

#include <string.h>
#include <stdint.h>

//using namespace std;


#define USE_WSRT_LIBRARY
 
#ifdef _WIN32
	#ifndef __MINGW__
		// Explicitly define 32-bit and 64-bit numbers
		typedef __int32 int32_t;
		typedef __int64 int64_t;
		typedef unsigned __int32 uint32_t;
		#ifndef LEGACY_WIN32
			typedef unsigned __int64 uint64_t;
		#else
			// VC 6.0 does not support unsigned __int64: may cause potential problems.
			typedef __int64 uint64_t;
		#endif

		#ifdef SDK_DYNAMIC
			#ifdef SDK_EXPORTS
				#define SDK_API __declspec(dllexport)
			#else
				#define SDK_API __declspec(dllimport)
			#endif
		#else
			#define SDK_API
		#endif
	#else // __MINGW__
		#define SDK_API
	#endif
#else
	#define SDK_API __attribute__ ((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif
	

	//音视频流类型定义
	typedef enum {
		WS_STREAM_TYPE_VIDEO = 0,
		WS_STREAM_TYPE_AUDIO,
		WS_STREAM_NO_VIDEO,
		WS_STREAM_NO_AUDIO,
		WS_STREAM_BOTH
	}wsrt_stream_type;

	//音视频编码格式定义
	typedef enum {
		WS_STREAM_CODEC_H264 = 0,
		WS_STREAM_CODEC_AAC_WITH_ADTS,
		WS_STREAM_CODEC_AAC,
		WS_STREAM_CODEC_H265,
		WS_STREAM_NO_CODEC
	}wsrt_stream_codec;

	//流媒体信息结构
	typedef struct {
		wsrt_stream_type	type;
		wsrt_stream_codec	video_codec;
		wsrt_stream_codec	audio_codec;
		uint32_t			audio_sample_rate;
		uint8_t				audio_channel_count;  // https://wiki.multimedia.cx/index.php?title=MPEG-4_Audio#Channel_Configurations
	}wsrt_parameters_t;


	
	//==================================================================
	//函 数 名：wsrt_key_frame
	//作    者：吴江旻   
	//日    期：2022/4/4 
	//功    能：请求关键帧回调函数
	//输入参数：无		
	//返 回 值：类型（int)
	//          0为成功，非0为失败
	//修改记录：
	//			
	//==================================================================
	typedef int (*wsrt_key_frame)();




	//==================================================================
	//函 数 名：wsrt_startup
	//作    者：吴江旻   
	//日    期：2022/4/4 
	//功    能：SDK库初始化函数
	//输入参数：
	//          params：类型(char*)，格式为json格式字符串：
	//					[{
	//						"stream_type"			:	"3",        //见wsrt_stream_type枚举定义，3表示WS_STREAM_NO_AUDIO
	//						"video_codec"			:	"0",        //见wsrt_stream_codec枚举定义，0表示WS_STREAM_CODEC_H264
	//						"audio_codec"			:	"1",		//见wsrt_stream_codec枚举定义，1表示WS_STREAM_CODEC_AAC_WITH_ADTS
	//						"audio_sample_rate"		:	"16000",	//音频采样率定义，单位Hz，取值范围有8000, 16000, 24000, 32000, 44100, 48000, 96000
	//						"audio_channel_count"	:	"1",		//音频声道模式定义，取值范围有1，2

	//						"uri"					:	"srt://www.baozan.cloud:10080?streamid=#!::h=live/livestream,m=publish", //SRT推流地址，支持域名解析，
	//						"display_log_mode"		:	"print",
	//						"key_frame_func"		:	""			//是请求关键帧回调函数指针地址
	//					}]
	//返 回 值：类型(int)
	//          0为成功，非0为失败
	//修改记录：
	//			2022/4/30	吴江旻修改输入参数为Json格式
	//==================================================================
	SDK_API		int wsrt_startup(const char* params);


	//==================================================================
	//函 数 名：wsrt_cleanup
	//作    者：吴江旻   
	//日    期：2022/4/4 
	//功    能：SDK库反初始化函数
	//输入参数：无		
	//返 回 值：类型（int)
	//          0为成功，非0为失败
	//修改记录：
	//			
	//==================================================================
	SDK_API		int wsrt_cleanup(void);

	
	//==================================================================
	//函 数 名：wsrt_cleanup
	//作    者：吴江旻   
	//日    期：2022/4/4 
	//功    能：输入音视频裸数据进行封装且发送函数
	//输入参数：
	//			type		：类型(wsrt_stream_type)。见wsrt_stream_type枚举定义，取值范围为WS_STREAM_TYPE_VIDEO、WS_STREAM_TYPE_AUDIO
	//			iskey		：类型(boolean)。视频关键帧为true。音频帧不区分是否关键帧，因此该值永远为true
	//			buff		：类型(unsigned char*)。存放一帧音视频裸数据的缓存开始地址
	//			len			：类型(int)。一帧音视频裸数据长度
	//			timestamp	：类型(int64_t)。获取音视频的时间戳，一般从编码器SDK获取。
	//返 回 值：类型（int)
	//          0为成功，非0为失败
	//修改记录：
	//			
	//==================================================================
	SDK_API		int wsrt_put_pkt(wsrt_stream_type type, bool iskey, unsigned char* buff, int len, int64_t timestamp);

	//==================================================================
	//函 数 名：wsrt_close
	//作    者：吴江旻   
	//日    期：2022/4/4 
	//功    能：关闭srt网络io线程函数
	//输入参数：无
	//返 回 值：类型（int)
	//          0为成功，非0为失败
	//修改记录：
	//			
	//==================================================================
	SDK_API		int wsrt_close();

#ifdef __cplusplus
}
#endif

#endif