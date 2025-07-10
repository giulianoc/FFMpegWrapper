/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/*
 * File:   FFMPEGEncoder.cpp
 * Author: giuliano
 *
 * Created on February 18, 2018, 1:27 AM
 */
#include "FFMpegWrapper.h"
#include "ProcessUtility.h"
#include "spdlog/spdlog.h"
#include <fstream>
#include <regex>

void FFMpegWrapper::retrieveLocalInputDevices(vector<pair<int, string>> &videoLocalInputDevices, vector<pair<int, string>> &audioLocalInputDevices)
{
	SPDLOG_INFO("Received retrieveLocalInputDevices");

	try
	{
		string outputFfmpegPathFileName;
		string ffmpegExecuteCommand;

		try
		{
			outputFfmpegPathFileName = _ffmpegTempDir + "/" + "inputDevices.log";

			string framework;
#ifdef _WIN32
			framework = "gdigrab";
#elif defined(__APPLE__)
			framework = "avfoundation";
#elif defined(__linux__)
			framework = "x11grab";
#endif

			ffmpegExecuteCommand =
				std::format("{}/ffmpeg -f {} -list_devices true -i \"\" > {} 2>&1", _ffmpegPath, framework, outputFfmpegPathFileName);

			chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

			SPDLOG_INFO(
				"retrieveLocalInputDevices: Executing ffmpeg command"
				", ffmpegExecuteCommand: {}",
				ffmpegExecuteCommand
			);
			int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
			/*
			 * ffmpeg ritorna un errore perchè non riceve uno stream di input valido (-i "")
			if (executeCommandStatus != 0)
			{
				string errorMessage = std::format(
					"retrieveLocalInputDevices failed"
					", executeCommandStatus: {}"
					", ffmpegExecuteCommand: {}",
					executeCommandStatus, ffmpegExecuteCommand
				);
				SPDLOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}
			*/

			chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

			SPDLOG_INFO(
				"retrieveLocalInputDevices: Executed ffmpeg command"
				", ffmpegExecuteCommand: {}"
				", executeCommandStatus: {}"
				", @FFMPEG statistics@ - duration (secs): @{}@",
				ffmpegExecuteCommand, executeCommandStatus, chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()
			);
		}
		catch (exception &e)
		{
			string lastPartOfFfmpegOutputFile = getLastPartOfFile(outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
			string errorMessage = std::format(
				"retrieveLocalInputDevices failed"
				", ffmpegExecuteCommand: {}"
				", lastPartOfFfmpegOutputFile: {}"
				", e.what(): {}",
				ffmpegExecuteCommand, lastPartOfFfmpegOutputFile, e.what()
			);
			SPDLOG_ERROR(errorMessage);

			if (fs::exists(outputFfmpegPathFileName.c_str()))
			{
				SPDLOG_INFO(
					"remove"
					", outputFfmpegPathFileName: {}",
					outputFfmpegPathFileName
				);
				fs::remove_all(outputFfmpegPathFileName);
			}

			throw e;
		}

		try
		{
			if (!fs::exists(outputFfmpegPathFileName.c_str()))
			{
				SPDLOG_INFO(
					"ffmpeg: ffmpeg status not available"
					", outputFfmpegPathFileName: {}",
					outputFfmpegPathFileName
				);

				throw FFMpegEncodingStatusNotAvailable();
			}

			ifstream ifPathFileName(outputFfmpegPathFileName);
			string line;
			bool isVideoSection = false;
			bool isAudioSection = false;
			regex videoRegex(R"(\[(\d+)\]\s+(.*))");
			while (getline(ifPathFileName, line))
			{
				// ...
				// [AVFoundation indev @ 0x7f86df706240] AVFoundation video devices:
				// [AVFoundation indev @ 0x7f86df706240] [0] FaceTime HD Camera
				// [AVFoundation indev @ 0x7f86df706240] [1] Capture screen 0
				// [AVFoundation indev @ 0x7f86df706240] AVFoundation audio devices:
				// [AVFoundation indev @ 0x7f86df706240] [0] MacBook Pro Microphone
				// [AVFoundation indev @ 0x7f86df706240] [1] Microsoft Teams Audio
				// ...
				if (line.find("AVFoundation video devices:") != string::npos)
				{
					isVideoSection = true;
					isAudioSection = false;
					continue;
				}
				if (line.find("AVFoundation audio devices:") != string::npos)
				{
					isVideoSection = false;
					isAudioSection = true;
					continue;
				}

				if (isVideoSection || isAudioSection)
				{
					smatch match;
					if (regex_search(line, match, videoRegex) && match.size() >= 3)
					{
						int index = stoi(match[1]);
						string deviceName = match[2];
						if (isVideoSection)
							videoLocalInputDevices.emplace_back(index, deviceName);
						else
							audioLocalInputDevices.emplace_back(index, deviceName);
					}
					else
					{
						isVideoSection = false;
						isAudioSection = false;
					}
				}
			}

			SPDLOG_INFO(
				"remove"
				", outputFfmpegPathFileName: {}",
				outputFfmpegPathFileName
			);
			fs::remove_all(outputFfmpegPathFileName);
		}
		catch (exception &e)
		{
			string errorMessage = std::format(
				"retrieveLocalInputDevices error"
				", e.what(): {}",
				e.what()
			);
			SPDLOG_ERROR(errorMessage);

			SPDLOG_INFO(
				"remove"
				", outputFfmpegPathFileName: {}",
				outputFfmpegPathFileName
			);
			fs::remove_all(outputFfmpegPathFileName);

			throw;
		}
	}
	catch (exception &e)
	{
		string errorMessage = std::format(
			"retrieveLocalInputDevices failed"
			", exception: {}",
			e.what()
		);
		SPDLOG_ERROR(errorMessage);
	}
}
