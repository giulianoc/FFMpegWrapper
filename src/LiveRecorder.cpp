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
#include "FFMpegEngine.h"
#include "FFMpegFilters.h"
#include "FFMpegWrapper.h"
#include "JSONUtils.h"
#include "ProcessUtility.h"
#include "StringUtils.h"
#include "spdlog/spdlog.h"

using namespace std;
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;


void FFMpegWrapper::liveRecorder(
	int64_t ingestionJobKey, int64_t encodingJobKey, bool externalEncoder, const string& segmentListPathName, const string& recordedFileNamePrefix,

	const string& otherInputOptions,

	// if streamSourceType is IP_PUSH means the liveURL should be like
	//		rtmp://<local transcoder IP to bind>:<port>
	//		listening for an incoming connection
	// else if streamSourceType is CaptureLive, liveURL is not used
	// else means the liveURL is "any thing" referring a stream
	const string& streamSourceType, // IP_PULL, TV, IP_PUSH, CaptureLive
	string liveURL,
	// Used only in case streamSourceType is IP_PUSH, Maximum time to wait for the incoming connection
	int pushListenTimeout,

	// parameters used only in case streamSourceType is CaptureLive
	int captureLive_videoDeviceNumber, const string& captureLive_videoInputFormat, int captureLive_frameRate, int captureLive_width, int captureLive_height,
	int captureLive_audioDeviceNumber, int captureLive_channelsNumber,

	bool utcTimeOverlay,

	const string_view& userAgent, time_t utcRecordingPeriodStart, time_t utcRecordingPeriodEnd,

	int segmentDurationInSeconds, const string& outputFileFormat,
	const string& otherOutputOptions,
	const string& segmenterType, // streamSegmenter or hlsSegmenter

	const json& outputsRoot,

	json framesToBeDetectedRoot,

	shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData,

	ProcessUtility::ProcessId &processId, optional<chrono::system_clock::time_point>& recordingStart,
	long *numberOfRestartBecauseOfFailure
)
{
	_currentApiName = APIName::LiveRecorder;

	SPDLOG_INFO(
		"Received {}"
		", ingestionJobKey: {}"
		", encodingJobKey: {}",
		toString(_currentApiName), ingestionJobKey, encodingJobKey
	);

	setStatus(
		ingestionJobKey, encodingJobKey
		/*
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

	FFMpegEngine ffMpegEngine;
	// vector<string> ffmpegArgumentList;
	// ostringstream ffmpegArgumentListStream;
	int iReturnedStatus = 0;
	string segmentListPath;

	try
	{
		time_t utcNow;
		chrono::system_clock::time_point endFfmpegCommand;
		chrono::system_clock::time_point startFfmpegCommand;
		segmentListPath = StringUtils::uriPathPrefix(segmentListPathName, true);

		// directory is created by EncoderVideoAudioProxy using MMSStorage::getStagingAssetPathName
		// I saw just once that the directory was not created and the liveencoder remains in the loop
		// where:
		//	1. the encoder returns an error becaise of the missing directory
		//	2. EncoderVideoAudioProxy calls again the encoder
		// So, for this reason, the below check is done
		if (!fs::exists(segmentListPath))
		{
			SPDLOG_WARN(
				"segmentListPath does not exist!!! It will be created"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", segmentListPath: {}",
				ingestionJobKey, encodingJobKey, segmentListPath
			);

			SPDLOG_INFO(
				"Create directory"
				", segmentListPath: {}",
				segmentListPath
			);
			fs::create_directories(segmentListPath);
			fs::permissions(
				segmentListPath,
				fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec | fs::perms::group_read | fs::perms::group_exec |
					fs::perms::others_read | fs::perms::others_exec,
				fs::perm_options::replace
			);
		}

		{
			chrono::system_clock::time_point now = chrono::system_clock::now();
			utcNow = chrono::system_clock::to_time_t(now);
		}

		// 2021-03-06: I saw even if ffmpeg starts exactly at utcRecordingPeriodStart, the segments start time
		//	is about utcRecordingPeriodStart + 5 seconds.
		//	So, to be sure we have the recording at utcRecordingPeriodStart, we have to start ffmpeg
		//	at lease 5 seconds ahead
		int secondsAheadToStartFfmpeg = 10;
		time_t utcRecordingPeriodStartFixed = utcRecordingPeriodStart - secondsAheadToStartFfmpeg;
		if (utcNow < utcRecordingPeriodStartFixed)
		{
			// 2019-12-19: since the first chunk is removed, we will start a bit early
			// than utcRecordingPeriodStart (50% less than segmentDurationInSeconds)
			long secondsToStartEarly;
			if (segmenterType == "streamSegmenter")
				secondsToStartEarly = segmentDurationInSeconds * 50 / 100;
			else
				secondsToStartEarly = 0;

			while (utcNow + secondsToStartEarly < utcRecordingPeriodStartFixed)
			{
				time_t sleepTime = utcRecordingPeriodStartFixed - (utcNow + secondsToStartEarly);

				SPDLOG_INFO(
					"LiveRecorder timing. Too early to start the LiveRecorder, just sleep {} seconds"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", utcNow: {}"
					", secondsToStartEarly: {}"
					", utcRecordingPeriodStartFixed: {}",
					sleepTime, ingestionJobKey, encodingJobKey, utcNow, secondsToStartEarly, utcRecordingPeriodStartFixed
				);

				this_thread::sleep_for(chrono::seconds(sleepTime));

				{
					chrono::system_clock::time_point now = chrono::system_clock::now();
					utcNow = chrono::system_clock::to_time_t(now);
				}
			}
		}
		else if (utcRecordingPeriodEnd <= utcNow)
		{
			time_t tooLateTime = utcNow - utcRecordingPeriodEnd;

			string errorMessage = std::format(
				"LiveRecorder timing. Too late to start the LiveRecorder"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", utcNow: {}"
				", utcRecordingPeriodStartFixed: {}"
				", utcRecordingPeriodEnd: {}"
				", tooLateTime: {}",
				ingestionJobKey, encodingJobKey, utcNow, utcRecordingPeriodStartFixed, utcRecordingPeriodEnd, tooLateTime
			);
			SPDLOG_ERROR(errorMessage);

			throw runtime_error(errorMessage);
		}
		else
		{
			time_t delayTime = utcNow - utcRecordingPeriodStartFixed;

			SPDLOG_WARN(
				"LiveRecorder timing. We are a bit late to start the LiveRecorder, let's start it"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", utcNow: {}"
				", utcRecordingPeriodStartFixed: {}"
				", utcRecordingPeriodEnd: {}"
				", delayTime: {}",
				ingestionJobKey, encodingJobKey, utcNow, utcRecordingPeriodStartFixed, utcRecordingPeriodEnd, delayTime
			);
		}

		{
			tm tmUtcTimestamp = Datetime::utcSecondsToLocalTime(chrono::system_clock::to_time_t(chrono::system_clock::now()));

			_outputFfmpegPathFileName = std::format(
				"{}/{}_{}_{}_{:0>4}-{:0>2}-{:0>2}-{:0>2}-{:0>2}-{:0>2}.log", _ffmpegTempDir, "liveRecorder", _currentIngestionJobKey,
				_currentEncodingJobKey, tmUtcTimestamp.tm_year + 1900, tmUtcTimestamp.tm_mon + 1, tmUtcTimestamp.tm_mday, tmUtcTimestamp.tm_hour,
				tmUtcTimestamp.tm_min, tmUtcTimestamp.tm_sec
			);
		}

		string recordedFileNameTemplate = recordedFileNamePrefix;
		if (segmenterType == "streamSegmenter")
			recordedFileNameTemplate += "_%Y-%m-%d_%H-%M-%S_%s."; // viene letto il timestamp dal nome del file
		else													  // if (segmenterType == "hlsSegmenter")
			recordedFileNameTemplate += "_%04d.";				  // non viene letto il timestamp dal nome del file
		recordedFileNameTemplate += outputFileFormat;

		time_t streamingDuration = utcRecordingPeriodEnd - utcNow;

		SPDLOG_INFO(
			"LiveRecording timing. Streaming duration"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", utcNow: {}"
			", utcRecordingPeriodStart: {}"
			", utcRecordingPeriodEnd: {}"
			", streamingDuration: {}",
			ingestionJobKey, encodingJobKey, utcNow, utcRecordingPeriodStart, utcRecordingPeriodEnd, streamingDuration
		);

		time_t localPushListenTimeout = pushListenTimeout;
		if (streamSourceType == "IP_PUSH" || streamSourceType == "TV")
		{
			if (localPushListenTimeout > 0 && localPushListenTimeout > streamingDuration)
			{
				// 2021-02-02: sceanrio:
				//	streaming duration is 25 seconds
				//	timeout: 3600 seconds
				//	The result is that the process will finish after 3600 seconds, not after 25 seconds
				//	To avoid that, in this scenario, we will set the timeout equals to streamingDuration
				SPDLOG_INFO(
					"LiveRecorder timing. Listen timeout in seconds is reduced because max after 'streamingDuration' the process has to finish"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", utcNow: {}"
					", utcRecordingPeriodStart: {}"
					", utcRecordingPeriodEnd: {}"
					", streamingDuration: {}"
					", localPushListenTimeout: {}",
					ingestionJobKey, encodingJobKey, utcNow, utcRecordingPeriodStart, utcRecordingPeriodEnd, streamingDuration, localPushListenTimeout
				);

				localPushListenTimeout = streamingDuration;
			}
		}

		// user agent is an HTTP header and can be used only in case of http request
		bool userAgentToBeUsed = false;
		if (streamSourceType == "IP_PULL" && !userAgent.empty())
		{
			string httpPrefix = "http"; // it includes also https
			if (liveURL.size() >= httpPrefix.size() && liveURL.compare(0, httpPrefix.size(), httpPrefix) == 0)
			{
				userAgentToBeUsed = true;
			}
			else
			{
				SPDLOG_WARN(
					"user agent cannot be used if not http"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", liveURL: {}",
					ingestionJobKey, encodingJobKey, liveURL
				);
			}
		}

		// ffmpegArgumentList.emplace_back("ffmpeg");

		auto& mainInput = ffMpegEngine.addInput();

		if (userAgentToBeUsed)
		{
			mainInput.addArg("-user_agent");
			mainInput.addArg(userAgent);
		}
		// {
		// 	ffmpegArgumentList.emplace_back("-user_agent");
		// 	ffmpegArgumentList.push_back(userAgent);
		// }

		if (!otherInputOptions.empty())
			mainInput.addArgs(otherInputOptions);
			// FFMpegEncodingParameters::addToArguments(otherInputOptions, ffmpegArgumentList);

		if (framesToBeDetectedRoot != nullptr && !framesToBeDetectedRoot.empty())
		{
			// 2022-05-28; in caso di framedetection, we will "fix" PTS
			//	otherwise the one logged seems are not correct.
			//	Using +genpts are OK
			mainInput.addArgs("-fflags +genpts");
			// ffmpegArgumentList.emplace_back("-fflags");
			// ffmpegArgumentList.emplace_back("+genpts");
		}

		if (streamSourceType == "IP_PUSH")
		{
			// listen/timeout depend on the protocol (https://ffmpeg.org/ffmpeg-protocols.html)
			if (liveURL.find("http://") != string::npos || liveURL.find("rtmp://") != string::npos)
			{
				mainInput.addArgs("-listen 1");
				// ffmpegArgumentList.emplace_back("-listen");
				// ffmpegArgumentList.emplace_back("1");
				if (localPushListenTimeout > 0)
				{
					// no timeout means it will listen infinitely
					mainInput.addArgs(std::format("-timeout {}", localPushListenTimeout));
					// ffmpegArgumentList.emplace_back("-timeout");
					// ffmpegArgumentList.push_back(std::format("{}", localPushListenTimeout));
				}
			}
			else if (liveURL.find("udp://") != string::npos)
			{
				if (localPushListenTimeout > 0)
				{
					int64_t listenTimeoutInMicroSeconds = localPushListenTimeout;
					listenTimeoutInMicroSeconds *= 1000000;
					liveURL += "?timeout=" + to_string(listenTimeoutInMicroSeconds);
				}
			}
			else
			{
				SPDLOG_ERROR(
					"listen/timeout not managed yet for the current protocol"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", liveURL: {}",
					ingestionJobKey, encodingJobKey, liveURL
				);
			}

			mainInput.setSource(liveURL);
			// ffmpegArgumentList.emplace_back("-i");
			// ffmpegArgumentList.push_back(liveURL);
		}
		else if (streamSourceType == "IP_PULL" || streamSourceType == "TV")
		{
			mainInput.setSource(liveURL);
			// ffmpegArgumentList.emplace_back("-i");
			// ffmpegArgumentList.push_back(liveURL);
		}
		else if (streamSourceType == "CaptureLive")
		{
			// video
			{
				// -f v4l2 -framerate 25 -video_size 640x480 -i /dev/video0
				mainInput.addArgs("-f v4l2 -thread_queue_size 4096");
				// ffmpegArgumentList.emplace_back("-f");
				// ffmpegArgumentList.emplace_back("v4l2");
				// ffmpegArgumentList.emplace_back("-thread_queue_size");
				// ffmpegArgumentList.emplace_back("4096");

				if (!captureLive_videoInputFormat.empty())
				{
					mainInput.addArgs(std::format("-input_format {}", captureLive_videoInputFormat));
					// ffmpegArgumentList.emplace_back("-input_format");
					// ffmpegArgumentList.push_back(captureLive_videoInputFormat);
				}

				if (captureLive_frameRate != -1)
				{
					mainInput.addArgs(std::format("-framerate {}", captureLive_frameRate));
					// ffmpegArgumentList.emplace_back("-framerate");
					// ffmpegArgumentList.push_back(to_string(captureLive_frameRate));
				}

				if (captureLive_width != -1 && captureLive_height != -1)
				{
					mainInput.addArgs(std::format("-video_size {}x{}", captureLive_width, captureLive_height));
					// ffmpegArgumentList.emplace_back("-video_size");
					// ffmpegArgumentList.push_back(to_string(captureLive_width) + "x" + to_string(captureLive_height));
				}

				mainInput.setSource(std::format("/dev/video{}", captureLive_videoDeviceNumber));
				// ffmpegArgumentList.emplace_back("-i");
				// ffmpegArgumentList.push_back(string("/dev/video") + to_string(captureLive_videoDeviceNumber));
			}

			// audio
			{
				auto& audioInput = ffMpegEngine.addInput(std::format("hw:{}", captureLive_audioDeviceNumber));

				audioInput.addArgs("-f alsa -thread_queue_size 2048");
				// ffmpegArgumentList.emplace_back("-f");
				// ffmpegArgumentList.emplace_back("alsa");
				//
				// ffmpegArgumentList.emplace_back("-thread_queue_size");
				// ffmpegArgumentList.emplace_back("2048");

				if (captureLive_channelsNumber != -1)
				{
					audioInput.addArgs(std::format("-ac {}", captureLive_channelsNumber));
					// ffmpegArgumentList.emplace_back("-ac");
					// ffmpegArgumentList.push_back(to_string(captureLive_channelsNumber));
				}

				// ffmpegArgumentList.emplace_back("-i");
				// ffmpegArgumentList.push_back(string("hw:") + to_string(captureLive_audioDeviceNumber));
			}
		}

		// to detect a frame we have to:
		// 1. add -r 1 -loop 1 -i <picture path name of the frame to be detected>
		// 2. add: -filter_complex "[0:v][1:v]blend=difference:shortest=1,blackframe=99:32[f]" -map "[f]" -f null -
		if (framesToBeDetectedRoot != nullptr && !framesToBeDetectedRoot.empty())
		{
			for (const auto & frameToBeDetectedRoot : framesToBeDetectedRoot)
			{
				if (JSONUtils::isPresent(frameToBeDetectedRoot, "picturePathName"))
				{
					string picturePathName = JSONUtils::asString(frameToBeDetectedRoot, "picturePathName", "");

					auto& pictureInput = ffMpegEngine.addInput(picturePathName);
					pictureInput.addArgs("-r 1 -loop 1");
					// ffmpegArgumentList.emplace_back("-r");
					// ffmpegArgumentList.emplace_back("1");
					//
					// ffmpegArgumentList.emplace_back("-loop");
					// ffmpegArgumentList.emplace_back("1");

					// ffmpegArgumentList.emplace_back("-i");
					// ffmpegArgumentList.push_back(picturePathName);
				}
			}
		}

		mainInput.setDurationSeconds(streamingDuration);
		// int streamingDurationIndex;
		// {
		// 	ffmpegArgumentList.emplace_back("-t");
		// 	streamingDurationIndex = ffmpegArgumentList.size();
		// 	ffmpegArgumentList.push_back(to_string(streamingDuration));
		// }

		auto& mainOutput = ffMpegEngine.addOutput();

		if (utcTimeOverlay)
		{
			{
				FFMpegFilters ffmpegFilters(_ffmpegTempDir, _ffmpegTtfFontDir, ingestionJobKey, encodingJobKey, -1);
				mainOutput.addVideoFilter(ffmpegFilters.getFilter(FFMpegFilters::createTimecodeDrawTextFilter(), nullopt));

				// json filtersRoot;
				// json videoFiltersRoot = json::array();
				//
				// videoFiltersRoot.push_back(FFMpegFilters::createTimecodeDrawTextFilter());
				// filtersRoot["video"] = videoFiltersRoot;
				//
				// string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, "", "", -1);
				//
				// ffmpegArgumentList.emplace_back("-filter:v");
				// ffmpegArgumentList.push_back(videoFilters);
			}

			// usiamo un codec di default
			{
				mainOutput.withVideoCodec("libx264");
				mainOutput.withAudioCodec("aac");
				// ffmpegArgumentList.emplace_back("-codec:v");
				// ffmpegArgumentList.emplace_back("libx264");
				// ffmpegArgumentList.emplace_back("-acodec");
				// ffmpegArgumentList.emplace_back("aac");
			}
		}
		else
		{
			// this is to get all video/audio tracks
			mainOutput.map("0:v").map("0:a");

			// ad esempio potrebbe essere usato per aggiungere il mapping della traccia dei timecode
			if (!otherOutputOptions.empty())
				mainOutput.addArgs(otherOutputOptions);

			// copia tutto cio che è stato mappato
			mainOutput.setCopyAllTracks(true);

			// this is to get all video tracks
			// ffmpegArgumentList.emplace_back("-map");
			// ffmpegArgumentList.emplace_back("0:v");
			//
			// ffmpegArgumentList.emplace_back("-c:v");
			// ffmpegArgumentList.emplace_back("copy");

			// this is to get all audio tracks
			// ffmpegArgumentList.emplace_back("-map");
			// ffmpegArgumentList.emplace_back("0:a");
			//
			// ffmpegArgumentList.emplace_back("-c:a");
			// ffmpegArgumentList.emplace_back("copy");
		}

		if (segmenterType == "streamSegmenter")
		{
			mainOutput.setPath(std::format("{}/{}", segmentListPath, recordedFileNameTemplate));
			mainOutput.addArgs(std::format(
				"-f segment "
				"-segment_list {} "
				"-segment_time {} "
				"-segment_atclocktime 1 "
				"-strftime 1 ",
				segmentListPathName, segmentDurationInSeconds
				));
			// ffmpegArgumentList.emplace_back("-f");
			// ffmpegArgumentList.emplace_back("segment");
			// ffmpegArgumentList.emplace_back("-segment_list");
			// ffmpegArgumentList.push_back(segmentListPathName);
			// ffmpegArgumentList.emplace_back("-segment_time");
			// ffmpegArgumentList.push_back(to_string(segmentDurationInSeconds));
			// ffmpegArgumentList.emplace_back("-segment_atclocktime");
			// ffmpegArgumentList.emplace_back("1");
			// ffmpegArgumentList.emplace_back("-strftime");
			// ffmpegArgumentList.emplace_back("1");
			// ffmpegArgumentList.push_back(segmentListPath + "/" + recordedFileNameTemplate);
		}
		else // if (segmenterType == "hlsSegmenter")
		{
			mainOutput.setPath(segmentListPathName);

			// delete_segments: Segment files removed from the playlist are deleted after a period of time
			// equal to the duration of the segment plus the duration of the playlist
			// hls_delete_threshold: Set the number of unreferenced segments to keep on disk
			// before 'hls_flags delete_segments' deletes them. Increase this to allow continue clients
			// to download segments which were recently referenced in the playlist.
			// Default value is 1, meaning segments older than hls_list_size+1 will be deleted.
			mainOutput.addArgs(std::format(
				"-hls_flags append_list"
				" -hls_time {} "
				"-hls_list_size 10 "
				"-hls_flags delete_segments "
				"-hls_delete_threshold 1 "
				"-hls_flags program_date_time "
				"-hls_segment_filename {}/{} "
				"-f hls",
				segmentDurationInSeconds, segmentListPath, recordedFileNameTemplate));
			// ffmpegArgumentList.emplace_back("-hls_flags");
			// ffmpegArgumentList.emplace_back("append_list");
			// ffmpegArgumentList.emplace_back("-hls_time");
			// ffmpegArgumentList.push_back(to_string(segmentDurationInSeconds));
			// ffmpegArgumentList.emplace_back("-hls_list_size");
			// ffmpegArgumentList.emplace_back("10");
			//
			// // Segment files removed from the playlist are deleted after a period of time
			// // equal to the duration of the segment plus the duration of the playlist
			// ffmpegArgumentList.emplace_back("-hls_flags");
			// ffmpegArgumentList.emplace_back("delete_segments");
			//
			// // Set the number of unreferenced segments to keep on disk
			// // before 'hls_flags delete_segments' deletes them. Increase this to allow continue clients
			// // to download segments which were recently referenced in the playlist.
			// // Default value is 1, meaning segments older than hls_list_size+1 will be deleted.
			// ffmpegArgumentList.emplace_back("-hls_delete_threshold");
			// ffmpegArgumentList.push_back(to_string(1));
			//
			// ffmpegArgumentList.emplace_back("-hls_flags");
			// ffmpegArgumentList.emplace_back("program_date_time");
			//
			// ffmpegArgumentList.emplace_back("-hls_segment_filename");
			// ffmpegArgumentList.push_back(segmentListPath + "/" + recordedFileNameTemplate);
			//
			// // Start the playlist sequence number (#EXT-X-MEDIA-SEQUENCE) based on the current
			// // date/time as YYYYmmddHHMMSS. e.g. 20161231235759
			// // 2020-07-11: For the Live-Grid task, without -hls_start_number_source we have video-audio out of sync
			// // 2020-07-19: commented, if it is needed just test it
			// // ffmpegArgumentList.push_back("-hls_start_number_source");
			// // ffmpegArgumentList.push_back("datetime");
			//
			// // 2020-07-19: commented, if it is needed just test it
			// // ffmpegArgumentList.push_back("-start_number");
			// // ffmpegArgumentList.push_back(to_string(10));
			//
			// ffmpegArgumentList.emplace_back("-f");
			// ffmpegArgumentList.emplace_back("hls");
			// ffmpegArgumentList.push_back(segmentListPathName);
		}

		SPDLOG_INFO(
			"outputsRootToFfmpeg..."
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", outputsRoot.size: {}",
			ingestionJobKey, encodingJobKey, outputsRoot.size()
		);
		// TODO
		/*
		outputsRootToFfmpeg(
			ingestionJobKey, encodingJobKey, externalEncoder,
			"",		 // otherOutputOptionsBecauseOfMaxWidth,
			nullptr, // inputDrawTextDetailsRoot,
			-1,		 // streamingDurationInSeconds,
			outputsRoot, ffmpegArgumentList
		);
		*/
		optional<string> inputSelectedVideoMap;
		optional<string> inputSelectedAudioMap;
		optional<int32_t> inputDurationInSeconds;
		outputsRootToFfmpeg(
			ingestionJobKey, encodingJobKey, externalEncoder,
			nullptr, // inputDrawTextDetailsRoot,
			outputsRoot, ffMpegEngine, inputSelectedVideoMap,
			inputSelectedAudioMap, inputDurationInSeconds
		);

		// 2. add: -filter_complex "[0:v][1:v]blend=difference:shortest=1,blackframe=99:32[f]" -map "[f]" -f null -
		if (framesToBeDetectedRoot != nullptr && !framesToBeDetectedRoot.empty())
		{
			for (int pictureIndex = 0; pictureIndex < framesToBeDetectedRoot.size(); pictureIndex++)
			{
				const json& frameToBeDetectedRoot = framesToBeDetectedRoot[pictureIndex];

				if (JSONUtils::isPresent(frameToBeDetectedRoot, "picturePathName"))
				{
					bool videoFrameToBeCropped = JSONUtils::asBool(frameToBeDetectedRoot, "videoFrameToBeCropped", false);

					// ffmpegArgumentList.emplace_back("-filter_complex");

					int amount = JSONUtils::asInt32(frameToBeDetectedRoot, "amount", 99);
					int threshold = JSONUtils::asInt32(frameToBeDetectedRoot, "threshold", 32);

					string filter;

					if (videoFrameToBeCropped)
					{
						int width = JSONUtils::asInt32(frameToBeDetectedRoot, "width", -1);
						int height = JSONUtils::asInt32(frameToBeDetectedRoot, "height", -1);
						int videoCrop_X = JSONUtils::asInt32(frameToBeDetectedRoot, "videoCrop_X", -1);
						int videoCrop_Y = JSONUtils::asInt32(frameToBeDetectedRoot, "videoCrop_Y", -1);

						ffMpegEngine.addFilterComplex(std::format(
							"[0:v]crop=w={}:h={}:x={}:y={}[CROPPED]",
							width, height, videoCrop_X, videoCrop_Y));
						ffMpegEngine.addFilterComplex(std::format(
							"[CROPPED][{}:v]blend=difference:shortest=1,blackframe=amount={}:threshold={}[differenceOut_{}]",
							pictureIndex + 1, amount, threshold, pictureIndex + 1
							));
						// filter = "[0:v]crop=w=" + to_string(width) + ":h=" + to_string(height) + ":x=" + to_string(videoCrop_X) +
						// 		 ":y=" + to_string(videoCrop_Y) + "[CROPPED];" + "[CROPPED][" + to_string(pictureIndex + 1) + ":v]" +
						// 		 "blend=difference:shortest=1,blackframe=amount=" + to_string(amount) + ":threshold=" + to_string(threshold) +
						// 		 "[differenceOut_" + to_string(pictureIndex + 1) + "]";
					}
					else
					{
						ffMpegEngine.addFilterComplex(std::format(
						"[0:v][{}:v]blend=difference:shortest=1,blackframe=amount={}:threshold={}[differenceOut_{}]",
						pictureIndex + 1, amount, threshold, pictureIndex + 1));
						// filter = "[0:v][" + to_string(pictureIndex + 1) + ":v]" +
						// 		 "blend=difference:shortest=1,blackframe=amount=" + to_string(amount) + ":threshold=" + to_string(threshold) +
						// 		 "[differenceOut_" + to_string(pictureIndex + 1) + "]";
					}
					// ffmpegArgumentList.push_back(filter);

					mainOutput.map(std::format("[differenceOut_{}]", pictureIndex + 1));
					// ffmpegArgumentList.emplace_back("-map");
					// ffmpegArgumentList.push_back("[differenceOut_" + to_string(pictureIndex + 1) + "]");

					mainOutput.addArgs("-f null -");
					// ffmpegArgumentList.emplace_back("-f");
					// ffmpegArgumentList.emplace_back("null");
					//
					// ffmpegArgumentList.emplace_back("-");
				}
			}
		}

		bool sigQuitOrTermReceived = true;
		while (sigQuitOrTermReceived)
		{
			// inizialmente la variabile è -1, per cui il primo incremento la inizializza a 0
			(*numberOfRestartBecauseOfFailure)++;

			sigQuitOrTermReceived = false;

			// if (!ffmpegArgumentList.empty())
			// 	copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(), ostream_iterator<string>(ffmpegArgumentListStream, " "));

			SPDLOG_INFO(
				"liveRecorder: Executing ffmpeg command"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", _outputFfmpegPathFileName: {}"
				", ffmpegArgumentList: {}",
				ingestionJobKey, encodingJobKey, _outputFfmpegPathFileName, ffMpegEngine.toSingleLine(true)
			);

			startFfmpegCommand = chrono::system_clock::now();

			// prima di ogni chiamata (ffmpeg) viene resettato ffmpegCallbackData.
			// In questo modo l'ultima chiamata (ffmpeg) conserverà ffmpegCallbackData.
			// forkAndExecByCallback puo essere rieseguito a seguito di un restart
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
				redirectionStdOutput, redirectionStdError, processId, iReturnedStatus
			);
			*/
			processId.reset();

			endFfmpegCommand = chrono::system_clock::now();

			int64_t realDuration = chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count();

			if (iReturnedStatus != 0)
			{
				if (*ffmpegCallbackData->getSignal() == 3 // SIGQUIT
					|| *ffmpegCallbackData->getSignal() == 15 // SIGTERM
					)
				{
					sigQuitOrTermReceived = true;

					string errorMessage = std::format(
						"liveRecorder: ffmpeg execution command failed because received SIGQUIT/SIGTERM and is called again"
						", ingestionJobKey: {}"
						", encodingJobKey: {}"
						", iReturnedStatus: {}"
						", _outputFfmpegPathFileName: {}"
						", ffmpegArgumentList: {}"
						", signal: {}"
						", difference between real and expected duration: {}",
						ingestionJobKey, encodingJobKey, iReturnedStatus, _outputFfmpegPathFileName, ffMpegEngine.toSingleLine(),
						*ffmpegCallbackData->getSignal(), realDuration - streamingDuration
					);
					SPDLOG_ERROR(errorMessage);
					ffmpegCallbackData->pushErrorMessage(std::format("{} {}",
						Datetime::nowLocalTime("%Y-%m-%d %H:%M:%S", true), errorMessage));

					// in case of IP_PUSH the monitor thread, in case the client does not
					// reconnect istantaneously, kills the process.
					// In general, if ffmpeg restart, liveMonitoring has to wait, for this reason
					// we will set again the pRecordingStart variable
					{
						if (chrono::system_clock::from_time_t(utcRecordingPeriodStart) < chrono::system_clock::now())
							recordingStart = chrono::system_clock::now() + chrono::seconds(localPushListenTimeout);
						else
							recordingStart = chrono::system_clock::from_time_t(utcRecordingPeriodStart) + chrono::seconds(localPushListenTimeout);
					}

					{
						chrono::system_clock::time_point now = chrono::system_clock::now();
						utcNow = chrono::system_clock::to_time_t(now);

						if (utcNow < utcRecordingPeriodEnd)
						{
							time_t localStreamingDuration = utcRecordingPeriodEnd - utcNow;
							mainInput.setDurationSeconds(localStreamingDuration);
							// ffmpegArgumentList[streamingDurationIndex] = to_string(localStreamingDuration);

							SPDLOG_INFO(
								"liveRecorder: ffmpeg execution command failed because received SIGQUIT/SIGTERM, recalculate streaming duration"
								", ingestionJobKey: {}"
								", encodingJobKey: {}"
								", iReturnedStatus: {}"
								", _outputFfmpegPathFileName: {}"
								", localStreamingDuration: {}",
								ingestionJobKey, encodingJobKey, iReturnedStatus, _outputFfmpegPathFileName, localStreamingDuration
							);
						}
						else
						{
							// exit from loop even if SIGQUIT/SIGTERM because time period expired
							sigQuitOrTermReceived = false;

							SPDLOG_INFO(
								"liveRecorder: ffmpeg execution command should be called again because received SIGQUIT/SIGTERM but "
								"utcRecordingPeriod expired"
								", ingestionJobKey: {}"
								", encodingJobKey: {}"
								", iReturnedStatus: {}"
								", _outputFfmpegPathFileName: {}"
								", ffmpegArgumentList: {}",
								ingestionJobKey, encodingJobKey, iReturnedStatus, _outputFfmpegPathFileName, ffMpegEngine.toSingleLine()
							);
						}

						continue;
					}
				}

				string errorMessage = std::format(
					"liveRecorder: ffmpeg: ffmpeg execution command failed"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", iReturnedStatus: {}"
					", _outputFfmpegPathFileName: {}"
					", ffmpegArgumentList: {}"
					", difference between real and expected duration: {}",
					ingestionJobKey, encodingJobKey, iReturnedStatus, _outputFfmpegPathFileName, ffMpegEngine.toSingleLine(),
					realDuration - streamingDuration
				);
				SPDLOG_ERROR(errorMessage);

				// to hide the ffmpeg staff
				errorMessage = string("liveRecorder: command failed") + ", ingestionJobKey: " + to_string(ingestionJobKey) +
							   ", encodingJobKey: " + to_string(encodingJobKey);
				throw runtime_error(errorMessage);
			}
		}

		SPDLOG_INFO(
			"liveRecorder: Executed ffmpeg command"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", ffmpegArgumentList: {}"
			", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @{}@",
			ingestionJobKey, encodingJobKey, ffMpegEngine.toSingleLine(),
			chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()
		);

		try
		{
			outputsRootToFfmpeg_clean(ingestionJobKey, encodingJobKey, outputsRoot, externalEncoder);
		}
		catch (exception &e)
		{
			SPDLOG_ERROR(
				"outputsRootToFfmpeg_clean failed"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", e.what(): {}",
				ingestionJobKey, encodingJobKey, e.what()
			);

			// throw;
		}

		// if (segmenterType == "streamSegmenter" || segmenterType == "hlsSegmenter")
		{
			if (!segmentListPath.empty() && fs::exists(segmentListPath))
			{
				try
				{
					SPDLOG_INFO(
						"removeDirectory"
						", ingestionJobKey: {}"
						", encodingJobKey: {}"
						", segmentListPath: {}",
						ingestionJobKey, encodingJobKey, segmentListPath
					);
					fs::remove_all(segmentListPath);
				}
				catch (exception &e)
				{
					SPDLOG_ERROR(
						"remove directory failed"
						", ingestionJobKey: {}"
						", encodingJobKey: {}"
						", segmentListPath: {}"
						", e.what(): {}",
						ingestionJobKey, encodingJobKey, segmentListPath, e.what()
					);

					// throw;
				}
			}
		}
	}
	catch (exception &e)
	{
		processId.reset();

		// string lastPartOfFfmpegOutputFile = getLastPartOfFile(_outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
		string errorMessage;
		if (iReturnedStatus == 9) // 9 means: SIGKILL
			errorMessage = std::format(
				"ffmpeg: ffmpeg execution command failed because killed by the user"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", _outputFfmpegPathFileName: {}"
				", ffmpegArgumentList: {}"
				", e.what(): {}",
				ingestionJobKey, encodingJobKey, _outputFfmpegPathFileName, ffMpegEngine.toSingleLine(), e.what()
			);
		else
			errorMessage = std::format(
				"ffmpeg: ffmpeg execution command failed"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", _outputFfmpegPathFileName: {}"
				", ffmpegArgumentList: {}"
				", e.what(): {}",
				ingestionJobKey, encodingJobKey, _outputFfmpegPathFileName, ffMpegEngine.toSingleLine(), e.what()
			);
		SPDLOG_ERROR(errorMessage);
		ffmpegCallbackData->pushErrorMessage(std::format("{} {}",
			Datetime::nowLocalTime("%Y-%m-%d %H:%M:%S", true), errorMessage));

		renameOutputFfmpegPathFileName(ingestionJobKey, encodingJobKey, _outputFfmpegPathFileName);

		SPDLOG_INFO(
			"remove"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", segmentListPathName: {}",
			ingestionJobKey, encodingJobKey, segmentListPathName
		);
		fs::remove_all(segmentListPathName);

		try
		{
			outputsRootToFfmpeg_clean(ingestionJobKey, encodingJobKey, outputsRoot, externalEncoder);
		}
		catch (exception &e)
		{
			SPDLOG_ERROR(
				"outputsRootToFfmpeg_clean failed"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", e.what(): {}",
				ingestionJobKey, encodingJobKey, e.what()
			);

			// throw;
		}

		if (!segmentListPath.empty() && fs::exists(segmentListPath))
		{
			try
			{
				SPDLOG_INFO(
					"removeDirectory"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", segmentListPath: {}",
					ingestionJobKey, encodingJobKey, segmentListPath
				);
				fs::remove_all(segmentListPath);
			}
			catch (exception &ex)
			{
				SPDLOG_ERROR(
					"remove directory failed"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", segmentListPath: {}"
					", e.what(): {}",
					ingestionJobKey, encodingJobKey, segmentListPath, ex.what()
				);

				// throw;
			}
		}

		if (iReturnedStatus == 9) // 9 means: SIGKILL
			throw FFMpegEncodingKilledByUser();
		if (ffmpegCallbackData->getUrlForbidden())
			throw FFMpegURLForbidden();
		if (ffmpegCallbackData->getUrlNotFound())
			throw FFMpegURLNotFound();
		throw;
	}

	renameOutputFfmpegPathFileName(ingestionJobKey, encodingJobKey, _outputFfmpegPathFileName);
}
