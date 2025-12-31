/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/*
 * File:   FFMPEGEncoder.h
 * Author: giuliano
 *
 * Created on February 18, 2018, 1:27 AM
 */

#pragma once

#include "FFMpegEngine.h"
#include "ProcessUtility.h"
#include <chrono>
#include <cstdint>
#include <string>
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#ifndef __FILEREF__
#ifdef __APPLE__
#define __FILEREF__ string("[") + string(__FILE__).substr(string(__FILE__).find_last_of("/") + 1) + ":" + to_string(__LINE__) + "] "
#else
#define __FILEREF__ string("[") + basename((char *)__FILE__) + ":" + to_string(__LINE__) + "] "
#endif
#endif

namespace fs = std::filesystem;

/*
using json = nlohmann::json;
using orderd_json = nlohmann::ordered_json;
using namespace nlohmann::literals;
*/

struct FFMpegEncodingStatusNotAvailable final : public std::exception
{
	[[nodiscard]] char const *what() const noexcept override { return "Encoding status not available"; };
};

struct FFMpegSizeOrFrameInfoNotAvailable final : public std::exception
{
	[[nodiscard]] char const *what() const noexcept override { return "SizeOrFrame Info not available"; };
};

struct FFMpegEncodingKilledByUser final : public std::exception
{
	[[nodiscard]] char const *what() const noexcept override { return "Encoding was killed by the User"; };
};

struct FFMpegURLForbidden final : public std::exception
{
	[[nodiscard]] char const *what() const throw() override { return "URL Forbidden"; };
};

struct FFMpegURLNotFound final : public std::exception
{
	[[nodiscard]] char const *what() const noexcept override { return "URL Not Found"; };
};

struct NoEncodingJobKeyFound final : public std::exception
{
	[[nodiscard]] char const *what() const noexcept override { return "No encoding job key found"; };
};

struct NoEncodingAvailable final : public std::exception
{
	[[nodiscard]] char const *what() const noexcept override { return "No encoding available"; };
};

struct MaxConcurrentJobsReached final : public std::exception
{
	[[nodiscard]] char const *what() const noexcept override { return "Encoder reached the max number of concurrent jobs"; };
};

struct EncodingIsAlreadyRunning final : public std::exception
{
	[[nodiscard]] char const *what() const noexcept override { return "Encoding is already running"; };
};

class FFMpegWrapper
{
  public:
	enum class KillType
	{
		None,
		Kill,
		RestartWithinEncoder,
		KillToRestartByEngine
	};
	static constexpr std::string_view toString(const KillType &killType)
	{
		switch (killType)
		{
		case KillType::None:
			return "none";
		case KillType::Kill:
			return "kill";
		case KillType::RestartWithinEncoder:
			return "restartWithinEncoder";
		case KillType::KillToRestartByEngine:
			return "killToRestartByEngine";
		default:
			const std::string errorMessage = std::format("toString, wrong KillType: {}", static_cast<int>(killType));
			SPDLOG_ERROR(errorMessage);
			throw std::runtime_error(errorMessage);
		}
	}
	static KillType toKillType(std::string_view killType)
	{
		if (killType == "none")
			return KillType::None;
		if (killType == "kill")
			return KillType::Kill;
		if (killType == "restartWithinEncoder")
			return KillType::RestartWithinEncoder;
		if (killType == "killToRestartByEngine")
			return KillType::KillToRestartByEngine;

		const std::string errorMessage = std::format("toKillType, wrong KillType: {}", killType);
		SPDLOG_ERROR(errorMessage);
		throw std::runtime_error(errorMessage);
	}

	std::string _ffmpegTempDir;

	explicit FFMpegWrapper(nlohmann::json configuration);

	~FFMpegWrapper();

