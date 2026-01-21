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
#include "Datetime.h"
#include "FFMpegEncodingParameters.h"
#include "FFMpegWrapper.h"
#include "ProcessUtility.h"
#include "spdlog/spdlog.h"

using namespace std;
using json = nlohmann::json;

void FFMpegWrapper::silentAudio(
	string videoAssetPathName, int64_t videoDurationInMilliSeconds,

	string addType, // entireTrack, begin, end
	int seconds,

	json encodingProfileDetailsRoot,

	string stagingEncodedAssetPathName, int64_t encodingJobKey, int64_t ingestionJobKey, ProcessUtility::ProcessId &processId,
	shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
)
{
	int iReturnedStatus = 0;

	_currentApiName = APIName::SilentAudio;

	LOG_INFO(
		"Received {}"
		", ingestionJobKey: {}"
		", encodingJobKey: {}",
		toString(_currentApiName), ingestionJobKey, encodingJobKey
	);

	setStatus(ingestionJobKey, encodingJobKey, videoDurationInMilliSeconds, videoAssetPathName, stagingEncodedAssetPathName);

	try
	{
		if (!fs::exists(videoAssetPathName))
		{
			string errorMessage = std::format(
				"video asset path name not existing"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", videoAssetPathName: {}",
				ingestionJobKey, encodingJobKey, videoAssetPathName
			);
			LOG_ERROR(errorMessage);

			throw runtime_error(errorMessage);
		}

		FFMpegEncodingParameters ffmpegEncodingParameters(
			ingestionJobKey, encodingJobKey, encodingProfileDetailsRoot,
			true, // isVideo,
			-1,	  // videoTrackIndexToBeUsed,
			-1,	  // audioTrackIndexToBeUsed,
			stagingEncodedAssetPathName,
			nullptr, // videoTracksRoot,
			nullptr, // audioTracksRoot,

			_twoPasses, // out

			_ffmpegTempDir, _ffmpegTtfFontDir
		);

		{
			tm tmUtcTimestamp = Datetime::utcSecondsToLocalTime(chrono::system_clock::to_time_t(chrono::system_clock::now()));

			_outputFfmpegPathFileName = std::format(
				"{}/{}_{}_{}_{:0>4}-{:0>2}-{:0>2}-{:0>2}-{:0>2}-{:0>2}.log", _ffmpegTempDir, "silentAudio", _currentIngestionJobKey,
				_currentEncodingJobKey, tmUtcTimestamp.tm_year + 1900, tmUtcTimestamp.tm_mon + 1, tmUtcTimestamp.tm_mday, tmUtcTimestamp.tm_hour,
				tmUtcTimestamp.tm_min, tmUtcTimestamp.tm_sec
			);
		}

		// vector<string> ffmpegArgumentList;
		// ostringstream ffmpegArgumentListStream;

		if (ffmpegEncodingParameters._httpStreamingFileFormat != "")
		{
		}
		else
		{
			FFMpegEngine ffMpegEngine;
			if (_twoPasses)
			{
			}
			else
			{
				if (addType == "entireTrack")
				{
					/*
					add entire track:

					Se si aggiunge una traccia audio 'silent', non ha senso avere altre tracce audio, per cui il comando
					ignora eventuali tracce audio nel file originale a aggiunge la traccia audio silent

					-f lavfi -i anullsrc genera una sorgente audio virtuale con silenzio di lunghezza infinita. Ecco perché è
					importante specificare -shortest per limitare la durata dell'output alla durata del flusso video.
					In caso contrario, verrebbe creato un file di output infinito.

					ffmpeg -f lavfi -i anullsrc -i video.mov -c:v copy -c:a aac -map 0:a -map 1:v -shortest output.mp4
					*/

					// ffmpegArgumentList.push_back("ffmpeg");
					// global options
					// ffmpegArgumentList.push_back("-y");
					ffMpegEngine.addGlobalArg("-y");

					// input options
					FFMpegEngine::Input input_1 = ffMpegEngine.addInput("anullsrc");
					input_1.addArgs("-f lavfi");
					FFMpegEngine::Input input_2 = ffMpegEngine.addInput(videoAssetPathName);

					// ffmpegArgumentList.push_back("-f");
					// ffmpegArgumentList.push_back("lavfi");
					// ffmpegArgumentList.push_back("-i");
					// ffmpegArgumentList.push_back("anullsrc");
					// ffmpegArgumentList.push_back("-i");
					// ffmpegArgumentList.push_back(videoAssetPathName);

					FFMpegEngine::Output mainOutput = ffMpegEngine.addOutput(stagingEncodedAssetPathName);
					// output options
					// ffmpegArgumentList.push_back("-map");
					// ffmpegArgumentList.push_back("0:a");
					// ffmpegArgumentList.push_back("-map");
					// ffmpegArgumentList.push_back("1:v");
					mainOutput.map("0:a");
					mainOutput.map("1:v");

					// ffmpegArgumentList.push_back("-shortest");
					mainOutput.addArgs("-shortest");

					if (encodingProfileDetailsRoot != nullptr)
					{
						ffmpegEncodingParameters.applyEncoding(
							-1,	  // -1: NO two passes
							true, // outputFileToBeAdded
							true, // videoResolutionToBeAdded
							nullptr, ffMpegEngine
						);
					}
					else
					{
						// ffmpegArgumentList.push_back("-c:v");
						// ffmpegArgumentList.push_back("copy");
						// ffmpegArgumentList.push_back("-c:a");
						// ffmpegArgumentList.push_back("aac"); // default
						mainOutput.withVideoCodec("copy");
						mainOutput.withAudioCodec("aac");

						// ffmpegArgumentList.push_back(stagingEncodedAssetPathName);
					}
				}
				else if (addType == "begin")
				{
					/*
					begin:
					ffmpeg -i video.mov -af "adelay=1s:all=true" -c:v copy -c:a aac output.mp4
					*/
					// ffmpegArgumentList.push_back("ffmpeg");
					// global options
					// ffmpegArgumentList.push_back("-y");
					ffMpegEngine.addGlobalArg("-y");

					// input options
					// ffmpegArgumentList.push_back("-i");
					// ffmpegArgumentList.push_back(videoAssetPathName);
					ffMpegEngine.addInput(videoAssetPathName);

					// output options
					FFMpegEngine::Output mainOutput = ffMpegEngine.addOutput(stagingEncodedAssetPathName);
					// ffmpegArgumentList.push_back("-af");
					// ffmpegArgumentList.push_back("adelay=" + to_string(seconds) + "s:all=true");
					mainOutput.addArgs(std::format("-af adelay={}s:all=true", seconds));

					if (encodingProfileDetailsRoot != nullptr)
					{
						ffmpegEncodingParameters.applyEncoding(
							-1,	  // -1: NO two passes
							true, // outputFileToBeAdded
							true, // videoResolutionToBeAdded
							nullptr, ffMpegEngine
						);
					}
					else
					{
						// ffmpegArgumentList.push_back("-c:v");
						// ffmpegArgumentList.push_back("copy");
						// ffmpegArgumentList.push_back("-c:a");
						// ffmpegArgumentList.push_back("aac"); // default
						mainOutput.addArgs("-c:v copy -c:a aac");

						// ffmpegArgumentList.push_back(stagingEncodedAssetPathName);
						mainOutput.setPath(stagingEncodedAssetPathName);
					}
				}

				try
				{
					chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					// if (!ffmpegArgumentList.empty())
					// 	copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(), ostream_iterator<string>(ffmpegArgumentListStream, " "));

					LOG_INFO(
						"{}: Executing ffmpeg command"
						", encodingJobKey: {}"
						", ingestionJobKey: {}"
						", ffmpegArgumentList: {}",
						toString(_currentApiName), encodingJobKey, ingestionJobKey, ffMpegEngine.toSingleLine()
					);

					if (ffmpegCallbackData)
						ffmpegCallbackData->reset();
					ffMpegEngine.run(_ffmpegPath, processId, iReturnedStatus,
						std::format(", ingestionJobKey: {}, encodingJobKey: {}", ingestionJobKey, encodingJobKey),
						ffmpegCallbackData, _outputFfmpegPathFileName);
					/*
					bool redirectionStdOutput = true;
					bool redirectionStdError = true;
					if (ffmpegLineCallback)
						ProcessUtility::forkAndExecByCallback(
							_ffmpegPath + "/ffmpeg", ffMpegEngine.buildArgs(true), ffmpegLineCallback,
							redirectionStdOutput, redirectionStdError, processId,iReturnedStatus
						);
					else
					{
						vector<string> args = ffMpegEngine.buildArgs(false);
						ProcessUtility::forkAndExec(
							_ffmpegPath + "/ffmpeg", args, _outputFfmpegPathFileName,
							redirectionStdOutput, redirectionStdError, processId,iReturnedStatus
						);
					}
					*/
					processId.reset();
					if (iReturnedStatus != 0)
					{
						LOG_ERROR(
							"{}: ffmpeg command failed"
							", encodingJobKey: {}"
							", ingestionJobKey: {}"
							", iReturnedStatus: {}"
							", ffmpegArgumentList: {}",
							toString(_currentApiName), encodingJobKey, ingestionJobKey, iReturnedStatus, ffMpegEngine.toSingleLine()
						);

						// to hide the ffmpeg staff
						string errorMessage = std::format(
							"{} command failed"
							", encodingJobKey: {}"
							", ingestionJobKey: {}",
							toString(_currentApiName), encodingJobKey, ingestionJobKey
						);
						throw runtime_error(errorMessage);
					}

					chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

					LOG_INFO(
						"{}: Executed ffmpeg command"
						", encodingJobKey: {}"
						", ingestionJobKey: {}"
						", ffmpegArgumentList: {}"
						", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @{}@",
						toString(_currentApiName), encodingJobKey, ingestionJobKey, ffMpegEngine.toSingleLine(),
						chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()
					);
				}
				catch (runtime_error &e)
				{
					processId.reset();

					// string lastPartOfFfmpegOutputFile = getLastPartOfFile(_outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9) // 9 means: SIGKILL
						errorMessage = std::format(
							"ffmpeg: ffmpeg command failed because killed by the user"
							", _outputFfmpegPathFileName: {}"
							", encodingJobKey: {}"
							", ingestionJobKey: {}"
							", ffmpegArgumentList: {}"
							", e.what(): {}",
							_outputFfmpegPathFileName, encodingJobKey, ingestionJobKey, ffMpegEngine.toSingleLine(), e.what()
						);
					else
						errorMessage = std::format(
							"ffmpeg: ffmpeg command failed"
							", _outputFfmpegPathFileName: {}"
							", encodingJobKey: {}"
							", ingestionJobKey: {}"
							", ffmpegArgumentList: {}"
							", e.what(): {}",
							_outputFfmpegPathFileName, encodingJobKey, ingestionJobKey, ffMpegEngine.toSingleLine(), e.what()
						);
					LOG_ERROR(errorMessage);

					LOG_INFO(
						"remove"
						", ingestionJobKey: {}"
						", encodingJobKey: {}"
						", _outputFfmpegPathFileName: {}",
						ingestionJobKey, encodingJobKey, _outputFfmpegPathFileName
					);
					fs::remove_all(_outputFfmpegPathFileName);

					if (iReturnedStatus == 9) // 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
				}

				LOG_INFO(
					"remove"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", _outputFfmpegPathFileName: {}",
					ingestionJobKey, encodingJobKey, _outputFfmpegPathFileName
				);
				fs::remove_all(_outputFfmpegPathFileName);
			}

			LOG_INFO(
				"{} file generated"
				", encodingJobKey: {}"
				", ingestionJobKey: {}"
				", stagingEncodedAssetPathName: {}",
				toString(_currentApiName), encodingJobKey, ingestionJobKey, stagingEncodedAssetPathName
			);

			unsigned long ulFileSize = fs::file_size(stagingEncodedAssetPathName);

			if (ulFileSize == 0)
			{
				LOG_INFO(
					"ffmpeg: ffmpeg command failed, encoded file size is 0"
					", encodingJobKey: {}"
					", ingestionJobKey: {}"
					", ffmpegArgumentList: {}",
					encodingJobKey, ingestionJobKey, ffMpegEngine.toSingleLine()
				);

				// to hide the ffmpeg staff
				string errorMessage = std::format(
					"command failed, encoded file size is 0"
					", encodingJobKey: {}"
					", ingestionJobKey: {}",
					encodingJobKey, ingestionJobKey
				);
				throw runtime_error(errorMessage);
			}
		}
	}
	catch (exception &e)
	{
		LOG_ERROR(
			"{} ffmpeg failed"
			", encodingJobKey: {}"
			", ingestionJobKey: {}"
			", stagingEncodedAssetPathName: {}"
			", e.what(): ",
			toString(_currentApiName), encodingJobKey, ingestionJobKey, stagingEncodedAssetPathName, e.what()
		);

		if (fs::exists(stagingEncodedAssetPathName))
		{
			// file in case of .3gp content OR directory in case of IPhone content
			LOG_INFO(
				"remove"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", stagingEncodedAssetPathName: {}",
				ingestionJobKey, encodingJobKey, stagingEncodedAssetPathName
			);
			fs::remove_all(stagingEncodedAssetPathName);
		}

		throw;
	}
}
