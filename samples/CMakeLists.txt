

message("sample>>ROOT_DIR=${ROOT_DIR}")
message("sample>>CMAKE_C_COMPILER=${CMAKE_C_COMPILER}")

#需包含的头文件路径
INCLUDE_DIRECTORIES(${ROOT_DIR}/inc)
#INCLUDE_DIRECTORIES(../3rdparty/imp-t31/include_cn/)
INCLUDE_DIRECTORIES(${INSTALL_LIBRARY_PATH}/include/fdk-aac)

#君正芯片
if(FLATFORM_INGENIC)
    SET(SAMPLE_NAME "t31cam")
    
    #需编译的源代码文件
    SET(${SAMPLE_NAME}_SRCS 
        ./ingenic_vi_venc_test.cpp 
	    ../src/utils.c
        )
    SET(COMPILE_LINK_FLAGS "-muclibc -ldl")

    INCLUDE_DIRECTORIES(../3rdparty/imp-t31/include_cn/)

    #设置目标程序的编译开关
    ADD_EXECUTABLE(${SAMPLE_NAME} ${${SAMPLE_NAME}_SRCS})
    SET_TARGET_PROPERTIES(${SAMPLE_NAME} PROPERTIES
        OUTPUT_NAME "${SAMPLE_NAME}"
	    #RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/bin
	    LINK_FLAGS ${COMPILE_LINK_FLAGS}
	    COMPILE_FLAGS "${COMPILE_LINK_FLAGS} -fpermissive -std=c++11"
    )
    

    #需连接的静态库
    MESSAGE("SRT_DIR=${SRT_DIR}")
    MESSAGE("OPENSSL_DIR=${OPENSSL_DIR}")
    TARGET_LINK_LIBRARIES(${SAMPLE_NAME}
                            ${ROOT_DIR}/3rdparty/imp-t31/lib/uclibc/libalog.a
                            ${ROOT_DIR}/3rdparty/imp-t31/lib/uclibc/libimp.a
                            #${ROOT_DIR}/lib/libwsrt.so
                            ${ROOT_DIR}/lib/libwsrt.a
                            ${INSTALL_LIBRARY_PATH}/lib/libfdk-aac.a
                            pthread m rt stdc++
    )

endif(FLATFORM_INGENIC)

#瑞芯微芯片
if(FLATFORM_ROCKCHIP)
    SET(SAMPLE_NAME "rkcam")

    #需编译的源代码文件
    aux_source_directory(common SAMPLE_COMMON_FILES)
    SET(${SAMPLE_NAME}_SRCS ${SAMPLE_COMMON_FILES} 
        ./rkmedia_vi_venc_test.c
	    #../src/utils.c
    )
    
    SET(COMPILE_LINK_FLAGS "-ldl")
    
    #add_definitions(-g -O0 -ggdb -gdwarf -funwind-tables -rdynamic)
   
    
    set(SDK_PATH "${COMPILER_DIR}/sdk_2.2.2")
    message(STATUS "SDK_PATH: ${SDK_PATH}" )

    include_directories(${SDK_PATH}/include)
    include_directories(${SDK_PATH}/include/rkmedia)
    include_directories(${SDK_PATH}/include/rkaiq/algos)
    include_directories(${SDK_PATH}/include/rkaiq/common)
    include_directories(${SDK_PATH}/include/rkaiq/iq_parser)
    include_directories(${SDK_PATH}/include/rkaiq/uAPI)
    include_directories(${SDK_PATH}/include/rkaiq/xcore)

    link_directories(${SDK_PATH}/lib)
    add_definitions(-DRKAIQ)
    
    #设置目标程序的编译开关
    ADD_EXECUTABLE(${SAMPLE_NAME} ${${SAMPLE_NAME}_SRCS})
    SET_TARGET_PROPERTIES(${SAMPLE_NAME} PROPERTIES
        OUTPUT_NAME "${SAMPLE_NAME}"
	    #RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/bin
	    LINK_FLAGS ${COMPILE_LINK_FLAGS}
	    COMPILE_FLAGS "${COMPILE_LINK_FLAGS} -fpermissive -std=c++11"
    )
    #add_executable(rkmedia_vi_venc_test rkmedia_vi_venc_test.c ${SAMPLE_COMMON_FILES})
    target_link_libraries(${SAMPLE_NAME} easymedia)
    target_link_libraries(${SAMPLE_NAME} pthread)
    target_link_libraries(${SAMPLE_NAME} v4lconvert v4l2 drm)
    target_link_libraries(${SAMPLE_NAME} rkaiq rga)
    target_link_libraries(${SAMPLE_NAME} rockchip_mpp
						                    liveMedia
						                    groupsock
						                    BasicUsageEnvironment
						                    UsageEnvironment
						                    asound
						                    RKAP_3A
						                    RKAP_ANR
						                    RKAP_Common
						                    rknn_runtime
						                    rknn_api
						                    rockface
						                    sqlite3
						                    md_share
						                    od_share
						                    rockx
						                    OpenVX
						                    VSC
						                    GAL
						                    ArchModelSw
						                    NNArchPerf
	)
    
 

    #需连接的静态库
    TARGET_LINK_LIBRARIES(${SAMPLE_NAME}
                            #${ROOT_DIR}/lib/libwsrt.so
                            ${ROOT_DIR}/lib/libwsrt.a
                            #${INSTALL_LIBRARY_PATH}/lib/libfdk-aac.a
                            pthread m rt stdc++
    )
endif(FLATFORM_ROCKCHIP)
 
