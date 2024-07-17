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

psn00bsdk_add_executable(bench GPREL main.c)

psn00bsdk_add_cd_image(
	iso      # Target name
	bench # Output file name (= bench.bin + bench.cue)
	iso.xml  # Path to config file
	DEPENDS bench system.cnf
)
