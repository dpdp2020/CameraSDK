#include <srt.h>
#include <imp/imp_encoder.h>
#include <imp/imp_audio.h>
#include <list>
#include <sys/time.h>
#include <memory>
#include <utility>

#include "wsrt_api.h"
#include "utils.h"
#include "aacenc_lib.h"
#include "wsrt_common.h"
#include "cJSON.h"

using namespace std;

extern "C" {

    #define MYTS_MUXER_COUNT        2
    #define VIDEO_CODEC             IMP_ENC_PROFILE_AVC_MAIN
    #define TS_BUF_SIZE             1320 /* ts packet: 188 bytes, 7 ts packets: 1316 bytes */
    #define MYSRT_CHUNK_SIZE        1316    //1456
    #define TS_PKT_SZ               188


    static srt_context_t srt_context;
    static ts_buffer_t ts_context;
    static int audioPatPmtFreq = 0;
    static int isQuit = 0;
    static char g_log_mode[16];

    //==================================================================
    //�� �� ����mysrt_send
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ�srt���ͺ���
    //���������
    //          send_data   ������(unsigned char*)�����һ֡����Ƶ�����ݵĻ��濪ʼ��ַ
	//			send_length	������(int)��һ֡����Ƶ�����ݳ���
    //�� �� ֵ�����ͣ�int)
    //          0Ϊ�ɹ�����0Ϊʧ��
    //�޸ļ�¼��
    //			
    //==================================================================
    int mysrt_send(unsigned char* send_data, int send_length)
    {
        int ret = 0;

        size_t receivedBytes = 0;
        size_t wroteBytes = 0;
        size_t lostBytes = 0;
        size_t lastReportedtLostBytes = 0;
        int chunk_size = 0;
        unsigned char* tmp_ptr;

        // read a few chunks at a time in attempt to deplete
        // read buffers as much as possible on each read event
        // note that this implies live streams and does not
        // work for cached/file sources
        std::list<std::shared_ptr<bytevector>> dataqueue;
        while (receivedBytes < send_length)
        {
            std::shared_ptr<bytevector> pdata(new bytevector(MYSRT_CHUNK_SIZE));

            chunk_size = send_length - receivedBytes;
            chunk_size = (chunk_size <= MYSRT_CHUNK_SIZE) ? chunk_size : MYSRT_CHUNK_SIZE;
            pdata->resize(chunk_size);

            memcpy((*pdata).data(), send_data + receivedBytes, chunk_size);
            //        MY_LOG("pdata:%d |%02x %02x %02x %02x %02x %02x %02x %02x",chunk_size,
            //                    (*pdata)[0], (*pdata)[1], (*pdata)[2], (*pdata)[3],
            //                    (*pdata)[4], (*pdata)[5], (*pdata)[6], (*pdata)[7]);
            if (pdata->empty())
            {
                break;
            }

            dataqueue.push_back(pdata);
            receivedBytes += (*pdata).size();
        }

        // if no target, let received data fall to the floor
        while (!dataqueue.empty())
        {
            //pthread_mutex_lock(&g_mutex);
            std::shared_ptr<bytevector> pdata = dataqueue.front();
            if (!ts_context.tar.get() || !ts_context.tar->IsOpen()) {
                lostBytes += (*pdata).size();
                //MY_LOG(D_VIDEO, "closed srt connection: %d", pdata->size());
                
            }
            else if (!ts_context.tar->Write(pdata->data(), pdata->size(), cout)) {
                lostBytes += (*pdata).size();
                MY_LOG(D_VIDEO, "lostBytes: %d", pdata->size());
                //mylog_print("lostBytes: %d", pdata->size());
            }
            else {

                parse_ts_data((uint8_t*)pdata->data(), (int)pdata->size());
                //wroteBytes += (*pdata).size();
    //            tmp_ptr = (unsigned char*)(pdata->data());
    //            MY_LOG("wroteBytes: %d[%02x %02x %02x %02x %02x %02x %02x %02x]",pdata->size(),
    //                      tmp_ptr[0], tmp_ptr[1], tmp_ptr[2], tmp_ptr[3],
    //                      tmp_ptr[4], tmp_ptr[5], tmp_ptr[6], tmp_ptr[7]);
            }
            dataqueue.pop_front();
            //pthread_mutex_unlock(&g_mutex);
        }

        return ret;

    }

    //ÿ7��ts��һ����srt_client send
    //==================================================================
    //�� �� ����send_ts_packets
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ�ÿ7��TS��װ���ϲ����ͺ���
    //���������
    //          opaque      ������(void*)������Ƶ������У���ts_buffer_t�ṹ���塣
    //          buf		    ������(uint8_t*)�����һ֡����Ƶ�����ݵĻ��濪ʼ��ַ��
    //			buf_size	������(size_t)��һ֡����Ƶ�����ݳ��ȡ�
    //          type        ������(E_STREAM_TYPE)������buf��ŵ�����Ƶ����Ƶ�����ͣ���E_STREAM_TYPEö�ٶ��塣
    //�� �� ֵ�����ͣ�int)
    //          0Ϊ�ɹ�����0Ϊʧ�ܡ�
    //�޸ļ�¼��
    //			
    //==================================================================
    int send_ts_packets(void* opaque, const uint8_t* buf, size_t buf_size, E_STREAM_TYPE type)
    {
        int i = 0;
        ts_buffer_t* ts_buf = (ts_buffer_t*)opaque;

        int offset = ts_buf->cached_ts_packets_num * TS_PKT_SZ;

        //MY_LOG(D_ALL,"cached_ts_packets_num:%d offset:%d buf_size:%d\n",ts_buf->cached_ts_packets_num,offset,buf_size);
        //��ֹ���, cached_ts_packets_num Ӧ��0~send_ts_packets
        if (ts_buf->cached_ts_packets_num < 7)
        {
            memcpy(ts_buf->cached_ts_packets_data + offset, buf, buf_size);
        }

        ts_buf->cached_ts_packets_num += 1;

        if (7 > ts_buf->cached_ts_packets_num) {
            //        MY_LOG("only cache ts packets:%d", ts_buf->cached_ts_packets_num);
            return 0;
        }

        int size = ts_buf->cached_ts_packets_num * TS_PKT_SZ;
        int res = mysrt_send(ts_buf->cached_ts_packets_data, size);

        //ִ����send,Ӧ�����cached_ts_packets_data �� cached_ts_packets_num
        memset(ts_buf->cached_ts_packets_data, 0, TS_BUF_SIZE);
        ts_buf->cached_ts_packets_num = 0;

        if (res < 0) {
            MY_LOG(D_VIDEO, "Srt Writer Error");
            return -1;
        }


        return 0;
    }


    //==================================================================
    //�� �� ����get_stream_codec
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ��ӿ�����Ƶ����������TS��װ�ı�������ת������
    //���������
    //          stream_codec�����ͣ�wsrt_stream_codec������Ƶ����Ƶ����ö�ٲ�������wsrt_stream_codecö�ٶ��塣
    //�� �� ֵ�����ͣ�av_stream_codec_t)
    //          ����TS�������Ƶ����Ƶ����ö��ֵ��
    //�޸ļ�¼��
    //			
    //==================================================================
    av_stream_codec_t get_stream_codec(wsrt_stream_codec stream_codec)
    {
        switch (stream_codec)
        {
            
            case    WS_STREAM_CODEC_H264:
                return AV_STREAM_CODEC_H264;
            case    WS_STREAM_CODEC_AAC_WITH_ADTS:
                return AV_STREAM_CODEC_AAC_WITH_ADTS;
            case     WS_STREAM_CODEC_AAC:
                return AV_STREAM_CODEC_AAC;
            case      WS_STREAM_CODEC_H265:
                return AV_STREAM_CODEC_H265;
            case      WS_STREAM_NO_CODEC:
            default:
                return AV_STREAM_NO_CODEC;
        }
    }

    //==================================================================
    //�� �� ����get_stream_type
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ��ӿ���������TS��װ��������ת������
    //�����������
    //          stream_type�����ͣ�wsrt_stream_type������Ƶ����Ƶ����ö�ٲ�������wsrt_stream_typeö�ٶ��塣
    //�� �� ֵ�����ͣ�av_stream_type_t)
    //          ����TS�������Ƶ����Ƶö��ֵ��
    //�޸ļ�¼��
    //			
    //==================================================================
    av_stream_type_t get_stream_type(wsrt_stream_type stream_type)
    {
        
        switch (stream_type)
        {

        case    WS_STREAM_TYPE_VIDEO:
            return AV_STREAM_TYPE_VIDEO;

        case    WS_STREAM_TYPE_AUDIO:
           
        default:
            return AV_STREAM_TYPE_AUDIO;
        }
    }


    //==================================================================
    //�� �� ����mpegts_init
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ���װTS��ʼ������
    //�����������
    //          av_params�����ͣ�wsrt_parameters_t��������Ƶ�����������wsrt_parameters_t�ṹ���塣
    //�� �� ֵ�����ͣ�int)
    //          0Ϊ�ɹ�����0Ϊʧ��
    //�޸ļ�¼��
    //			
    //==================================================================
    int mpegts_init(wsrt_parameters_t* av_params)
    {
        int ret;

        if (NULL == srt_context.ts_muxer)
        {
            //��ʼ����Ƶ�������
            if (av_params->type == WS_STREAM_BOTH)
            {
                srt_context.stream_infos = (av_stream_t*)malloc(sizeof(av_stream_t) * MYTS_MUXER_COUNT);
                memset(srt_context.stream_infos, 0, sizeof(av_stream_t) * MYTS_MUXER_COUNT);

                srt_context.stream_infos[0].type = AV_STREAM_TYPE_VIDEO;
                srt_context.stream_infos[0].codec = get_stream_codec(av_params->video_codec);
                srt_context.stream_infos[1].type = AV_STREAM_TYPE_AUDIO;
                srt_context.stream_infos[1].codec = get_stream_codec(av_params->audio_codec);    // AV_STREAM_CODEC_AAC;    AV_STREAM_CODEC_AAC_WITH_ADTS
                srt_context.stream_infos[1].audio_channel_count = av_params->audio_channel_count;
                srt_context.stream_infos[1].audio_sample_rate = av_params->audio_sample_rate;
                srt_context.stream_infos[1].audio_object_type = AOT_AAC_LC;
            }
            else
            {
                srt_context.stream_infos = (av_stream_t*)malloc(sizeof(av_stream_t));
                memset(srt_context.stream_infos, 0, sizeof(av_stream_t));

                srt_context.stream_infos[0].type = get_stream_type(av_params->type);
                if (av_params->type == WS_STREAM_NO_VIDEO)
                {
                    srt_context.stream_infos[0].type = AV_STREAM_TYPE_AUDIO;
                    srt_context.stream_infos[0].codec = get_stream_codec(av_params->audio_codec);
                }
                else if (av_params->type == WS_STREAM_NO_AUDIO)
                {
                    srt_context.stream_infos[0].type = AV_STREAM_TYPE_VIDEO;
                    srt_context.stream_infos[0].codec = get_stream_codec(av_params->video_codec);
                }
                    
                srt_context.stream_infos[0].audio_channel_count = av_params->audio_channel_count;
                srt_context.stream_infos[0].audio_sample_rate = av_params->audio_sample_rate;
                srt_context.stream_infos[0].audio_object_type = AOT_AAC_LC;
            }

       
            srt_context.ts_buffer = (ts_buffer_t*)malloc(sizeof(ts_buffer_t));
            if (NULL == srt_context.ts_buffer)
            {
                MY_LOG(D_VIDEO, "Faile to alloc target_t");
                return -1;
            }

            srt_context.ts_buffer->cached_ts_packets_data = (uint8_t*)malloc(TS_BUF_SIZE);
            if (NULL == srt_context.ts_buffer->cached_ts_packets_data)
            {
                MY_LOG(D_VIDEO, "Failed to alloc cached_ts_packets_data\n");
                return -1;
            }

            srt_context.ts_buffer->cached_ts_packets_num = 0;
            srt_context.ts_buffer->client = NULL;

            srt_context.ts_muxer = new_ts_muxer(srt_context.stream_infos, MYTS_MUXER_COUNT);
            if (NULL == srt_context.ts_muxer)
            {
                MY_LOG(D_VIDEO, "Failed to alloc m_ts_muxer");
                return -1;
            }

            ret = ts_muxer_set_avio_context(srt_context.ts_muxer, srt_context.ts_buffer, send_ts_packets);
            if (ret)
            {
                MY_LOG(D_VIDEO, "Failed to run ts_muxer_set_avio_context\n");
                return -1;
            }

            MY_LOG(D_ALL, "mpegts_init");
            ret = ts_muxer_write_header(srt_context.ts_muxer, E_STREAM_TYPE_HEADER);
            if (ret)
            {
                MY_LOG(D_VIDEO, "Failed to run ts_muxer_write_header\n");
                return -1;
            }

        }

        return 0;
    }


    //==================================================================
    //�� �� ����mysrt_waiting_connect
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ�����SRT�����߳�
    //�����������
    //�� �� ֵ�����ͣ�void*)
    //          ����NULL
    //�޸ļ�¼��
    //			
    //==================================================================
    void* mysrt_waiting_connect(void* args)
    {
        bool tarConnected = false;
        size_t wroteBytes = 0;
        size_t lostBytes = 0;
        size_t lastReportedtLostBytes = 0;
        std::time_t writeErrorLogTimer(std::time(nullptr));
      
        int pollid = srt_epoll_create();
        if (pollid < 0)
        {
            MY_LOG(D_ALL, "Can't initialize epoll");
            return NULL;
        }
        MY_LOG(D_VIDEO, "create srt epoll pollid=%d", pollid);
Reconnect:
        try {
            while (!isQuit) {

                if (!ts_context.tar.get())
                {
                    MY_LOG(D_ALL, "Ready Connection: %s", srt_context.tar_uri.c_str());
                    //tar = Target::Create("srt://192.168.1.102:10080?streamid=#!::h=live/livestream,m=publish");
                    //tar = Target::Create("srt://:10080");
                    ts_context.tar = Target::Create(srt_context.tar_uri);
                    if (!ts_context.tar.get())
                    {
                        MY_LOG(D_ALL, "Unsupported target type");
                        return NULL;
                    }
                    

                    // IN because we care for state transitions only
                    // OUT - to check the connection state changes
                    int events = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                    switch (ts_context.tar->uri.type())
                    {
                    case UriParser::SRT:

                        if (srt_epoll_add_usock(pollid, ts_context.tar->GetSRTSocket(), &events))
                        {
                            MY_LOG(D_ALL, "Failed to add SRT destination to poll, %d", ts_context.tar->GetSRTSocket());
                            return NULL;
                        }
                        break;
                    default:
                        break;
                    }

                }

                int srtrfdslen = 2;
                int srtwfdslen = 2;
                SRTSOCKET srtrwfds[4] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK , SRT_INVALID_SOCK , SRT_INVALID_SOCK };
                int sysrfdslen = 2;
                SYSSOCKET sysrfds[2];
                if (srt_epoll_wait(pollid,
                    &srtrwfds[0], &srtrfdslen, &srtrwfds[2], &srtwfdslen,
                    100,
                    &sysrfds[0], &sysrfdslen, 0, 0) >= 0)
                {
                    bool doabort = false;
                    for (size_t i = 0; i < sizeof(srtrwfds) / sizeof(SRTSOCKET); i++)
                    {
                        SRTSOCKET s = srtrwfds[i];
                        if (s == SRT_INVALID_SOCK)
                            continue;

                        //bool issource = false;
                        if (ts_context.tar && ts_context.tar->GetSRTSocket() != s)
                            continue;


                        SRT_SOCKSTATUS status = srt_getsockstate(s);
                        switch (status)
                        {
                        case SRTS_LISTENING:
                        {

                            const bool res = ts_context.tar->AcceptNewClient();
                            if (!res)
                            {
                                MY_LOG(D_ALL, "Failed to accept SRT connection");
                                doabort = true;
                                break;
                            }

                            srt_epoll_remove_usock(pollid, s);

                            SRTSOCKET ns = ts_context.tar->GetSRTSocket();
                            int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                            if (srt_epoll_add_usock(pollid, ns, &events))
                            {
                                MY_LOG(D_VIDEO, "Failed to add SRT client to poll, %d", ns);
                                doabort = true;
                            }
                            else
                            {
                                MY_LOG(D_VIDEO, "Accepted SRT connection");

                                if (srt_context.key_frame_cb)
                                {
                                    if (srt_context.key_frame_cb() != 0)
                                        MY_LOG(D_VIDEO, "Request key frame failure");
                                    MY_LOG(D_VIDEO, "Request key frame success");
                                }
                                
                                tarConnected = true;
                            }
                        }
                        break;
                        case SRTS_BROKEN:
                        case SRTS_NONEXIST:
                        case SRTS_CLOSED:
                        {
                            
                            if (tarConnected)
                            {
                                MY_LOG(D_VIDEO, "SRT target disconnected");
                                tarConnected = false;
                            }
                            //��Ҫ��������
                            srt_epoll_remove_usock(pollid, s);
                            ts_context.tar.reset();
                        }
                        break;
                        case SRTS_CONNECTED:
                        {
                            if (!tarConnected)
                            {
                                MY_LOG(D_VIDEO, "SRT target connected");
                                tarConnected = true;
                                if (ts_context.tar->uri.type() == UriParser::SRT)
                                {
                                   
                                    const int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                                    // Disable OUT event polling when connected
                                    if (srt_epoll_update_usock(pollid, ts_context.tar->GetSRTSocket(), &events))
                                    {
                                        MY_LOG(D_VIDEO, "Failed to add SRT destination to poll, %d", ts_context.tar->GetSRTSocket());
                                        return NULL;
                                    }
                                }

                                MY_LOG(D_ALL, "request key frame addr=%x", srt_context.key_frame_cb);
                                //�����׷��ؼ�֡
                                if (srt_context.key_frame_cb) 
                                {
                                    if (srt_context.key_frame_cb() != 0)
                                        MY_LOG(D_VIDEO, "Requst key frame failure");
                                }
                                

                            }
                        }

                        default:
                        {
                            // No-Op
                        }
                        break;
                        }
                    }
                    

                    if (doabort)
                    {
                        MY_LOG(D_ALL, "do abort");
                        break;
                    }
                }
            }
        }
        catch (std::exception& x)
        {
            MY_LOG(D_ALL, "ERROR: %s", x.what());
           
            char* rel = strstr(x.what(), "connection time out"); //�״γ��ֵ�ַ��strstr�������ddabc
            if (rel != NULL)
            {
                if (!isQuit)
                    goto Reconnect;
            }
           
            MY_LOG(D_ALL, "mysrt_waiting_connect exception");
            return NULL;
        }

        MY_LOG(D_ALL, "mysrt_waiting_connect exit");

        return NULL;
    }


    //==================================================================
    //�� �� ����mysrt_init_connect
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ���������SRT�����̺߳���
    //�����������
    //�� �� ֵ������(int)
    //          0Ϊ�ɹ�����0Ϊʧ��
    //�޸ļ�¼��
    //			
    //==================================================================
    int mysrt_init_connect()
    {
        int ret;
        pthread_t tid;

        srt_startup();

        ret = pthread_create(&tid, 0, mysrt_waiting_connect, NULL);
        if (ret < 0) {
            MY_LOG(D_ALL, "Create thread srt failure");
            return ret;
        }
        //pthread_join(tid, NULL);

        return 0;
    }

    //==================================================================
    //�� �� ����wsrt_startup
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ�SDK���ʼ������
    //���������
    //          params������(char*)����ʽΪjson��ʽ�ַ�����
    //					[{
    //						"stream_type"			:	"3",        //��wsrt_stream_typeö�ٶ��壬3��ʾWS_STREAM_NO_AUDIO
    //						"video_codec"			:	"0",        //��wsrt_stream_codecö�ٶ��壬0��ʾWS_STREAM_CODEC_H264
    //						"audio_codec"			:	"1",		//��wsrt_stream_codecö�ٶ��壬1��ʾWS_STREAM_CODEC_AAC_WITH_ADTS
    //						"audio_sample_rate"		:	"16000",	//��Ƶ�����ʶ��壬��λHz��ȡֵ��Χ��8000, 16000, 24000, 32000, 44100, 48000, 96000
    //						"audio_channel_count"	:	"1",		//��Ƶ����ģʽ���壬ȡֵ��Χ��1��2
    //						"uri"					:	"srt://www.baozan.cloud:10080?streamid=#!::h=live/livestream,m=publish", //SRT������ַ��֧������������
    //						"display_log_mode"		:	"print",
    //						"key_frame_func"		:	""			//������ؼ�֡�ص�����ָ���ַ
    //					}]
    //�� �� ֵ������(int)
    //          0Ϊ�ɹ�����0Ϊʧ��
    //�޸ļ�¼��
    //			2022/4/30	�⽭�F�޸��������ΪJson��ʽ
    //==================================================================
    int wsrt_startup(const char* params)
    {
        srt_context.stream_infos = NULL;
        srt_context.ts_muxer = NULL;
        srt_context.ts_buffer = NULL;
        srt_context.key_frame_cb = NULL;
       

        cJSON* root = cJSON_Parse(params);
        if (root == NULL)
        {
            MY_LOG(D_ALL, "the parameters is error:\n%s", params);
            return -1;
        }
       
        wsrt_parameters_t av_params;
        cJSON* stream_type = cJSON_GetObjectItem(root, "stream_type"); 
        cJSON* video_codec = cJSON_GetObjectItem(root, "video_codec");
        cJSON* audio_codec = cJSON_GetObjectItem(root, "audio_codec");
        cJSON* audio_sample_rate = cJSON_GetObjectItem(root, "audio_sample_rate");
        cJSON* audio_channel_count = cJSON_GetObjectItem(root, "audio_channel_count");
        cJSON* connect_uri = cJSON_GetObjectItem(root, "uri");
        cJSON* display_log_mode = cJSON_GetObjectItem(root, "display_log_mode");
        cJSON* key_frame_func = cJSON_GetObjectItem(root, "key_frame_func");

        av_params.type = stream_type->valueint;
        av_params.video_codec = video_codec->valueint;
        av_params.audio_codec = audio_codec->valueint;
        av_params.audio_sample_rate = audio_sample_rate->valueint;
        av_params.audio_channel_count = audio_channel_count->valueint;

        srt_context.tar_uri = connect_uri->valuestring;
        srt_context.log_mode = display_log_mode->valuestring;
        strcpy(g_log_mode, display_log_mode->valuestring);
        srt_context.key_frame_cb = (wsrt_key_frame) key_frame_func->valueint;

        cJSON_Delete(root);


        //��������ؼ�֡�ص��ӿ�
        //srt_context.key_frame_cb = key_frame_cb;
        MY_LOG(D_ALL, "key_frame_cb func=%x", srt_context.key_frame_cb);

        int ret = mysrt_init_connect();
        if (ret < 0) {
            MY_LOG(D_VIDEO, "init srt failure");
            return -1;
        }
        MY_LOG(D_VIDEO, "srt init finish");

        ret = mpegts_init(&av_params);
        if (ret < 0) {
            MY_LOG(D_VIDEO, "init srt failure");
            return -1;
        }
        MY_LOG(D_VIDEO, "mpegts init finish");
        return ret;
    }

    void mpegts_cleanup()
    {
        if (srt_context.stream_infos)
        {
            free(srt_context.stream_infos);
            srt_context.stream_infos = NULL;
        }

        if (srt_context.ts_muxer)
        {
            free_ts_muxer(srt_context.ts_muxer);
            srt_context.ts_muxer = NULL;
        }

        if (srt_context.ts_buffer)
        {
            if (srt_context.ts_buffer->cached_ts_packets_data)
            {
                free(srt_context.ts_buffer->cached_ts_packets_data);
                srt_context.ts_buffer->cached_ts_packets_data = NULL;
            }
            free(srt_context.ts_buffer);
            srt_context.ts_buffer = NULL;
        }
    }
    //==================================================================
    //�� �� ����wsrt_cleanup
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ�SDK�ⷴ��ʼ������
    //�����������		
    //�� �� ֵ�����ͣ�int)
    //          0Ϊ�ɹ�����0Ϊʧ��
    //�޸ļ�¼��
    //			
    //==================================================================
    int wsrt_cleanup()
    { 
        mpegts_cleanup();
        srt_cleanup();

        return 0;
    }

    //==================================================================
    //�� �� ����wsrt_cleanup
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ���������Ƶ�����ݽ��з�װ�ҷ��ͺ���
    //���������
    //			type		������(wsrt_stream_type)����wsrt_stream_typeö�ٶ��壬ȡֵ��ΧΪWS_STREAM_TYPE_VIDEO��WS_STREAM_TYPE_AUDIO
    //			iskey		������(boolean)����Ƶ�ؼ�֡Ϊtrue����Ƶ֡�������Ƿ�ؼ�֡����˸�ֵ��ԶΪtrue
    //			buff		������(unsigned char*)�����һ֡����Ƶ�����ݵĻ��濪ʼ��ַ
    //			len			������(int)��һ֡����Ƶ�����ݳ���
    //			timestamp	������(int64_t)����ȡ����Ƶ��ʱ�����һ��ӱ�����SDK��ȡ��
    //�� �� ֵ�����ͣ�int)
    //          0Ϊ�ɹ�����0Ϊʧ��
    //�޸ļ�¼��
    //			
    //==================================================================
    int wsrt_put_pkt(wsrt_stream_type type, bool iskey, unsigned char* buff, int len, int64_t timestamp)
    {
        int ret = 0;

        //int64_t cur_ts = timestamp;
        timestamp = timestamp * 90;
        //MY_LOG(D_ALL, "2put timestamp=%lld last_ts=%lld", timestamp, last_ts);

        if (NULL == srt_context.ts_muxer) {
            MY_LOG(D_ALL, "st_muxer handle is NULL");
            return -1;
        }

        av_packet_t pkt;
        memset(&pkt, 0, sizeof(av_packet_t));
        //MY_LOG(D_ALL, "pts: %lld, len: %d, type: %d |%x %x %x %x %x %x %x %x %x %x", 
        //    timestamp + 63000, len, type, buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], buff[6], buff[7], buff[8], buff[9]);

        pkt.data = (uint8_t*)buff;
        pkt.size = len;
        pkt.dts = NOPTS_VALUE;
        pkt.pts = (int64_t)timestamp;
        if (iskey)
        {
            pkt.flags |= AV_PACKET_FLAGS_KEY;

        }

        if (WS_STREAM_TYPE_VIDEO == type)
        {
            pkt.av_stream_index = 0;
        }
        else if (WS_STREAM_TYPE_AUDIO == type)
        {
            pkt.av_stream_index = 1;
        }
        else
        {
            MY_LOG(D_ALL, "stream_index invalid");
            return -1;
        }

        //pthread_mutex_lock(&g_mutex);
        if (WS_STREAM_TYPE_VIDEO == type && iskey)
        {
            //MY_LOG(D_VIDEO, "put video data");
            // ����I֡�������PAT/PMT
            ts_muxer_write_header(srt_context.ts_muxer, E_STREAM_TYPE_VIDEO);
        }
        else if (WS_STREAM_TYPE_AUDIO == type)
        {
            //MY_LOG(D_VIDEO, "put audio data");
            // ����Ƶ����ÿ30֡����PAT/PMT
            if (0 == (audioPatPmtFreq++ % 30)) {
                ts_muxer_write_header(srt_context.ts_muxer, E_STREAM_TYPE_AUDIO);
            }
        }

        ret = ts_muxer_write_packet(srt_context.ts_muxer, &pkt); //���뽫pkt�����ݴ���pes->payload,׼������
        if (ret)
        {
            //pthread_mutex_unlock(&g_mutex);
            MY_LOG(D_ALL, "ts_muxer_write_packet failed(%d) %d\n", __LINE__, ret);
            return ret;
        }
        //pthread_mutex_unlock(&g_mutex);

        return ret;
    }

    //==================================================================
    //�� �� ����wsrt_close
    //��    �ߣ��⽭�F   
    //��    �ڣ�2022/4/4 
    //��    �ܣ��ر�srt����io�̺߳���
    //�����������
    //�� �� ֵ�����ͣ�int)
    //          0Ϊ�ɹ�����0Ϊʧ��
    //�޸ļ�¼��
    //			
    //==================================================================
    int wsrt_close()
    {
        isQuit = 1;
        return 0;

    }

}