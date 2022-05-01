CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
PROJECT(WCAMERA)

SET(CMAKE_SYSTEM_NAME Linux)

#项目路径
MESSAGE(STATUS "BINARY dir path: " ${PROJECT_BINARY_DIR})
MESSAGE(STATUS "SOURCE dir path: " ${PROJECT_SOURCE_DIR})
	
#多芯片方案选择，支持君正和瑞芯微
option(FLATFORM_ROCKCHIP "Rockchip rv1109-1126" ON)
option(FLATFORM_INGENIC "Ingenic T31" OFF)
message("FLATFORM_ROCKCHIP=${FLATFORM_ROCKCHIP}")
message("FLATFORM_INGENIC=${FLATFORM_INGENIC}")

execute_process(
        COMMAND bash -c "cd ${PROJECT_SOURCE_DIR} && pwd"
        OUTPUT_VARIABLE ROOT_DIR
)
string(STRIP ${ROOT_DIR} ROOT_DIR)
message("project root is ${ROOT_DIR}")



#####################################################
#解压编译器
if(FLATFORM_ROCKCHIP)
	message("瑞芯微rv1126平台...")
	EXEC_PROGRAM("cd ${ROOT_DIR}/compiler && tar zfvx arm32.tar.gz")
	SET(COMPILER_DIR ${ROOT_DIR}/compiler/arm32)
	SET(CMAKE_C_COMPILER ${COMPILER_DIR}/bin/arm-linux-gnueabihf-gcc)
	SET(CMAKE_CXX_COMPILER ${COMPILER_DIR}/bin/arm-linux-gnueabihf-g++)
	SET(INSTALL_LIBRARY_PATH ${COMPILER_DIR}/arm-linux-gnueabihf)
	SET(COMPILER_PREFIX ${COMPILER_DIR}/bin/arm-linux-gnueabihf-)

	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flax-vector-conversions -g -O2 -Wall -ggdb -gdwarf -funwind-tables -rdynamic")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flax-vector-conversions -g -O2 -Wall -ggdb -gdwarf -funwind-tables -rdynamic")
endif(FLATFORM_ROCKCHIP)

if(FLATFORM_INGENIC)
	message("君正T31平台...")
	EXEC_PROGRAM("cd ${ROOT_DIR}/compiler && 7z x mips-gcc472-glibc216-64bit-r2.3.3.7z -r -aos && rm -r ./mips-gcc472-glibc216-64bit/share/doc")
	SET(COMPILER_DIR ${ROOT_DIR}/compiler/mips-gcc472-glibc216-64bit)
	SET(CMAKE_C_COMPILER ${COMPILER_DIR}/bin/mips-linux-gnu-gcc)
	SET(CMAKE_CXX_COMPILER ${COMPILER_DIR}/bin/mips-linux-gnu-g++)
	SET(INSTALL_LIBRARY_PATH ${COMPILER_DIR}/mips-linux-gnu)
	SET(COMPILER_PREFIX ${COMPILER_DIR}/bin/mips-linux-gnu-)
	
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -EL -mmxu2 -flax-vector-conversions")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -EL -mmxu2 -flax-vector-conversions")

endif(FLATFORM_INGENIC)

#####################################################
#编译openssl
SET(OPENSSL_DIR ${ROOT_DIR}/3rdparty/openssl-1.1.0l)
EXEC_PROGRAM("cd ${OPENSSL_DIR} && rm Makefile && ./config no-asm shared no-async --prefix=${INSTALL_LIBRARY_PATH} --cross-compile-prefix=${COMPILER_PREFIX}")
if(FLATFORM_INGENIC)
	EXEC_PROGRAM("cd ${OPENSSL_DIR} && sed -i 's/-m64/-muclibc/g' Makefile")
endif(FLATFORM_INGENIC)
if(FLATFORM_ROCKCHIP)
	EXEC_PROGRAM("cd ${OPENSSL_DIR} && sed -i 's/-m64//g' Makefile")
endif(FLATFORM_ROCKCHIP)
EXEC_PROGRAM("cd ${OPENSSL_DIR} && make -j4 && make install")

#####################################################
#编译srt
SET(SRT_DIR ${ROOT_DIR}/3rdparty/srt-1.4.1)
EXEC_PROGRAM("cd ${SRT_DIR} && ./configure --with-compiler-prefix=${COMPILER_PREFIX} --prefix=${INSTALL_LIBRARY_PATH} --openssl-crypto-library=${INSTALL_LIBRARY_PATH}/lib --openssl-include-dir=${INSTALL_LIBRARY_PATH}/include --openssl-ssl-library=${INSTALL_LIBRARY_PATH}/lib")
EXEC_PROGRAM("cd ${SRT_DIR} && make -j4 && make install")

#####################################################
#编译fdk-aac
SET(FDK_DIR ${ROOT_DIR}/3rdparty/fdk-aac-2.0.2)
EXEC_PROGRAM("cd ${FDK_DIR} && ./configure --enable-static --with-pic=yes --disable-shared --host=arm-linux CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} --prefix=${INSTALL_LIBRARY_PATH}")
EXEC_PROGRAM("cd ${FDK_DIR} && make -j4 && make install")


#编译libwsrt库
ADD_SUBDIRECTORY(src)
#编译应用程序
ADD_SUBDIRECTORY(samples)


