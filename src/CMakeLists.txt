cmake_minimum_required(VERSION 2.8.11)

set(CMAKE_AUTOMOC ON)
set(CMAKE_UPNP ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON )


set(HEADERS

)

set(SRC
	main.cpp
)


find_library(LIB_AVCODEC "avcodec" /Users/f_v_m/ffmpeg/lib NO_DEFAULT_PATH)
find_library(LIB_AVDEVICE "avdevice" /Users/f_v_m/ffmpeg/lib NO_DEFAULT_PATH)
find_library(LIB_AVFILTER "avfilter" /Users/f_v_m/ffmpeg/lib NO_DEFAULT_PATH)
find_library(LIB_AVFORMAT "avformat" /Users/f_v_m/ffmpeg/lib NO_DEFAULT_PATH)
find_library(LIB_AVUTIL "avutil" /Users/f_v_m/ffmpeg/lib NO_DEFAULT_PATH)
find_library(LIB_SWSCALE "swscale" /Users/f_v_m/ffmpeg/lib NO_DEFAULT_PATH)
find_library(LIB_SWRESAMPLE "swresample" /Users/f_v_m/ffmpeg/lib NO_DEFAULT_PATH)

MESSAGE(${LIB_AVCODEC})
MESSAGE(${LIB_AVUTIL})





include_directories(/Users/f_v_m/ffmpeg/include)

add_executable(${APP_EXECUTABLE_NANE} ${HEADERS} ${SRC})

target_link_libraries(${APP_EXECUTABLE_NANE}
				${LIB_AVCODEC}
				${LIB_AVDEVICE}
				${LIB_AVFILTER}
				${LIB_AVFORMAT}
				${LIB_AVUTIL}
                ${LIB_SWSCALE}
                ${LIB_SWRESAMPLE}
)



INSTALL(TARGETS ${APP_EXECUTABLE_NANE} BUNDLE DESTINATION . RUNTIME DESTINATION . COMPONENT Runtime)



FILE(GLOB cmake_rule_files "*.cxx.rule")
SOURCE_GROUP("CMake Rules" FILES ${cmake_rule_files}) 

FILE(GLOB h_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
SOURCE_GROUP("Include" FILES ${h_files}) 

FILE(GLOB cpp_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
SOURCE_GROUP("Source" FILES ${cpp_files}) 