	void encodeContent(
		std::string mmsSourceAssetPathName, int64_t durationInMilliSeconds, std::string encodedStagingAssetPathName, nlohmann::json encodingProfileDetailsRoot,
		bool isVideo,
		// if false it means is audio
		nlohmann::json videoTracksRoot, nlohmann::json audioTracksRoot, int videoTrackIndexToBeUsed, int audioTrackIndexToBeUsed, nlohmann::json filtersRoot,
		int64_t physicalPathKey, int64_t encodingJobKey, int64_t ingestionJobKey, ProcessUtility::ProcessId &processId,
		std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void overlayImageOnVideo(
		bool externalEncoder,  std::string mmsSourceVideoAssetPathName, int64_t videoDurationInMilliSeconds,  std::string mmsSourceImageAssetPathName,
		 std::string imagePosition_X_InPixel,  std::string imagePosition_Y_InPixel,
		//  std::string encodedFileName,
		 std::string stagingEncodedAssetPathName, nlohmann::json encodingProfileDetailsRoot, int64_t encodingJobKey, int64_t ingestionJobKey,
		ProcessUtility::ProcessId &processId, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void overlayTextOnVideo(
		 std::string mmsSourceVideoAssetPathName, int64_t videoDurationInMilliSeconds,

		nlohmann::json drawTextDetailsRoot, nlohmann::json encodingProfileDetailsRoot,  std::string stagingEncodedAssetPathName, int64_t encodingJobKey,
		int64_t ingestionJobKey, ProcessUtility::ProcessId &processId, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void videoSpeed(
		 std::string mmsSourceVideoAssetPathName, int64_t videoDurationInMilliSeconds,

		 std::string videoSpeedType, int videoSpeedSize,

		nlohmann::json encodingProfileDetailsRoot,

		 std::string stagingEncodedAssetPathName, int64_t encodingJobKey, int64_t ingestionJobKey, ProcessUtility::ProcessId &processId,
		std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void pictureInPicture(
		const  std::string &mmsMainVideoAssetPathName, int64_t mainVideoDurationInMilliSeconds, const  std::string &mmsOverlayVideoAssetPathName,
		int64_t overlayVideoDurationInMilliSeconds, bool soundOfMain, const  std::string &overlayPosition_X_InPixel,
		const  std::string &overlayPosition_Y_InPixel, const  std::string &overlay_Width_InPixel, const std::string &overlay_Height_InPixel,
		const nlohmann::json &encodingProfileDetailsRoot, std::string stagingEncodedAssetPathName, int64_t encodingJobKey, int64_t ingestionJobKey,
		ProcessUtility::ProcessId &processId, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void introOutroOverlay(
		std::string introVideoAssetPathName, int64_t introVideoDurationInMilliSeconds, std::string mainVideoAssetPathName,
		int64_t mainVideoDurationInMilliSeconds, std::string outroVideoAssetPathName, int64_t outroVideoDurationInMilliSeconds,

		int64_t introOverlayDurationInSeconds, int64_t outroOverlayDurationInSeconds,

		bool muteIntroOverlay, bool muteOutroOverlay,

		nlohmann::json encodingProfileDetailsRoot,

		std::string stagingEncodedAssetPathName, int64_t encodingJobKey, int64_t ingestionJobKey, ProcessUtility::ProcessId &processId,
		std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void introOverlay(
		std::string introVideoAssetPathName, int64_t introVideoDurationInMilliSeconds, std::string mainVideoAssetPathName,
		int64_t mainVideoDurationInMilliSeconds,

		int64_t introOverlayDurationInSeconds,

		bool muteIntroOverlay,

		nlohmann::json encodingProfileDetailsRoot,

		std::string stagingEncodedAssetPathName, int64_t encodingJobKey, int64_t ingestionJobKey, ProcessUtility::ProcessId &processId,
		std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void outroOverlay(
		std::string mainVideoAssetPathName, int64_t mainVideoDurationInMilliSeconds, std::string outroVideoAssetPathName,
		int64_t outroVideoDurationInMilliSeconds,

		int64_t outroOverlayDurationInSeconds,

		bool muteOutroOverlay,

		nlohmann::json encodingProfileDetailsRoot,

		std::string stagingEncodedAssetPathName, int64_t encodingJobKey, int64_t ingestionJobKey, ProcessUtility::ProcessId &processId,
		std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void silentAudio(
		std::string videoAssetPathName, int64_t videoDurationInMilliSeconds, std::string addType,
		// entireTrack, begin, end
		int seconds, nlohmann::json encodingProfileDetailsRoot, std::string stagingEncodedAssetPathName, int64_t encodingJobKey, int64_t ingestionJobKey,
		ProcessUtility::ProcessId &processId, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	time_t getOutputFFMpegFileLastModificationTime();
	uintmax_t getOutputFFMpegFileSize();
	std::string getOutputFfmpegPathFileName() const;

	std::tuple<int64_t, long, nlohmann::json> getMediaInfo(
		int64_t ingestionJobKey, bool isMMSAssetPathName, int timeoutInSeconds, std::string mediaSource,
		std::vector<std::tuple<int, int64_t, std::string, std::string, int, int, std::string, long>> &videoTracks,
		std::vector<std::tuple<int, int64_t, std::string, long, int, long, std::string>> &audioTracks
	);

	std::string getNearestKeyFrameTime(
		int64_t ingestionJobKey, std::string mediaSource,
		std::string readIntervals, // intervallo dove cercare il key frame piu vicino
		double keyFrameTime
	);

	int probeChannel(int64_t ingestionJobKey, std::string url);

	void muxAllFiles(int64_t ingestionJobKey, std::vector<std::string> sourcesPathName, std::string destinationPathName);

	void getLiveStreamingInfo(
		std::string liveURL, std::string userAgent, int64_t ingestionJobKey, int64_t encodingJobKey,
		std::vector<std::tuple<int, std::string, std::string, std::string, std::string, int, int>> &videoTracks, std::vector<std::tuple<int, std::string, std::string, std::string, int, bool>> &audioTracks
	);

	void generateFrameToIngest(
		int64_t ingestionJobKey, std::string mmsAssetPathName, int64_t videoDurationInMilliSeconds, double startTimeInSeconds, std::string frameAssetPathName,
		int imageWidth, int imageHeight, ProcessUtility::ProcessId &processId, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void generateFramesToIngest(
		int64_t ingestionJobKey, int64_t encodingJobKey, std::string imagesDirectory, std::string imageBaseFileName, double startTimeInSeconds,
		int framesNumber, std::string videoFilter, int periodInSeconds, bool mjpeg, int imageWidth, int imageHeight, std::string mmsAssetPathName,
		int64_t videoDurationInMilliSeconds, ProcessUtility::ProcessId &processId, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void concat(int64_t ingestionJobKey, bool isVideo, std::vector<std::string> &sourcePhysicalPaths, std::string concatenatedMediaPathName);

	void splitVideoInChunks(
		int64_t ingestionJobKey, std::string sourcePhysicalPath, long chunksDurationInSeconds, std::string chunksDirectory, std::string chunkBaseFileName
	);

	void slideShow(
		int64_t ingestionJobKey, int64_t encodingJobKey, float durationOfEachSlideInSeconds, std::string frameRateMode, nlohmann::json encodingProfileDetailsRoot,
		std::vector<std::string> &imagesSourcePhysicalPaths, std::vector<std::string> &audiosSourcePhysicalPaths, float shortestAudioDurationInSeconds,
		// the shortest duration among the audios
		std::string encodedStagingAssetPathName, ProcessUtility::ProcessId &processId, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void cutWithoutEncoding(
		int64_t ingestionJobKey, std::string sourcePhysicalPath, bool isVideo,
		std::string cutType, // KeyFrameSeeking (input seeking), FrameAccurateWithoutEncoding, KeyFrameSeekingInterval
		std::string startKeyFramesSeekingInterval, std::string endKeyFramesSeekingInterval, std::string startTime, std::string endTime, int framesNumber,
		std::string cutMediaPathName
	);

	void cutFrameAccurateWithEncoding(
		int64_t ingestionJobKey, std::string sourceVideoAssetPathName,
		// no keyFrameSeeking needs reencoding otherwise the key frame is always used
		// If you re-encode your video when you cut/trim, then you get a frame-accurate cut
		// because FFmpeg will re-encode the video and start with an I-frame.
		int64_t encodingJobKey, const nlohmann::json &encodingProfileDetailsRoot, std::string startTime, std::string endTime, int framesNumber,
		std::string stagingEncodedAssetPathName, ProcessUtility::ProcessId &processId, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void extractTrackMediaToIngest(
		int64_t ingestionJobKey, std::string sourcePhysicalPath, std::vector<std::pair<std::string, int>> &tracksToBeExtracted, std::string extractTrackMediaPathName
	);

	void liveRecorder(
		int64_t ingestionJobKey, int64_t encodingJobKey, bool externalEncoder, const std::string &segmentListPathName,
		const std::string &recordedFileNamePrefix, const std::string &otherInputOptions, const std::string &streamSourceType,
		// IP_PULL, TV, IP_PUSH, CaptureLive
		std::string liveURL, int pushListenTimeout, int captureLive_videoDeviceNumber, const std::string &captureLive_videoInputFormat,
		int captureLive_frameRate, int captureLive_width, int captureLive_height, int captureLive_audioDeviceNumber, int captureLive_channelsNumber,
		bool utcTimeOverlay, const std::string_view &userAgent, time_t utcRecordingPeriodStart, time_t utcRecordingPeriodEnd, int segmentDurationInSeconds,
		const std::string &outputFileFormat, const std::string& otherOutputOptions, const std::string &segmenterType,
		// streamSegmenter or hlsSegmenter
		const nlohmann::json &outputsRoot, nlohmann::json framesToBeDetectedRoot, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData,
		ProcessUtility::ProcessId &processId, std::optional<std::chrono::system_clock::time_point>& recordingStart,
		long *numberOfRestartBecauseOfFailure
	);

	void liveProxy(
		int64_t ingestionJobKey, int64_t encodingJobKey, bool externalEncoder, long maxStreamingDurationInMinutes, std::mutex *inputsRootMutex,
		nlohmann::json *inputsRoot, const nlohmann::json &outputsRoot, std::optional<std::chrono::system_clock::time_point> &proxyStart,
		const std::shared_ptr<FFMpegEngine::CallbackData> &ffmpegCallbackData, long& numberOfRestartBecauseOfFailure,
		const KillType& killTypeReceived, ProcessUtility::ProcessId &processId, bool keepOutputLog = true
	);

	void liveGrid(
		int64_t ingestionJobKey, int64_t encodingJobKey, bool externalEncoder, std::string userAgent, nlohmann::json inputChannelsRoot,
		// name,url
		int gridColumns, int gridWidth,
		// i.e.: 1024
		int gridHeight,
		// i.e.: 578

		nlohmann::json outputsRoot,

		ProcessUtility::ProcessId &processId, std::shared_ptr<FFMpegEngine::CallbackData> ffmpegCallbackData
	);

	void changeFileFormat(
		int64_t ingestionJobKey, int64_t physicalPathKey, std::string sourcePhysicalPath,
		std::vector<std::tuple<int64_t, int, int64_t, int, int, std::string, std::string, long, std::string>> &sourceVideoTracks,
		std::vector<std::tuple<int64_t, int, int64_t, long, std::string, long, int, std::string>> &sourceAudioTracks, std::string destinationPathName, std::string outputFileFormat
	);

	void streamingToFile(int64_t ingestionJobKey, bool regenerateTimestamps, std::string sourceReferenceURL, std::string destinationPathName);

	static void encodingVideoCodecValidation(std::string codec);

	std::pair<std::string, std::string> retrieveStreamingYouTubeURL(int64_t ingestionJobKey, std::string youTubeURL);
	void retrieveLocalInputDevices(std::vector<std::pair<std::string, std::string>> &videoLocalInputDevices, std::vector<std::pair<std::string, std::string>> &audioLocalInputDevices);
	bool ffmpegExecutableExist();

	static bool isNumber(int64_t ingestionJobKey, std::string number);
	static std::pair<double, long> timeToSeconds(int64_t ingestionJobKey, std::string time);
	static std::string secondsToTime(int64_t ingestionJobKey, double dSeconds);
	static std::string centsOfSecondsToTime(int64_t ingestionJobKey, long centsOfSeconds);

  private:
	enum class APIName
	{
		EncodeContent = 0,
		OverlayImageOnVideo = 1,
		OverlayTextOnVideo = 2,
		VideoSpeed = 3,
		PictureInPicture = 4,
		IntroOutroOverlay = 5,
		GetMediaInfo = 6,
		ProbeChannel = 7,
		MuxAllFiles = 8,
		GenerateFrameToIngest = 9,
		GenerateFramesToIngest = 10,
		Concat = 11,
		CutWithoutEncoding = 12,
		CutFrameAccurateWithEncoding = 13,
		SlideShow = 14,
		ExtractTrackMediaToIngest = 15,
		LiveRecorder = 16,
		LiveProxy = 17,
		LiveGrid = 18,
		ChangeFileFormat = 19,
		StreamingToFile = 20,
		SilentAudio = 21,
		IntroOverlay = 22,
		OutroOverlay = 23,
		NearestKeyFrameTime = 24,
		SplitVideoInChunks = 25
	};
	static const char *toString(const APIName &apiName)
	{
		switch (apiName)
		{
		case APIName::EncodeContent:
			return "EncodeContent";
		case APIName::OverlayImageOnVideo:
			return "OverlayImageOnVideo";
		case APIName::OverlayTextOnVideo:
			return "OverlayTextOnVideo";
		case APIName::VideoSpeed:
			return "VideoSpeed";
		case APIName::PictureInPicture:
			return "PictureInPicture";
		case APIName::IntroOutroOverlay:
			return "IntroOutroOverlay";
		case APIName::GetMediaInfo:
			return "GetMediaInfo";
		case APIName::ProbeChannel:
			return "ProbeChannel";
		case APIName::MuxAllFiles:
			return "MuxAllFiles";
		case APIName::GenerateFrameToIngest:
			return "GenerateFrameToIngest";
		case APIName::GenerateFramesToIngest:
			return "GenerateFramesToIngest";
		case APIName::Concat:
			return "Concat";
		case APIName::CutWithoutEncoding:
			return "CutWithoutEncoding";
		case APIName::CutFrameAccurateWithEncoding:
			return "CutFrameAccurateWithEncoding";
		case APIName::SlideShow:
			return "SlideShow";
		case APIName::ExtractTrackMediaToIngest:
			return "ExtractTrackMediaToIngest";
		case APIName::LiveRecorder:
			return "LiveRecorder";
		case APIName::LiveProxy:
			return "LiveProxy";
		case APIName::LiveGrid:
			return "LiveGrid";
		case APIName::ChangeFileFormat:
			return "ChangeFileFormat";
		case APIName::StreamingToFile:
			return "StreamingToFile";
		case APIName::SilentAudio:
			return "SilentAudio";
		case APIName::IntroOverlay:
			return "IntroOverlay";
		case APIName::OutroOverlay:
			return "OutroOverlay";
		case APIName::NearestKeyFrameTime:
			return "NearestKeyFrameTime";
		case APIName::SplitVideoInChunks:
			return "SplitVideoInChunks";
		default:
			throw std::runtime_error(std::string("Wrong APIName"));
		}
	}
	static APIName toAPIName(const std::string &apiName)
	{
		std::string lowerCase;
		lowerCase.resize(apiName.size());
		transform(apiName.begin(), apiName.end(), lowerCase.begin(), [](unsigned char c) { return tolower(c); });

		if (lowerCase == "encodecontent")
			return APIName::EncodeContent;
		else if (lowerCase == "overlayimageonvideo")
			return APIName::OverlayImageOnVideo;
		else if (lowerCase == "overlaytextonvideo")
			return APIName::OverlayTextOnVideo;
		else if (lowerCase == "videospeed")
			return APIName::VideoSpeed;
		else if (lowerCase == "pictureinpicture")
			return APIName::PictureInPicture;
		else if (lowerCase == "introoutrooverlay")
			return APIName::IntroOutroOverlay;
		else if (lowerCase == "getmediainfo")
			return APIName::GetMediaInfo;
		else if (lowerCase == "probechannel")
			return APIName::ProbeChannel;
		else if (lowerCase == "muxallfiles")
			return APIName::MuxAllFiles;
		else if (lowerCase == "generateframetoingest")
			return APIName::GenerateFrameToIngest;
		else if (lowerCase == "generateframestoingest")
			return APIName::GenerateFramesToIngest;
		else if (lowerCase == "concat")
			return APIName::Concat;
		else if (lowerCase == "cutwithoutencoding")
			return APIName::CutWithoutEncoding;
		else if (lowerCase == "cutframeaccuratewithencoding")
			return APIName::CutFrameAccurateWithEncoding;
		else if (lowerCase == "slideshow")
			return APIName::SlideShow;
		else if (lowerCase == "extracttrackmediatoingest")
			return APIName::ExtractTrackMediaToIngest;
		else if (lowerCase == "liverecorder")
			return APIName::LiveRecorder;
		else if (lowerCase == "liveproxy")
			return APIName::LiveProxy;
		else if (lowerCase == "livegrid")
			return APIName::LiveGrid;
		else if (lowerCase == "changefileformat")
			return APIName::ChangeFileFormat;
		else if (lowerCase == "streamingtofile")
			return APIName::StreamingToFile;
		else if (lowerCase == "silentaudio")
			return APIName::SilentAudio;
		else if (lowerCase == "introoverlay")
			return APIName::IntroOverlay;
		else if (lowerCase == "outrooverlay")
			return APIName::OutroOverlay;
		else if (lowerCase == "nearestkeyframetime")
			return APIName::NearestKeyFrameTime;
		else if (lowerCase == "splitvideoinchunks")
			return APIName::SplitVideoInChunks;
		else
			throw std::runtime_error(std::string("Wrong APIName") + ", current apiName: " + apiName);
	}

	std::string _ffmpegPath;
	std::string _ffmpegEndlessRecursivePlaylistDir;
	std::string _ffmpegTtfFontDir;
	int _charsToBeReadFromFfmpegErrorOutput;
	bool _twoPasses;
	std::string _outputFfmpegPathFileName;
	bool _currentlyAtSecondPass;

	std::string _youTubeDlPath;
	std::string _pythonPathName;

	APIName _currentApiName;

	int64_t _currentDurationInMilliSeconds;
	std::string _currentMMSSourceAssetPathName;
	std::string _currentStagingEncodedAssetPathName;
	int64_t _currentIngestionJobKey;
	int64_t _currentEncodingJobKey;

	std::chrono::system_clock::time_point _startFFMpegMethod;
	// int _startCheckingFrameInfoInMinutes;

	int _waitingNFSSync_maxMillisecondsToWait;
	int _waitingNFSSync_milliSecondsWaitingBetweenChecks;

	std::string _incrontabConfigurationDirectory;
	std::string _incrontabConfigurationFileName;
	std::string _incrontabBinary;

	void setStatus(
		int64_t ingestionJobKey, int64_t encodingJobKey = -1, int64_t durationInMilliSeconds = -1, std::string mmsSourceAssetPathName = "",
		std::string stagingEncodedAssetPathName = ""
	);

	static int getNextLiveProxyInput(
		int64_t ingestionJobKey, int64_t encodingJobKey, nlohmann::json *inputsRoot, std::mutex *inputsRootMutex, int currentInputIndex, bool timedInput,
		nlohmann::json *newInputRoot
	);

	std::tuple<std::string, int, int64_t, nlohmann::json, std::optional<std::string>, std::optional<std::string>, std::optional<int32_t>> liveProxyInput(
		int64_t ingestionJobKey, int64_t encodingJobKey, bool externalEncoder, nlohmann::json inputRoot, long maxStreamingDurationInMinutes,
		FFMpegEngine &ffMpegEngine, const KillType& killTypeReceived
	);

	void outputsRootToFfmpeg(
		int64_t ingestionJobKey, int64_t encodingJobKey, bool externalEncoder, std::string otherOutputOptionsBecauseOfMaxWidth,
		nlohmann::json inputDrawTextDetailsRoot,
		long streamingDurationInSeconds, nlohmann::json outputsRoot, std::vector<std::string> &ffmpegOutputArgumentList
	);
	void outputsRootToFfmpeg(
		int64_t ingestionJobKey, int64_t encodingJobKey, bool externalEncoder,
		const nlohmann::json& inputDrawTextDetailsRoot,
		nlohmann::json outputsRoot, FFMpegEngine& ffMpegEngine,
		std::optional<std::string> &inputSelectedVideoMap, std::optional<std::string> &inputSelectedAudioMap,
		std::optional<int32_t> &inputDurationInSeconds
	);
	void outputsRootToFfmpeg_clean(int64_t ingestionJobKey, int64_t encodingJobKey, nlohmann::json outputsRoot, bool externalEncoder);

	// std::string getLastPartOfFile(std::string pathFileName, int lastCharsToBeRead);

	// long getFrameByOutputLog(std::string ffmpegEncodingStatus);
	// long getSizeByOutputLog(std::string ffmpegEncodingStatus);

	void addToIncrontab(int64_t ingestionJobKey, int64_t encodingJobKey, std::string directoryToBeMonitored);

	void removeFromIncrontab(int64_t ingestionJobKey, int64_t encodingJobKey, std::string directoryToBeMonitored);

	// int progressDownloadCallback(
	// 	int64_t ingestionJobKey, std::chrono::system_clock::time_point &lastTimeProgressUpdate, double &lastPercentageUpdated, double dltotal,
	// 	double dlnow, double ultotal, double ulnow
	// );

	void renameOutputFfmpegPathFileName(int64_t ingestionJobKey, int64_t encodingJobKey, std::string outputFfmpegPathFileName);
};
