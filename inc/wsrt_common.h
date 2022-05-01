#pragma once
#include <transmitmedia.hpp>
#include "ts_muxer.h"
#include "wsrt_api.h"

//保存ts包的缓存
typedef struct TS_BUFFER_
{
	void*				client;
	unique_ptr<Target>	tar;
	uint8_t*			cached_ts_packets_data;
	int					cached_ts_packets_num;
	int					cached_ts_packets_length;
} ts_buffer_t;


// SDK的环境变量
typedef struct SRT_CONTEXT_
{
	ts_muxer_t*			ts_muxer;
	av_stream_t*		stream_infos;
	ts_buffer_t*		ts_buffer;
	string				tar_uri;
	wsrt_key_frame		key_frame_cb;
	string				log_mode;
} srt_context_t;