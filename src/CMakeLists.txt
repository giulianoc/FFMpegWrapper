# Copyright (C) Giuliano Catrambone (giuliano.catrambone@catrasoftware.it)

# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.

# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.

# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 675 Mass
# Ave, Cambridge, MA 02139, USA.

# Commercial use other than under the terms of the GNU General Public License is
# allowed only after express negotiation of conditions with the authors.

set(SOURCES
    FFMpegFilters.cpp
    FFMpegEncodingParameters.cpp
    FFMpegWrapper.cpp
    Monitoring.cpp
    EncodeContent.cpp
    Overlay.cpp
    IntroOutroOverlay.cpp
    LocalInputDevices.cpp
    VideoSpeed.cpp
    PictureInPicture.cpp
    SilentAudio.cpp
    MediaInfo.cpp
    ConcatCutMuxSplit.cpp
    Frames.cpp
    SlideShow.cpp
    LiveRecorder.cpp
    LiveProxy.cpp
    LiveGrid.cpp
    ChangeFileFormat.cpp
    StreamingToFile.cpp
    Incrontab.cpp
    Youtube.cpp)
set(HEADERS FFMpegFilters.h FFMpegEncodingParameters.h FFMpegWrapper.h)

find_program(LSB_RELEASE_EXEC lsb_release)
execute_process(
  COMMAND ${LSB_RELEASE_EXEC} -is
  OUTPUT_VARIABLE LSB_RELEASE_ID_SHORT
  OUTPUT_STRIP_TRAILING_WHITESPACE)
message("-- LSB_RELEASE_ID_SHORT: ${LSB_RELEASE_ID_SHORT}")

include_directories("${THREADLOGGER_INCLUDE_DIR}")
include_directories("${STRINGUTILS_INCLUDE_DIR}")
include_directories("${FFMPEGENGINE_INCLUDE_DIR}")
include_directories("${DATETIME_INCLUDE_DIR}")
include_directories("${JSONUTILS_INCLUDE_DIR}")
include_directories("${CURLWRAPPER_INCLUDE_DIR}")
include_directories("${PROCESSUTILITY_INCLUDE_DIR}")
include_directories("${SPDLOG_INCLUDE_DIR}")
include_directories("${NLOHMANN_INCLUDE_DIR}")
if(APPLE)
    link_directories("${THREADLOGGER_LIB_DIR}")
	link_directories("${JSONUTILS_LIB_DIR}")
    link_directories("${FFMPEGENGINE_LIB_DIR}")
endif()

add_library(FFMpegWrapper SHARED ${SOURCES} ${HEADERS})

if(APPLE)
    target_link_libraries(FFMpegWrapper FFMpegEngine)
    target_link_libraries(FFMpegWrapper StringUtils)
    target_link_libraries(FFMpegWrapper Datetime)
    target_link_libraries(FFMpegWrapper JSONUtils)
	target_link_libraries(FFMpegWrapper CurlWrapper)
	target_link_libraries(FFMpegWrapper ProcessUtility)
endif()

install(TARGETS FFMpegWrapper DESTINATION lib)
install(FILES ${HEADERS} DESTINATION include)
