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

using namespace std;

void FFMpegWrapper::retrieveLocalInputDevices(
	vector<pair<string, string>> &videoLocalInputDevices, vector<pair<string, string>> &audioLocalInputDevices
)
{
	LOG_INFO("Received retrieveLocalInputDevices");

	try
	{
		string outputFfmpegPathFileName;
		string ffmpegExecuteCommand;

		try
		{
			outputFfmpegPathFileName = _ffmpegTempDir + "/" + "inputDevices.log";

#ifdef _WIN32
			ffmpegExecuteCommand = std::format("{}/ffmpeg -list_devices true -f dshow -i dummy > {} 2>&1", _ffmpegPath, outputFfmpegPathFileName);
#elif defined(__APPLE__)
			ffmpegExecuteCommand =
				std::format("{}/ffmpeg -f avfoundation -list_devices true -i \"\" > {} 2>&1", _ffmpegPath, outputFfmpegPathFileName);
#elif defined(__linux__)
			ffmpegExecuteCommand = std::format("{}/ffmpeg -f x11grab -list_devices true -i \"\" > {} 2>&1", _ffmpegPath, outputFfmpegPathFileName);
#endif

			chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

			LOG_INFO(
				"retrieveLocalInputDevices: Executing ffmpeg command"
				", ffmpegExecuteCommand: {}",
				ffmpegExecuteCommand
			);
			int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
			// ffmpeg ritorna un errore perchè non riceve uno stream di input valido (-i "")
			// if (executeCommandStatus != 0)
			// {
			// 	string errorMessage = std::format(
			// 		"retrieveLocalInputDevices failed"
			// 		", executeCommandStatus: {}"
			// 		", ffmpegExecuteCommand: {}",
			// 		executeCommandStatus, ffmpegExecuteCommand
			// 	);
			// 	LOG_ERROR(errorMessage);

			// 	throw runtime_error(errorMessage);
			// }

			chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

			LOG_INFO(
				"retrieveLocalInputDevices: Executed ffmpeg command"
				", ffmpegExecuteCommand: {}"
				", executeCommandStatus: {}"
				", @FFMPEG statistics@ - duration (secs): @{}@",
				ffmpegExecuteCommand, executeCommandStatus, chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()
			);
		}
		catch (exception &e)
		{
			string errorMessage = std::format(
				"retrieveLocalInputDevices failed"
				", ffmpegExecuteCommand: {}"
				", e.what(): {}",
				ffmpegExecuteCommand, e.what()
			);
			LOG_ERROR(errorMessage);

			if (fs::exists(outputFfmpegPathFileName.c_str()))
			{
				LOG_INFO(
					"remove"
					", outputFfmpegPathFileName: {}",
					outputFfmpegPathFileName
				);
				fs::remove_all(outputFfmpegPathFileName);
			}

			throw;
		}

		try
		{
			if (!fs::exists(outputFfmpegPathFileName.c_str()))
			{
				LOG_INFO(
					"ffmpeg: ffmpeg status not available"
					", outputFfmpegPathFileName: {}",
					outputFfmpegPathFileName
				);

				throw FFMpegEncodingStatusNotAvailable();
			}

			ifstream ifPathFileName(outputFfmpegPathFileName);
			string line;
#ifdef _WIN32
			regex videoRegex("\"([^\"]+)\" \\(video\\)");
			regex audioRegex("\"([^\"]+)\" \\(audio\\)");
			while (getline(ifPathFileName, line))
			{
				// ...
				// [dshow @ 000002068dcfe7c0] "OBS Virtual Camera" (video)
				// [dshow @ 000002068dcfe7c0]   Alternative name
				// "@device_sw_{860BB310-5D01-11D0-BD3B-00A0C911CE86}\{A3FCE0F5-3493-419F-958A-ABA1250EC20B}" [dshow @ 000002068dcfe7c0] Could not
				// enumerate audio only devices (or none found). Error opening input file dummy.
				// ...
				smatch match;
				if (regex_search(line, match, videoRegex))
					videoLocalInputDevices.emplace_back(match[1], match[1]); // first deviceFFMpeg, second deviceLabel
				else if (regex_search(line, match, audioRegex))
					audioLocalInputDevices.emplace_back(match[1], match[1]); // first deviceFFMpeg, second deviceLabel
			}
#elif defined(__APPLE__)
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
						string deviceFFMpeg = match[1];
						string deviceLabel = match[2];
						if (isVideoSection)
							videoLocalInputDevices.emplace_back(deviceFFMpeg, deviceLabel);
						else
							audioLocalInputDevices.emplace_back(deviceFFMpeg, deviceLabel);
					}
					else
					{
						isVideoSection = false;
						isAudioSection = false;
					}
				}
			}
#elif defined(__linux__)
#endif

#ifdef _WIN32
			// Windows non permette la rimozione del file
#else
			LOG_INFO(
				"remove"
				", outputFfmpegPathFileName: {}",
				outputFfmpegPathFileName
			);
			fs::remove_all(outputFfmpegPathFileName);
#endif
		}
		catch (exception &e)
		{
			string errorMessage = std::format(
				"retrieveLocalInputDevices error"
				", e.what(): {}",
				e.what()
			);
			LOG_ERROR(errorMessage);

#ifdef _WIN32
			// Windows non permette la rimozione del file
#else
			LOG_INFO(
				"remove"
				", outputFfmpegPathFileName: {}",
				outputFfmpegPathFileName
			);
			fs::remove_all(outputFfmpegPathFileName);
#endif

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
		LOG_ERROR(errorMessage);
	}
}
