# PSn00bSDK example CMake script
# (C) 2021 spicyjpeg - MPL licensed

cmake_minimum_required(VERSION 3.21)

project(
	ps1-benchmark
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "PSn00bSDK benchmark"
	HOMEPAGE_URL "https://github.com/SimoSbara/ps1-benchmark"
)

psn00bsdk_add_executable(ps1-benchmark GPREL main.c)

psn00bsdk_add_cd_image(
	iso      # Target name
	bench # Output file name (= template.bin + template.cue)
	iso.xml  # Path to config file
	DEPENDS bench system.cnf
)
