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
#ifndef utils_h
#define utils_h

#include <stdio.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char g_log_mode[16];

long long get_timestamp();
//由struct timeval结构体数据（由gettimeofday获取到的）转换成可显示的时间字符串
char * get_local_time(char *time_str, int len);
void mylog_print(const char *fmts, ...);
//跟踪bin数据
int mylog_print_bin(unsigned char* buf, int len);

typedef enum {
    D_ALL               = 0x00,
    D_NONE              = 0x01,
    D_AUDIO             = 0x02,
    D_VIDEO             = 0x04
} enum_DEBUG_FOCUS;


#define LOG_FOCUS   D_ALL

static FILE* log_fd = NULL;
#define MY_LOG(dbg,fmt, ...)                                                                            \
            do{                                                                                         \
                char tmp[1024] = "";                                                                    \
                char time_str[64];                                                                      \
                                                                                                        \
                if (LOG_FOCUS == dbg || LOG_FOCUS == D_ALL){                                            \
                    if (0 == strcmp(g_log_mode, "file")){                                           \
                        sprintf(tmp, "[%s](%s|%d)" fmt "\n", get_local_time(time_str, sizeof(time_str)),\
                                                            __func__,__LINE__,##__VA_ARGS__);           \
                        if(log_fd == NULL)                                                              \
                            log_fd=fopen("./wcam.log", "at+");                                            \
                        fwrite(tmp, 1, strlen(tmp), log_fd);                                            \
                        fflush(log_fd);                                                                 \
                    }else if(0 == strcmp(g_log_mode, "print")){                                     \
                        printf("[%s](%s|%d)" fmt "\n", get_local_time(time_str, sizeof(time_str)),      \
                                                        __func__,__LINE__,##__VA_ARGS__);               \
                                                                                                        \
                    }                                                                                   \
                }                                                                                       \
            }while(0)

int parse_ts_data(unsigned char* buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* utils_h */
