# PSn00bSDK example CMake script
# (C) 2021 spicyjpeg - MPL licensed

cmake_minimum_required(VERSION 3.21)

project(
	bench
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "PSn00bSDK benchmark"
	HOMEPAGE_URL "https://github.com/SimoSbara/ps1-benchmark"
)

file(GLOB _sources *.c)
psn00bsdk_add_executable(bench GPREL ${_sources})

psn00bsdk_target_incbin(bench PRIVATE tilesc tilesc.tim)

psn00bsdk_add_cd_image(
	iso      # Target name
	bench # Output file name (= bench.bin + bench.cue)
	iso.xml  # Path to config file
	DEPENDS bench system.cnf TRACK-1.vag TRACK-2.vag TRACK-3.vag TRACK-4.vag
)

install(
	FILES
		${PROJECT_BINARY_DIR}/bench.bin
		${PROJECT_BINARY_DIR}/bench.cue
	TYPE BIN
)