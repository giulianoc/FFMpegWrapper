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

void FFMpegWrapper::videoSpeed(
	string mmsSourceVideoAssetPathName, int64_t videoDurationInMilliSeconds,

	string videoSpeedType, int videoSpeedSize,

	json encodingProfileDetailsRoot,

	string stagingEncodedAssetPathName, int64_t encodingJobKey, int64_t ingestionJobKey, ProcessUtility::ProcessId &processId,
	shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
)
{
	int iReturnedStatus = 0;

	_currentApiName = APIName::VideoSpeed;

	setStatus(ingestionJobKey, encodingJobKey, videoDurationInMilliSeconds, mmsSourceVideoAssetPathName, stagingEncodedAssetPathName);

	try
	{
		if (!fs::exists(mmsSourceVideoAssetPathName))
		{
			string errorMessage = std::format(
				"Source video asset path name not existing"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", mmsSourceVideoAssetPathName: {}",
				ingestionJobKey, encodingJobKey, mmsSourceVideoAssetPathName
			);
			SPDLOG_ERROR(errorMessage);

			throw runtime_error(errorMessage);
		}

		FFMpegEngine ffMpegEngine;

		FFMpegEngine::Output& mainOutput = ffMpegEngine.addOutput(stagingEncodedAssetPathName);

		// vector<string> ffmpegEncodingProfileArgumentList;
		if (encodingProfileDetailsRoot != nullptr)
		{
			try
			{
				string httpStreamingFileFormat;
				string ffmpegHttpStreamingParameter = "";
				bool encodingProfileIsVideo = true;

				string ffmpegFileFormatParameter = "";

				string ffmpegVideoCodecParameter = "";
				string ffmpegVideoCodec = "";
				string ffmpegVideoProfileParameter = "";
				string ffmpegVideoResolutionParameter = "";
				int videoBitRateInKbps = -1;
				string ffmpegVideoBitRateParameter = "";
				string ffmpegVideoOtherParameters = "";
				string ffmpegVideoMaxRateParameter = "";
				string ffmpegVideoBufSizeParameter = "";
				string ffmpegVideoFrameRateParameter = "";
				string ffmpegVideoKeyFramesRateParameter = "";
				bool twoPasses;
				vector<tuple<string, int, int, int, string, string, string>> videoBitRatesInfo;

				string ffmpegAudioCodecParameter = "";
				string ffmpegAudioCodec = "";
				string ffmpegAudioBitRateParameter = "";
				string ffmpegAudioOtherParameters = "";
				string ffmpegAudioChannelsParameter = "";
				string ffmpegAudioSampleRateParameter = "";
				vector<string> audioBitRatesInfo;

				FFMpegEncodingParameters::settingFfmpegParameters(
					encodingProfileDetailsRoot, encodingProfileIsVideo,

					httpStreamingFileFormat, ffmpegHttpStreamingParameter,

					ffmpegFileFormatParameter,

					ffmpegVideoCodecParameter, ffmpegVideoCodec, ffmpegVideoProfileParameter, ffmpegVideoOtherParameters, twoPasses,
					ffmpegVideoFrameRateParameter, ffmpegVideoKeyFramesRateParameter,

					videoBitRatesInfo, ffmpegAudioCodecParameter, ffmpegAudioCodec, ffmpegAudioOtherParameters, ffmpegAudioChannelsParameter,
					ffmpegAudioSampleRateParameter, audioBitRatesInfo
				);

				tuple<string, int, int, int, string, string, string> videoBitRateInfo = videoBitRatesInfo[0];
				tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, ignore, ignore, ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter,
					ffmpegVideoBufSizeParameter) = videoBitRateInfo;

				ffmpegAudioBitRateParameter = audioBitRatesInfo[0];

				/*
				if (httpStreamingFileFormat != "")
				{
					string errorMessage = __FILEREF__ + "in case of recorder it is not possible to have an httpStreaming encoding"
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", encodingJobKey: " + to_string(encodingJobKey)
					;
					SPDLOG_ERROR(errorMessage);

					throw runtime_error(errorMessage);
				}
				else */
				if (twoPasses)
				{
					// siamo sicuri che non sia possibile?
					/*
					string errorMessage = __FILEREF__ + "in case of videoSpeed it is not possible to have a two passes encoding"
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", twoPasses: " + to_string(twoPasses)
					;
					SPDLOG_ERROR(errorMessage);

					throw runtime_error(errorMessage);
					*/
					twoPasses = false;

					SPDLOG_WARN(
						"in case of videoSpeed it is not possible to have a two passes encoding. Change it to false"
						", ingestionJobKey: {}"
						", encodingJobKey: {}"
						", twoPasses: {}",
						ingestionJobKey, encodingJobKey, twoPasses
					);
				}

				// FFMpegEncodingParameters::addToArguments(ffmpegVideoCodecParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.withVideoCodec(ffmpegVideoCodec);
				// FFMpegEncodingParameters::addToArguments(ffmpegVideoProfileParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegVideoProfileParameter);
				// FFMpegEncodingParameters::addToArguments(ffmpegVideoBitRateParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegVideoBitRateParameter);
				// FFMpegEncodingParameters::addToArguments(ffmpegVideoOtherParameters, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegVideoOtherParameters);
				// FFMpegEncodingParameters::addToArguments(ffmpegVideoMaxRateParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegVideoMaxRateParameter);
				// FFMpegEncodingParameters::addToArguments(ffmpegVideoBufSizeParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegVideoBufSizeParameter);
				// FFMpegEncodingParameters::addToArguments(ffmpegVideoFrameRateParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegVideoFrameRateParameter);
				// FFMpegEncodingParameters::addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegVideoKeyFramesRateParameter);
				// we cannot have two video filters parameters (-vf), one is for the overlay.
				// If it is needed we have to combine both using the same -vf parameter and using the
				// comma (,) as separator. For now we will just comment it and the resolution will be the one
				// coming from the video (no changes)
				// FFMpegEncodingParameters::addToArguments(ffmpegVideoResolutionParameter, ffmpegEncodingProfileArgumentList);
				// ffmpegEncodingProfileArgumentList.push_back("-threads");
				// ffmpegEncodingProfileArgumentList.push_back("0");
				mainOutput.addArgs("-threads 0");
				// FFMpegEncodingParameters::addToArguments(ffmpegAudioCodecParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.withAudioCodec(ffmpegAudioCodec);
				// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegAudioBitRateParameter);
				// FFMpegEncodingParameters::addToArguments(ffmpegAudioOtherParameters, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegAudioOtherParameters);
				// FFMpegEncodingParameters::addToArguments(ffmpegAudioChannelsParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegAudioChannelsParameter);
				// FFMpegEncodingParameters::addToArguments(ffmpegAudioSampleRateParameter, ffmpegEncodingProfileArgumentList);
				mainOutput.addArgs(ffmpegAudioSampleRateParameter);
			}
			catch (runtime_error &e)
			{
				SPDLOG_ERROR(
					"ffmpeg: encodingProfileParameter retrieving failed"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", e.what(): {}",
					ingestionJobKey, encodingJobKey, e.what()
				);

				throw e;
			}
		}

		{
			tm tmUtcTimestamp = Datetime::utcSecondsToLocalTime(chrono::system_clock::to_time_t(chrono::system_clock::now()));

			_outputFfmpegPathFileName = std::format(
				"{}/{}_{}_{}_{:0>4}-{:0>2}-{:0>2}-{:0>2}-{:0>2}-{:0>2}.log", _ffmpegTempDir, "videoSpeed", _currentIngestionJobKey,
				_currentEncodingJobKey, tmUtcTimestamp.tm_year + 1900, tmUtcTimestamp.tm_mon + 1, tmUtcTimestamp.tm_mday, tmUtcTimestamp.tm_hour,
				tmUtcTimestamp.tm_min, tmUtcTimestamp.tm_sec
			);
		}

		{
			string videoPTS;
			string audioTempo;

			if (videoSpeedType == "SlowDown")
			{
				switch (videoSpeedSize)
				{
				case 1:
					videoPTS = "1.1";
					audioTempo = "(1/1.1)";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds / 100);

					break;
				case 2:
					videoPTS = "1.2";
					audioTempo = "(1/1.2)";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds * 20 / 100);

					break;
				case 3:
					videoPTS = "1.3";
					audioTempo = "(1/1.3)";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds * 30 / 100);

					break;
				case 4:
					videoPTS = "1.4";
					audioTempo = "(1/1.4)";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds * 40 / 100);

					break;
				case 5:
					videoPTS = "1.5";
					audioTempo = "(1/1.5)";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds * 50 / 100);

					break;
				case 6:
					videoPTS = "1.6";
					audioTempo = "(1/1.6)";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds * 60 / 100);

					break;
				case 7:
					videoPTS = "1.7";
					audioTempo = "(1/1.7)";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds * 70 / 100);

					break;
				case 8:
					videoPTS = "1.8";
					audioTempo = "(1/1.8)";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds * 80 / 100);

					break;
				case 9:
					videoPTS = "1.9";
					audioTempo = "(1/1.9)";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds * 90 / 100);

					break;
				case 10:
					videoPTS = "2";
					audioTempo = "0.5";
					_currentDurationInMilliSeconds += (videoDurationInMilliSeconds * 100 / 100);

					break;
				default:
					videoPTS = "1.3";
					audioTempo = "(1/1.3)";

					break;
				}
			}
			else // if (videoSpeedType == "SpeedUp")
			{
				switch (videoSpeedSize)
				{
				case 1:
					videoPTS = "(1/1.1)";
					audioTempo = "1.1";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 10 / 100);

					break;
				case 2:
					videoPTS = "(1/1.2)";
					audioTempo = "1.2";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 20 / 100);

					break;
				case 3:
					videoPTS = "(1/1.3)";
					audioTempo = "1.3";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 30 / 100);

					break;
				case 4:
					videoPTS = "(1/1.4)";
					audioTempo = "1.4";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 40 / 100);

					break;
				case 5:
					videoPTS = "(1/1.5)";
					audioTempo = "1.5";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 50 / 100);

					break;
				case 6:
					videoPTS = "(1/1.6)";
					audioTempo = "1.6";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 60 / 100);

					break;
				case 7:
					videoPTS = "(1/1.7)";
					audioTempo = "1.7";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 70 / 100);

					break;
				case 8:
					videoPTS = "(1/1.8)";
					audioTempo = "1.8";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 80 / 100);

					break;
				case 9:
					videoPTS = "(1/1.9)";
					audioTempo = "1.9";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 90 / 100);

					break;
				case 10:
					videoPTS = "0.5";
					audioTempo = "2";
					_currentDurationInMilliSeconds -= (videoDurationInMilliSeconds * 100 / 100);

					break;
				default:
					videoPTS = "(1/1.3)";
					audioTempo = "1.3";

					break;
				}
			}

			string complexFilter = "-filter_complex [0:v]setpts=" + videoPTS + "*PTS[v];[0:a]atempo=" + audioTempo + "[a]";
			string videoMap = "-map [v]";
			string audioMap = "-map [a]";

			// vector<string> ffmpegArgumentList;
			// ostringstream ffmpegArgumentListStream;
			{
				// ffmpegArgumentList.push_back("ffmpeg");
				// global options
				// ffmpegArgumentList.push_back("-y");
				ffMpegEngine.addGlobalArg("-y");
				// input options
				// ffmpegArgumentList.push_back("-i");
				// ffmpegArgumentList.push_back(mmsSourceVideoAssetPathName);
				ffMpegEngine.addInput(mmsSourceVideoAssetPathName);
				// output options
				// FFMpegEncodingParameters::addToArguments(complexFilter, ffmpegArgumentList);
				ffMpegEngine.addFilterComplex(complexFilter);
				// FFMpegEncodingParameters::addToArguments(videoMap, ffmpegArgumentList);
				mainOutput.map("[v]");
				// FFMpegEncodingParameters::addToArguments(audioMap, ffmpegArgumentList);
				mainOutput.map("[a]");

				// encoding parameters
				// if (encodingProfileDetailsRoot != nullptr)
				// {
				// 	for (string parameter : ffmpegEncodingProfileArgumentList)
				// 		FFMpegEncodingParameters::addToArguments(parameter, ffmpegArgumentList);
				// }
				//
				// ffmpegArgumentList.push_back(stagingEncodedAssetPathName);

				try
				{
					chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					// if (!ffmpegArgumentList.empty())
					// 	copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(), ostream_iterator<string>(ffmpegArgumentListStream, " "));

					SPDLOG_INFO(
						"videoSpeed: Executing ffmpeg command"
						", encodingJobKey: {}"
						", ingestionJobKey: {}"
						", ffmpegArgumentList: {}",
						encodingJobKey, ingestionJobKey, ffMpegEngine.toSingleLine()
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
							redirectionStdOutput, redirectionStdError, processId, iReturnedStatus
						);
					else
					{
						vector<string> args = ffMpegEngine.buildArgs(false);
						ProcessUtility::forkAndExec(
							_ffmpegPath + "/ffmpeg", args, _outputFfmpegPathFileName,
							redirectionStdOutput, redirectionStdError, processId, iReturnedStatus
						);
					}
					*/
					processId.reset();
					if (iReturnedStatus != 0)
					{
						SPDLOG_ERROR(
							"videoSpeed: ffmpeg command failed"
							", encodingJobKey: {}"
							", ingestionJobKey: {}"
							", iReturnedStatus: {}"
							", ffmpegArgumentList: {}",
							encodingJobKey, ingestionJobKey, iReturnedStatus, ffMpegEngine.toSingleLine()
						);

						// to hide the ffmpeg staff
						string errorMessage = std::format(
							"videoSpeed command failed"
							", encodingJobKey: {}"
							", ingestionJobKey: {}",
							encodingJobKey, ingestionJobKey
						);
						throw runtime_error(errorMessage);
					}

					chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

					SPDLOG_INFO(
						"videoSpeed: Executed ffmpeg command"
						", encodingJobKey: {}"
						", ingestionJobKey: {}"
						", ffmpegArgumentList: {}"
						", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @{}@",
						encodingJobKey, ingestionJobKey, ffMpegEngine.toSingleLine(),
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
					SPDLOG_ERROR(errorMessage);

					SPDLOG_INFO(
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

				SPDLOG_INFO(
					"remove"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", _outputFfmpegPathFileName: {}",
					ingestionJobKey, encodingJobKey, _outputFfmpegPathFileName
				);
				fs::remove_all(_outputFfmpegPathFileName);
			}

			SPDLOG_INFO(
				"VideoSpeed file generated"
				", encodingJobKey: {}"
				", ingestionJobKey: {}"
				", stagingEncodedAssetPathName: {}",
				encodingJobKey, ingestionJobKey, stagingEncodedAssetPathName
			);

			unsigned long ulFileSize = fs::file_size(stagingEncodedAssetPathName);

			if (ulFileSize == 0)
			{
				SPDLOG_ERROR(
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
		SPDLOG_ERROR(
			"ffmpeg: ffmpeg VideoSpeed failed"
			", encodingJobKey: {}"
			", ingestionJobKey: {}"
			", mmsSourceVideoAssetPathName: {}"
			", stagingEncodedAssetPathName: {}"
			", e.what(): {}",
			encodingJobKey, ingestionJobKey, mmsSourceVideoAssetPathName, stagingEncodedAssetPathName, e.what()
		);

		if (fs::exists(stagingEncodedAssetPathName))
		{
			// file in case of .3gp content OR directory in case of IPhone content
			SPDLOG_INFO(
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
