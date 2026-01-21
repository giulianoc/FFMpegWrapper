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
#include "JSONUtils.h"
#include "ProcessUtility.h"
#include "spdlog/fmt/fmt.h"
#include <fstream>

using namespace std;
using json = nlohmann::json;

void FFMpegWrapper::slideShow(
	int64_t ingestionJobKey, int64_t encodingJobKey, float durationOfEachSlideInSeconds, string frameRateMode, json encodingProfileDetailsRoot,
	vector<string> &imagesSourcePhysicalPaths, vector<string> &audiosSourcePhysicalPaths,
	float shortestAudioDurationInSeconds, // the shortest duration among the audios
	string encodedStagingAssetPathName, ProcessUtility::ProcessId &processId, shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
)
{
	_currentApiName = APIName::SlideShow;

	// IN CASE WE HAVE AUDIO
	//	We will stop the video at the shortest between
	//		- the duration of the slide show (sum of the duration of the images)
	//		- shortest audio
	//
	//	So, if the duration of the picture is longer than the duration of the shortest audio
	//			we have to reduce the duration of the pictures (1)
	//	    if the duration of the shortest audio is longer than the duration of the pictures
	//			we have to increase the duration of the last pictures (2)

	// CAPIRE COME MAI LA PERCENTUALE E' SEMPRE ZERO. Eliminare videoDurationInSeconds se non serve
	int64_t videoDurationInSeconds;
	if (audiosSourcePhysicalPaths.size() > 0)
	{
		if (durationOfEachSlideInSeconds * imagesSourcePhysicalPaths.size() < shortestAudioDurationInSeconds)
			videoDurationInSeconds = durationOfEachSlideInSeconds * imagesSourcePhysicalPaths.size();
		else
			videoDurationInSeconds = shortestAudioDurationInSeconds;
	}
	else
		videoDurationInSeconds = durationOfEachSlideInSeconds * imagesSourcePhysicalPaths.size();

	setStatus(
		ingestionJobKey, encodingJobKey, videoDurationInSeconds * 1000
		/*
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

	LOG_INFO(
		"Received {}"
		", ingestionJobKey: {}"
		", encodingJobKey: {}"
		", frameRateMode: {}"
		", encodedStagingAssetPathName: {}"
		", durationOfEachSlideInSeconds: {}"
		", shortestAudioDurationInSeconds: {}"
		", videoDurationInSeconds: {}",
		toString(_currentApiName), ingestionJobKey, encodingJobKey, frameRateMode, encodedStagingAssetPathName, durationOfEachSlideInSeconds,
		shortestAudioDurationInSeconds, videoDurationInSeconds
	);

	int iReturnedStatus = 0;

	string slideshowListImagesPathName = std::format("{}/{}.slideshowListImages.txt", _ffmpegTempDir, ingestionJobKey);

	{
		ofstream slideshowListFile(slideshowListImagesPathName.c_str(), ofstream::trunc);
		string lastSourcePhysicalPath;
		for (int imageIndex = 0; imageIndex < imagesSourcePhysicalPaths.size(); imageIndex++)
		{
			string sourcePhysicalPath = imagesSourcePhysicalPaths[imageIndex];
			double slideDurationInSeconds;

			if (!fs::exists(sourcePhysicalPath))
			{
				string errorMessage = std::format(
					"Source asset path name not existing"
					", ingestionJobKey: {}"
					// + ", encodingJobKey: " + to_string(encodingJobKey)
					", sourcePhysicalPath: {}",
					ingestionJobKey, sourcePhysicalPath
				);
				LOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}

			if (audiosSourcePhysicalPaths.size() > 0)
			{
				if (imageIndex + 1 >= imagesSourcePhysicalPaths.size() &&
					durationOfEachSlideInSeconds * (imageIndex + 1) < shortestAudioDurationInSeconds)
				{
					// we are writing the last image and the duration of all the slides
					// is less than the shortest audio duration (2)
					slideDurationInSeconds = shortestAudioDurationInSeconds - (durationOfEachSlideInSeconds * imageIndex);
				}
				else
				{
					// check case (1)

					if (durationOfEachSlideInSeconds * (imageIndex + 1) <= shortestAudioDurationInSeconds)
						slideDurationInSeconds = durationOfEachSlideInSeconds;
					else if (durationOfEachSlideInSeconds * (imageIndex + 1) > shortestAudioDurationInSeconds)
					{
						// if we are behind shortestAudioDurationInSeconds, we have to add
						// the remaining secondsand we have to terminate (next 'if' checks if before
						// we were behind just break)
						if (durationOfEachSlideInSeconds * imageIndex >= shortestAudioDurationInSeconds)
							break;
						else
							slideDurationInSeconds = (durationOfEachSlideInSeconds * (imageIndex + 1)) - shortestAudioDurationInSeconds;
					}
				}
			}
			else
				slideDurationInSeconds = durationOfEachSlideInSeconds;

			// https://trac.ffmpeg.org/wiki/Slideshow
			slideshowListFile << "file '" << sourcePhysicalPath << "'" << endl;
			LOG_INFO(
				"slideShow"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", line: file '{}'",
				ingestionJobKey, encodingJobKey, sourcePhysicalPath
			);
			slideshowListFile << "duration " << slideDurationInSeconds << endl;
			LOG_INFO(
				"slideShow"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", line: duration {}",
				ingestionJobKey, encodingJobKey, slideDurationInSeconds
			);

			lastSourcePhysicalPath = sourcePhysicalPath;
		}
		slideshowListFile << "file '" << lastSourcePhysicalPath << "'" << endl;
		LOG_INFO(
			"slideShow"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", line: file '{}'",
			ingestionJobKey, encodingJobKey, lastSourcePhysicalPath
		);
		slideshowListFile.close();
	}

	string slideshowListAudiosPathName = std::format("{}/{}.slideshowListAudios.txt", _ffmpegTempDir, ingestionJobKey);

	if (audiosSourcePhysicalPaths.size() > 1)
	{
		ofstream slideshowListFile(slideshowListAudiosPathName.c_str(), ofstream::trunc);
		string lastSourcePhysicalPath;
		for (string sourcePhysicalPath : audiosSourcePhysicalPaths)
		{
			if (!fs::exists(sourcePhysicalPath))
			{
				string errorMessage = std::format(
					"Source asset path name not existing"
					", ingestionJobKey: {}"
					// + ", encodingJobKey: " + to_string(encodingJobKey)
					", sourcePhysicalPath: {}",
					ingestionJobKey, sourcePhysicalPath
				);
				LOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}

			slideshowListFile << "file '" << sourcePhysicalPath << "'" << endl;

			lastSourcePhysicalPath = sourcePhysicalPath;
		}
		slideshowListFile << "file '" << lastSourcePhysicalPath << "'" << endl;
		slideshowListFile.close();
	}

	FFMpegEngine ffMpegEngine;

	FFMpegEngine::Output mainOutput = ffMpegEngine.addOutput(encodedStagingAssetPathName);

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

				ffmpegVideoCodecParameter, ffmpegVideoCodec, ffmpegVideoProfileParameter, ffmpegVideoOtherParameters, twoPasses, ffmpegVideoFrameRateParameter,
				ffmpegVideoKeyFramesRateParameter,

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
				LOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}
			else */
			if (twoPasses)
			{
				// siamo sicuri che non sia possibile?
				/*
				string errorMessage = __FILEREF__ + "in case of introOutroOverlay it is not possible to have a two passes encoding"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", twoPasses: " + to_string(twoPasses)
				;
				LOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
				*/
				twoPasses = false;

				LOG_WARN(
					"in case of introOutroOverlay it is not possible to have a two passes encoding. Change it to false"
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
			mainOutput.withVideoCodec(ffmpegAudioCodec);
			// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegEncodingProfileArgumentList);
			mainOutput.addArgs(ffmpegAudioBitRateParameter);
			// FFMpegEncodingParameters::addToArguments(ffmpegAudioOtherParameters, ffmpegEncodingProfileArgumentList);
			mainOutput.addArgs(ffmpegAudioOtherParameters);
			// FFMpegEncodingParameters::addToArguments(ffmpegAudioChannelsParameter, ffmpegEncodingProfileArgumentList);
			mainOutput.addArgs(ffmpegAudioChannelsParameter);
			// FFMpegEncodingParameters::addToArguments(ffmpegAudioSampleRateParameter, ffmpegEncodingProfileArgumentList);
			mainOutput.addArgs(ffmpegAudioSampleRateParameter);
		}
		catch (exception &e)
		{
			LOG_ERROR(
				"ffmpeg: encodingProfileParameter retrieving failed"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", e.what(): {}"
				", encodingProfileDetailsRoot: {}",
				ingestionJobKey, encodingJobKey, e.what(), JSONUtils::toString(encodingProfileDetailsRoot)
			);

			// to hide the ffmpeg staff
			string errorMessage = std::format(
				"encodingProfileParameter retrieving failed"
				" ingestionJobKey: {}"
				", encodingJobKey: {}"
				", e.what(): {}",
				ingestionJobKey, encodingJobKey, e.what()
			);
			throw;
		}
	}
	else
	{
		mainOutput.withVideoCodec("libx264");
		mainOutput.addArgs("-r 25");
	}

	{
		tm tmUtcTimestamp = Datetime::utcSecondsToLocalTime(chrono::system_clock::to_time_t(chrono::system_clock::now()));

		_outputFfmpegPathFileName = std::format(
			"{}/{}_{}_{:0>4}-{:0>2}-{:0>2}-{:0>2}-{:0>2}-{:0>2}.log", _ffmpegTempDir, "slideShow", _currentIngestionJobKey,
			tmUtcTimestamp.tm_year + 1900, tmUtcTimestamp.tm_mon + 1, tmUtcTimestamp.tm_mday, tmUtcTimestamp.tm_hour, tmUtcTimestamp.tm_min,
			tmUtcTimestamp.tm_sec
		);
	}

	// Then you can stream copy or re-encode your files
	// The -safe 0 above is not required if the paths are relative
	// ffmpeg -f concat -safe 0 -i mylist.txt -c copy output

	// https://www.imakewebsites.ca/posts/2016/10/30/ffmpeg-concatenating-with-image-sequences-and-audio/
	// vector<string> ffmpegArgumentList;
	// ostringstream ffmpegArgumentListStream;

	// ffmpegArgumentList.push_back("ffmpeg");
	{
		FFMpegEngine::Input input = ffMpegEngine.addInput(slideshowListImagesPathName);
		// ffmpegArgumentList.push_back("-f");
		// ffmpegArgumentList.push_back("concat");
		// ffmpegArgumentList.push_back("-safe");
		// ffmpegArgumentList.push_back("0");
		input.addArgs("-f concat -safe 0");
		// ffmpegArgumentList.push_back("-i");
		// ffmpegArgumentList.push_back(slideshowListImagesPathName);
	}
	if (audiosSourcePhysicalPaths.size() == 1)
	{
		// ffmpegArgumentList.push_back("-i");
		// ffmpegArgumentList.push_back(audiosSourcePhysicalPaths[0]);
		ffMpegEngine.addInput(audiosSourcePhysicalPaths[0]);
	}
	else if (audiosSourcePhysicalPaths.size() > 1)
	{
		FFMpegEngine::Input input = ffMpegEngine.addInput(slideshowListAudiosPathName);
		// ffmpegArgumentList.push_back("-f");
		// ffmpegArgumentList.push_back("concat");
		// ffmpegArgumentList.push_back("-safe");
		// ffmpegArgumentList.push_back("0");
		input.addArgs("-f concat -safe 0");
		// ffmpegArgumentList.push_back("-i");
		// ffmpegArgumentList.push_back(slideshowListAudiosPathName);
	}

	// encoding parameters
	// if (encodingProfileDetailsRoot != nullptr)
	// {
	// 	for (string parameter : ffmpegEncodingProfileArgumentList)
	// 		FFMpegEncodingParameters::addToArguments(parameter, ffmpegArgumentList);
	// }
	// else
	// {
	// 	ffmpegArgumentList.push_back("-c:v");
	// 	ffmpegArgumentList.push_back("libx264");
	// 	ffmpegArgumentList.push_back("-r");
	// 	ffmpegArgumentList.push_back("25");
	// }

	// ffmpegArgumentList.push_back("-fps_mode");
	// ffmpegArgumentList.push_back(frameRateMode);
	mainOutput.addArgs(std::format("-fps_mode {}", frameRateMode));
	// ffmpegArgumentList.push_back("-pix_fmt");
	// yuv420p: the only option for broad compatibility
	// ffmpegArgumentList.push_back("yuv420p");
	mainOutput.addArgs("-pix_fmt yuv420p");
	if (audiosSourcePhysicalPaths.size() > 0)
		// ffmpegArgumentList.push_back("-shortest");
		mainOutput.addArgs("-shortest");
	// ffmpegArgumentList.push_back(encodedStagingAssetPathName);

	try
	{
		chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

		// if (!ffmpegArgumentList.empty())
		// 	copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(), ostream_iterator<string>(ffmpegArgumentListStream, " "));

		LOG_INFO(
			"slideShow: Executing ffmpeg command"
			", ingestionJobKey: {}"
			", ffmpegArgumentList: {}",
			ingestionJobKey, ffMpegEngine.toSingleLine()
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
			redirectionStdOutput, redirectionStdError, processId,iReturnedStatus
		);
		*/
		processId.reset();
		if (iReturnedStatus != 0)
		{
			LOG_ERROR(
				"slideShow: ffmpeg command failed"
				", ingestionJobKey: {}"
				", iReturnedStatus: {}"
				", ffmpegArgumentList: {}",
				ingestionJobKey, iReturnedStatus, ffMpegEngine.toSingleLine()
			);

			// to hide the ffmpeg staff
			string errorMessage = string("slideShow: command failed") + ", ingestionJobKey: " + to_string(ingestionJobKey);
			throw runtime_error(errorMessage);
		}

		chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

		LOG_INFO(
			"slideShow: Executed ffmpeg command"
			", ingestionJobKey: {}"
			", ffmpegArgumentList: {}"
			", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @{}@",
			ingestionJobKey, ffMpegEngine.toSingleLine(), chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()
		);
	}
	catch (exception &e)
	{
		processId.reset();

		// string lastPartOfFfmpegOutputFile = getLastPartOfFile(_outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
		string errorMessage;
		if (iReturnedStatus == 9) // 9 means: SIGKILL
			errorMessage = std::format(
				"ffmpeg: ffmpeg command failed because killed by the user"
				", _outputFfmpegPathFileName: {}"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", ffmpegArgumentList: {}"
				", e.what(): {}",
				_outputFfmpegPathFileName, ingestionJobKey, encodingJobKey, ffMpegEngine.toSingleLine(), e.what()
			);
		else
			errorMessage = std::format(
				"ffmpeg: ffmpeg command failed"
				", _outputFfmpegPathFileName: {}"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", ffmpegArgumentList: {}"
				", e.what(): {}",
				_outputFfmpegPathFileName, ingestionJobKey, encodingJobKey, ffMpegEngine.toSingleLine(), e.what()
			);
		LOG_ERROR(errorMessage);

		LOG_INFO(
			"Remove"
			", _outputFfmpegPathFileName: {}",
			_outputFfmpegPathFileName
		);
		fs::remove_all(_outputFfmpegPathFileName);

		if (fs::exists(slideshowListImagesPathName.c_str()))
		{
			LOG_INFO(
				"Remove"
				", slideshowListImagesPathName: {}",
				slideshowListImagesPathName
			);
			fs::remove_all(slideshowListImagesPathName);
		}
		if (fs::exists(slideshowListAudiosPathName.c_str()))
		{
			LOG_INFO(
				"Remove"
				", slideshowListAudiosPathName: {}",
				slideshowListAudiosPathName
			);
			fs::remove_all(slideshowListAudiosPathName);
		}

		if (iReturnedStatus == 9) // 9 means: SIGKILL
			throw FFMpegEncodingKilledByUser();
		else
			throw e;
	}

	LOG_INFO(
		"Remove"
		", _outputFfmpegPathFileName: {}",
		_outputFfmpegPathFileName
	);
	fs::remove_all(_outputFfmpegPathFileName);

	if (fs::exists(slideshowListImagesPathName.c_str()))
	{
		LOG_INFO(
			"Remove"
			", slideshowListImagesPathName: {}",
			slideshowListImagesPathName
		);
		fs::remove_all(slideshowListImagesPathName);
	}
	if (fs::exists(slideshowListAudiosPathName.c_str()))
	{
		LOG_INFO(
			"Remove"
			", slideshowListAudiosPathName: {}",
			slideshowListAudiosPathName
		);
		fs::remove_all(slideshowListAudiosPathName);
	}
}
