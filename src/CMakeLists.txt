SET(LIBRARY_NAME "wsrt")



#INCLUDE_DIRECTORIES(../3rdparty/jz_aac/fdk)
INCLUDE_DIRECTORIES(${INSTALL_LIBRARY_PATH}/include/fdk-aac)
INCLUDE_DIRECTORIES(../3rdparty/imp-t31/include_cn/)
INCLUDE_DIRECTORIES(../3rdparty/srt-1.4.1 
					../3rdparty/srt-1.4.1/srtcore
					../3rdparty/srt-1.4.1/apps
					)



# 设置目标链接库文件的存放位置
SET(LIBRARY_OUTPUT_PATH "${ROOT_DIR}/lib")

# 添加源文件目录
AUX_SOURCE_DIRECTORY(${ROOT_DIR}/src DIR_LIB_SRCS)
MESSAGE("src>>ROOT_DIR=${ROOT_DIR}")
MESSAGE("src>>DIR_LIB_SRCS=${DIR_LIB_SRCS}")
MESSAGE("src>>PROJECT_NAME=${PROJECT_NAME}")
	
# 添加头文件目录
INCLUDE_DIRECTORIES(${ROOT_DIR}/inc)
	
# 生成静态库
add_library(${LIBRARY_NAME}_static STATIC ${DIR_LIB_SRCS}
			${ROOT_DIR}/3rdparty/srt-1.4.1/apps/transmitmedia.cpp 
			${ROOT_DIR}/3rdparty/srt-1.4.1/apps/uriparser.cpp
			${ROOT_DIR}/3rdparty/srt-1.4.1/apps/verbose.cpp
			${ROOT_DIR}/3rdparty/srt-1.4.1/apps/socketoptions.cpp
			)
# 生成动态库
add_library(${LIBRARY_NAME} SHARED ${DIR_LIB_SRCS}
			${ROOT_DIR}/3rdparty/srt-1.4.1/apps/transmitmedia.cpp 
			${ROOT_DIR}/3rdparty/srt-1.4.1/apps/uriparser.cpp
			${ROOT_DIR}/3rdparty/srt-1.4.1/apps/verbose.cpp
			${ROOT_DIR}/3rdparty/srt-1.4.1/apps/socketoptions.cpp
			)

	
message("INSTALL_LIBRARY_PATH: ${INSTALL_LIBRARY_PATH}")
link_directories(${INSTALL_LIBRARY_PATH}/lib)
TARGET_LINK_LIBRARIES(${LIBRARY_NAME}
						${INSTALL_LIBRARY_PATH}/lib/libsrt.a
						${INSTALL_LIBRARY_PATH}/lib/libcrypto.a
						pthread m rt stdc++ 
						)
TARGET_LINK_LIBRARIES(${LIBRARY_NAME}_static
						pthread m rt stdc++
						)

message("COMPILER_PREFIX: ${COMPILER_PREFIX}")
add_custom_command(TARGET ${LIBRARY_NAME}_static POST_BUILD
                    COMMAND ${COMPILER_PREFIX}ar x ${INSTALL_LIBRARY_PATH}/lib/libsrt.a
                    COMMAND ${COMPILER_PREFIX}ar x ${INSTALL_LIBRARY_PATH}/lib/libcrypto.a
                    COMMAND ${COMPILER_PREFIX}ar rsv ${ROOT_DIR}/lib/libwsrt.a *.o
                    COMMAND ${COMPILER_PREFIX}ranlib ${ROOT_DIR}/lib/libwsrt.a
                    )

IF(FLATFORM_INGENIC)
	# 指定静态库的输出名称
	set_target_properties(${LIBRARY_NAME}_static PROPERTIES OUTPUT_NAME "${LIBRARY_NAME}" LINK_FLAGS "-muclibc -ldl" COMPILE_FLAGS "-muclibc -ldl -fpermissive -std=c++11")
	# 使动态库和静态库同时存在
	set_target_properties(${LIBRARY_NAME} PROPERTIES CLEAN_DIRECT_OUTPUT 1 LINK_FLAGS "-muclibc -ldl" COMPILE_FLAGS "-muclibc -ldl -fpermissive -std=c++11")
ENDIF(FLATFORM_INGENIC)

IF(FLATFORM_ROCKCHIP)
	# 指定静态库的输出名称
	set_target_properties(${LIBRARY_NAME}_static PROPERTIES OUTPUT_NAME "${LIBRARY_NAME}" LINK_FLAGS "-ldl" COMPILE_FLAGS "-ldl -fpermissive -std=c++11")
	# 使动态库和静态库同时存在
	set_target_properties(${LIBRARY_NAME} PROPERTIES CLEAN_DIRECT_OUTPUT 1 LINK_FLAGS "-ldl" COMPILE_FLAGS "-ldl -fpermissive -std=c++11")
ENDIF(FLATFORM_ROCKCHIP)

set_target_properties(${LIBRARY_NAME}_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)

# 指定动态库版本
# VERSION 动态库版本
# SOVERSION API版本
set_target_properties(${LIBRARY_NAME} PROPERTIES VERSION 1.0 SOVERSION 1)

# 将动态库与动态库同时安装到lib目录中
install (TARGETS ${LIBRARY_NAME}_static ${LIBRARY_NAME} DESTINATION ${ROOT_DIR}/lib)



