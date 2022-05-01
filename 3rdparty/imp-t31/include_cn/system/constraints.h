/*
 * IMP Capacity constraints definition header file.
 *
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 * Author: Zoro <ykli@ingenic.cn>
 */

#ifndef __CONSTRAINTS_H__
#define __CONSTRAINTS_H__

/**
 * @file
 * IMP系统限制头文件
 */

/* Common */
#define MAX_NAME_LEN				32					/**< Name字符串最大长度 */
#define MAX_ENCODER_BS_BUFFER_WIDTH				1920
#define MAX_ENCODER_BS_BUFFER_HEIGHT			1080

/* System-DGM */
#define NR_MAX_GROUPS_IN_DEV		6					/**< 每个Device的最大Group数 */
#define NR_MAX_OUTPUT_IN_GROUP		3					/**< 每个Group的最大Output数 */

/* FrameSource */
#define NR_MAX_FS_GROUPS			5					/**< FrameSource的最大Group数 */
#define NR_MAX_FS_CHNS				NR_MAX_FS_GROUPS
#define NR_MAX_FS_OUTPUT_IN_GROUP	3					/**< FrameSource的最大输出Channel数 */
#define NR_MAX_FS_DEPTH				2					/**< FrameSource的最大缓存帧数 */

/* IVS */
#define NR_MAX_IVS_GROUPS			1					/**< IVS的最大Group数 */
#define NR_MAX_IVS_CHN_IN_GROUP		64					/**< IVS的最大输出Channel数 */
#define NR_MAX_IVS_CHANNEL			(NR_MAX_IVS_GROUPS * NR_MAX_IVS_CHN_IN_GROUP)		/**< IVS的最大Channel数 */

/* Encoder */
#define NR_MAX_ENC_GROUPS			6					/**< Encoder的最大Group数 */
#define NR_MAX_ENC_CHN_IN_GROUP		2					/**< Encoder中每个Group可注册的Channel数 */
#define NR_MAX_ENC_CHN				6					/**< Encoder的最大Channel数 */
#define NR_MAX_ENC_CHN_STREAM		3				/**< Encoder中每个Channel的输出码流Buffer的最大个数 */
#define NR_MAX_ENC_CHN_USERDATACNT	2				/**< Encoder中每个Channel的用户插入数据缓存最大个数 */
#define NR_MAX_ENC_CHN_USERDATASIZE	1044			/**< Encoder中每个Channel的用户插入数据每个缓存最大大小 */

/* Decoder */
#define NR_MAX_DEC_GROUPS			1				/**< Decoder的最大Group数 */
#define NR_MAX_DEC_CHN_IN_GROUP		1				/**< Decoder中每个Group可注册的Channel数 */
#define NR_MAX_DEC_CHN				1				/**< Decoder的最大Channel数 */
#define NR_MAX_DEC_CHN_STREAM		2				/**< Decoder中每个Channel的输出码流Buffer的最大个数 */

/* OSD */
#define NR_MAX_OSD_REGION			512				/**< OSD的最大Region数 */
#define NR_MAX_OSD_GROUPS			4					/**< OSD的最大Group数 */
#define NR_MAX_OSD_OUTPUT_IN_GROUP	3					/**< OSD中每个Group中的最大Channel数 */

/* PWM */
#define NR_MAX_PWM_NUM			2

/* FB */
#define NR_MAX_FB_GROUPS			1					/**< FB的最大Group数 */
#define NR_MAX_FB_OUTPUT_IN_GROUP	0					/**< FB中每个Group中的最大Channel数 */
#define NR_MAX_FB_FGS				2					/**< FB中FG的最大个数 */

#endif /* __CONSTRAINTS_H__ */
