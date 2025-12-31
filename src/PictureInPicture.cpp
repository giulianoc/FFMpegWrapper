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
#include "StringUtils.h"
#include "spdlog/spdlog.h"
#include <regex>

using namespace std;
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

void FFMpegWrapper::pictureInPicture(
	const string& mmsMainVideoAssetPathName, int64_t mainVideoDurationInMilliSeconds, const string& mmsOverlayVideoAssetPathName,
	int64_t overlayVideoDurationInMilliSeconds, bool soundOfMain, const string& overlayPosition_X_InPixel, const string& overlayPosition_Y_InPixel,
	const string& overlay_Width_InPixel, const string& overlay_Height_InPixel,

	const json& encodingProfileDetailsRoot,

	string stagingEncodedAssetPathName, int64_t encodingJobKey, int64_t ingestionJobKey, ProcessUtility::ProcessId &processId,
	shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
)
{

	_currentApiName = APIName::PictureInPicture;

	setStatus(ingestionJobKey, encodingJobKey, mainVideoDurationInMilliSeconds,
		mmsMainVideoAssetPathName, stagingEncodedAssetPathName);

	try
	{
		if (!fs::exists(mmsMainVideoAssetPathName))
		{
			string errorMessage = std::format(
				"Main video asset path name not existing"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", mmsMainVideoAssetPathName: {}",
				ingestionJobKey, encodingJobKey, mmsMainVideoAssetPathName
			);
			SPDLOG_ERROR(errorMessage);

			throw runtime_error(errorMessage);
		}

		if (!fs::exists(mmsOverlayVideoAssetPathName))
		{
			string errorMessage = std::format(
				"Overlay video asset path name not existing"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", mmsOverlayVideoAssetPathName: {}",
				ingestionJobKey, encodingJobKey, mmsOverlayVideoAssetPathName
			);
			SPDLOG_ERROR(errorMessage);

			throw runtime_error(errorMessage);
		}

		// 2022-12-09: Aggiunto "- 1000" perchè in un caso era stato generato l'errore anche
		// 	per pochi millisecondi di video overlay superiore al video main
		if (mainVideoDurationInMilliSeconds < overlayVideoDurationInMilliSeconds - 1000)
		{
			string errorMessage = std::format(
				"pictureInPicture: overlay video duration cannot be bigger than main video diration"
				", encodingJobKey: {}"
				", ingestionJobKey: {}"
				", mainVideoDurationInMilliSeconds: {}"
				", overlayVideoDurationInMilliSeconds: {}",
				encodingJobKey, ingestionJobKey, mainVideoDurationInMilliSeconds, overlayVideoDurationInMilliSeconds
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
				string ffmpegHttpStreamingParameter;
				bool encodingProfileIsVideo = true;

				string ffmpegFileFormatParameter;

				string ffmpegVideoCodecParameter;
				string ffmpegVideoCodec;
				string ffmpegVideoProfileParameter;
				string ffmpegVideoResolutionParameter;
				int videoBitRateInKbps = -1;
				string ffmpegVideoBitRateParameter;
				string ffmpegVideoOtherParameters;
				string ffmpegVideoMaxRateParameter;
				string ffmpegVideoBufSizeParameter;
				string ffmpegVideoFrameRateParameter;
				string ffmpegVideoKeyFramesRateParameter;
				bool twoPasses;
				vector<tuple<string, int, int, int, string, string, string>> videoBitRatesInfo;

				string ffmpegAudioCodecParameter;
				string ffmpegAudioCodec;
				string ffmpegAudioBitRateParameter;
				string ffmpegAudioOtherParameters;
				string ffmpegAudioChannelsParameter;
				string ffmpegAudioSampleRateParameter;
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
					string errorMessage = __FILEREF__ + "in case of pictureInPicture it is not possible to have a two passes encoding"
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", twoPasses: " + to_string(twoPasses)
					;
					SPDLOG_ERROR(errorMessage);

					throw runtime_error(errorMessage);
					*/
					twoPasses = false;

					SPDLOG_WARN(
						"in case of pictureInPicture it is not possible to have a two passes encoding. Change it to false"
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
				// ffmpegEncodingProfileArgumentList.emplace_back("-threads");
				// ffmpegEncodingProfileArgumentList.emplace_back("0");
				mainOutput.addArgs(std::format("-threads 0"));
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
				"{}/{}_{}_{}_{:0>4}-{:0>2}-{:0>2}-{:0>2}-{:0>2}-{:0>2}.log", _ffmpegTempDir, "pictureInPicture", _currentIngestionJobKey,
				_currentEncodingJobKey, tmUtcTimestamp.tm_year + 1900, tmUtcTimestamp.tm_mon + 1, tmUtcTimestamp.tm_mday, tmUtcTimestamp.tm_hour,
				tmUtcTimestamp.tm_min, tmUtcTimestamp.tm_sec
			);
		}

		{
			string ffmpegOverlayPosition_X_InPixel = StringUtils::replaceAll(overlayPosition_X_InPixel, "mainVideo_width", "main_w");
			ffmpegOverlayPosition_X_InPixel = StringUtils::replaceAll(ffmpegOverlayPosition_X_InPixel, "overlayVideo_width", "overlay_w");

			string ffmpegOverlayPosition_Y_InPixel = StringUtils::replaceAll(overlayPosition_Y_InPixel, "mainVideo_height", "main_h");
			ffmpegOverlayPosition_Y_InPixel = StringUtils::replaceAll(ffmpegOverlayPosition_Y_InPixel, "overlayVideo_height", "overlay_h");

			string ffmpegOverlay_Width_InPixel = StringUtils::replaceAll(overlay_Width_InPixel, "overlayVideo_width", "iw");

			string ffmpegOverlay_Height_InPixel = StringUtils::replaceAll(overlay_Height_InPixel, "overlayVideo_height", "ih");

			/*
			string ffmpegFilterComplex = string("-filter_complex 'overlay=")
					+ ffmpegImagePosition_X_InPixel + ":"
					+ ffmpegImagePosition_Y_InPixel + "'"
					;
			*/
			string ffmpegFilterComplex = string("-filter_complex ");
			if (soundOfMain)
				ffmpegFilterComplex += "[1]scale=";
			else
				ffmpegFilterComplex += "[0]scale=";
			ffmpegFilterComplex += (ffmpegOverlay_Width_InPixel + ":" + ffmpegOverlay_Height_InPixel);
			ffmpegFilterComplex += "[pip];";

			if (soundOfMain)
				ffmpegFilterComplex += "[0][pip]overlay=";
			else
				ffmpegFilterComplex += "[pip][0]overlay=";
			ffmpegFilterComplex += (ffmpegOverlayPosition_X_InPixel + ":" + ffmpegOverlayPosition_Y_InPixel);
			// ostringstream ffmpegArgumentListStream;
			{
				// vector<string> ffmpegArgumentList;
				int iReturnedStatus = 0;
				// ffmpegArgumentList.emplace_back("ffmpeg");
				// global options
				// ffmpegArgumentList.emplace_back("-y");
				ffMpegEngine.addGlobalArg("-y");
				// input options
				if (soundOfMain)
				{
					// ffmpegArgumentList.emplace_back("-i");
					// ffmpegArgumentList.push_back(mmsMainVideoAssetPathName);
					// ffmpegArgumentList.emplace_back("-i");
					// ffmpegArgumentList.push_back(mmsOverlayVideoAssetPathName);
					ffMpegEngine.addInput(mmsMainVideoAssetPathName);
					ffMpegEngine.addInput(mmsOverlayVideoAssetPathName);
				}
				else
				{
					// ffmpegArgumentList.emplace_back("-i");
					// ffmpegArgumentList.push_back(mmsOverlayVideoAssetPathName);
					// ffmpegArgumentList.emplace_back("-i");
					// ffmpegArgumentList.push_back(mmsMainVideoAssetPathName);
					ffMpegEngine.addInput(mmsOverlayVideoAssetPathName);
					ffMpegEngine.addInput(mmsMainVideoAssetPathName);
				}
				// output options
				// FFMpegEncodingParameters::addToArguments(ffmpegFilterComplex, ffmpegArgumentList);

				// encoding parameters
				// if (encodingProfileDetailsRoot != nullptr)
				// {
				// 	for (const string& parameter : ffmpegEncodingProfileArgumentList)
				// 		FFMpegEncodingParameters::addToArguments(parameter, ffmpegArgumentList);
				// }

				// ffmpegArgumentList.push_back(stagingEncodedAssetPathName);

				try
				{
					chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					// if (!ffmpegArgumentList.empty())
					// 	copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(), ostream_iterator<string>(ffmpegArgumentListStream, " "));

					SPDLOG_INFO(
						"pictureInPicture: Executing ffmpeg command"
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
					ProcessUtility::forkAndExecByCallback(
						_ffmpegPath + "/ffmpeg", ffMpegEngine.buildArgs(true), ffmpegLineCallback,
						redirectionStdOutput, redirectionStdError, processId,
						iReturnedStatus
					);
					*/
					processId.reset();
					if (iReturnedStatus != 0)
					{
						SPDLOG_ERROR(
							"pictureInPicture: ffmpeg command failed"
							", encodingJobKey: {}"
							", ingestionJobKey: {}"
							", iReturnedStatus: {}"
							", ffmpegArgumentList: {}",
							encodingJobKey, ingestionJobKey, iReturnedStatus, ffMpegEngine.toSingleLine()
						);

						// to hide the ffmpeg staff
						string errorMessage = std::format(
							"pictureInPicture command failed"
							", encodingJobKey: {}"
							", ingestionJobKey: {}",
							encodingJobKey, ingestionJobKey
						);
						throw runtime_error(errorMessage);
					}

					chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

					SPDLOG_INFO(
						"pictureInPicture: Executed ffmpeg command"
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
				"pictureInPicture file generated"
				", encodingJobKey: {}"
				", ingestionJobKey: {}"
				", stagingEncodedAssetPathName: {}",
				encodingJobKey, ingestionJobKey, stagingEncodedAssetPathName
			);

			unsigned long ulFileSize = fs::file_size(stagingEncodedAssetPathName);

			if (ulFileSize == 0)
			{
				SPDLOG_ERROR(
					"ffmpeg: ffmpeg command failed, pictureInPicture encoded file size is 0"
					", encodingJobKey: {}"
					", ingestionJobKey: {}"
					", ffmpegArgumentList: {}",
					encodingJobKey, ingestionJobKey, ffMpegEngine.toSingleLine()
				);

				// to hide the ffmpeg staff
				string errorMessage = std::format(
					"command failed, pictureInPicture encoded file size is 0"
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
			"ffmpeg: ffmpeg pictureInPicture failed"
			", encodingJobKey: {}"
			", ingestionJobKey: {}"
			", mmsMainVideoAssetPathName: {}"
			", mmsOverlayVideoAssetPathName: {}"
			", stagingEncodedAssetPathName: {}"
			", e.what(): {}",
			encodingJobKey, ingestionJobKey, mmsMainVideoAssetPathName, mmsOverlayVideoAssetPathName, stagingEncodedAssetPathName, e.what()
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
