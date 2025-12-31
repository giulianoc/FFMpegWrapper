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
#include "Datetime.h"
#include "JSONUtils.h"
#include "StringUtils.h"
#include "spdlog/spdlog.h"

FFMpegWrapper::FFMpegWrapper(nlohmann::json configuration) : _currentApiName()
{
	_ffmpegPath = JSONUtils::asString(configuration["ffmpeg"], "path", ".");
	SPDLOG_DEBUG(
		"Configuration item"
		", ffmpeg->path: {}",
		_ffmpegPath
	);
	_ffmpegTempDir = JSONUtils::asString(configuration["ffmpeg"], "tempDir", ".");
	_ffmpegEndlessRecursivePlaylistDir = JSONUtils::asString(configuration["ffmpeg"], "endlessRecursivePlaylistDir", ".");
	_ffmpegTtfFontDir = JSONUtils::asString(configuration["ffmpeg"], "ttfFontDir", ".");

	_youTubeDlPath = JSONUtils::asString(configuration["youTubeDl"], "path", ".");
	SPDLOG_DEBUG(
		"Configuration item"
		", youTubeDl->path: {}",
		_youTubeDlPath
	);
	_pythonPathName = JSONUtils::asString(configuration["youTubeDl"], "pythonPathName", ".");
	SPDLOG_DEBUG(
		"Configuration item"
		", youTubeDl->pythonPathName: {}",
		_pythonPathName
	);

	_waitingNFSSync_maxMillisecondsToWait = JSONUtils::asInt32(configuration["storage"], "waitingNFSSync_maxMillisecondsToWait", 150000);
	_waitingNFSSync_milliSecondsWaitingBetweenChecks =
		JSONUtils::asInt32(configuration["storage"], "waitingNFSSync_milliSecondsWaitingBetweenChecks", 100);
	/*
	info(__FILEREF__ + "Configuration item"
		+ ", storage->waitingNFSSync_sleepTimeInSeconds: "
		+ to_string(_waitingNFSSync_sleepTimeInSeconds)
	);
	*/

	// _startCheckingFrameInfoInMinutes = JSONUtils::asInt32(configuration["ffmpeg"], "startCheckingFrameInfoInMinutes", 5);

	_charsToBeReadFromFfmpegErrorOutput = 1024 * 3;

	_twoPasses = false;
	_currentlyAtSecondPass = false;

	_currentIngestionJobKey = -1;			  // just for log
	_currentEncodingJobKey = -1;			  // just for log
	_currentDurationInMilliSeconds = -1;	  // in case of some functionalities, it is important for getEncodingProgress
	_currentMMSSourceAssetPathName = "";	  // just for log
	_currentStagingEncodedAssetPathName = ""; // just for log

	_startFFMpegMethod = std::chrono::system_clock::now();

	_incrontabConfigurationDirectory = "/home/mms/mms/conf";
	_incrontabConfigurationFileName = "incrontab.txt";
	_incrontabBinary = "/usr/bin/incrontab";
}

FFMpegWrapper::~FFMpegWrapper() {}

bool FFMpegWrapper::ffmpegExecutableExist()
{
#ifdef _WIN32
	return fs::exists(std::format("{}/ffmpeg.exe", _ffmpegPath));
#else
	return fs::exists(std::format("{}/ffmpeg", _ffmpegPath));
#endif
}

void FFMpegWrapper::encodingVideoCodecValidation(std::string codec)
{
	if (codec != "libx264" && codec != "libvpx" && codec != "rawvideo" && codec != "mpeg4" && codec != "xvid")
	{
		std::string errorMessage = std::format(
			"ffmpeg: Video codec is wrong"
			", codec: {}",
			codec
		);
		SPDLOG_ERROR(errorMessage);

		throw std::runtime_error(errorMessage);
	}
}

void FFMpegWrapper::setStatus(
	int64_t ingestionJobKey, int64_t encodingJobKey, int64_t durationInMilliSeconds, std::string mmsSourceAssetPathName, std::string stagingEncodedAssetPathName
)
{

	_currentIngestionJobKey = ingestionJobKey;						   // just for log
	_currentEncodingJobKey = encodingJobKey;						   // just for log
	_currentDurationInMilliSeconds = durationInMilliSeconds;		   // in case of some functionalities, it is important for getEncodingProgress
	_currentMMSSourceAssetPathName = mmsSourceAssetPathName;		   // just for log
	_currentStagingEncodedAssetPathName = stagingEncodedAssetPathName; // just for log

	_startFFMpegMethod = std::chrono::system_clock::now();
}

/*
int FFMpeg::progressDownloadCallback(
	int64_t ingestionJobKey, std::chrono::system_clock::time_point &lastTimeProgressUpdate, double &lastPercentageUpdated, double dltotal, double dlnow,
	double ultotal, double ulnow
)
{

	int progressUpdatePeriodInSeconds = 15;

	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	if (dltotal != 0 && (dltotal == dlnow || now - lastTimeProgressUpdate >= std::chrono::seconds(progressUpdatePeriodInSeconds)))
	{
		double progress = (dlnow / dltotal) * 100;
		// int downloadingPercentage = floorf(progress * 100) / 100;
		// this is to have one decimal in the percentage
		double downloadingPercentage = ((double)((int)(progress * 10))) / 10;

		info(
			__FILEREF__ + "Download still running" + ", ingestionJobKey: " + to_string(ingestionJobKey) +
			", downloadingPercentage: " + to_string(downloadingPercentage) + ", dltotal: " + to_string(dltotal) + ", dlnow: " + to_string(dlnow) +
			", ultotal: " + to_string(ultotal) + ", ulnow: " + to_string(ulnow)
		);

		lastTimeProgressUpdate = now;

		if (lastPercentageUpdated != downloadingPercentage)
		{
			info(
				__FILEREF__ + "Update IngestionJob" + ", ingestionJobKey: " + to_string(ingestionJobKey) +
				", downloadingPercentage: " + to_string(downloadingPercentage)
			);
			// downloadingStoppedByUser = _mmsEngineDBFacade->updateIngestionJobSourceDownloadingInProgress (
			//     ingestionJobKey, downloadingPercentage);

			lastPercentageUpdated = downloadingPercentage;
		}

		// if (downloadingStoppedByUser)
		//     return 1;   // stop downloading
	}

	return 0;
}
*/

/*
int FFMpeg::progressDownloadCallback(double dltotal, double dlnow, double ultotal, double ulnow)
{

	int progressUpdatePeriodInSeconds = 15;

	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	if (dltotal != 0 && (dltotal == dlnow || now - _lastTimeProgressUpdate >= std::chrono::seconds(progressUpdatePeriodInSeconds)))
	{
		double progress = (dlnow / dltotal) * 100;
		// int downloadingPercentage = floorf(progress * 100) / 100;
		// this is to have one decimal in the percentage
		double downloadingPercentage = ((double)((int)(progress * 10))) / 10;

		SPDLOG_INFO(
			"Download still running"
			", ingestionJobKey: {}"
			", downloadingPercentage: {}"
			", dltotal: {}"
			", dlnow: {}"
			", ultotal: {}"
			", ulnow: {}",
			_ingestionJobKey, downloadingPercentage, dltotal, dlnow, ultotal, ulnow
		);

		_lastTimeProgressUpdate = now;

		if (_lastPercentageUpdated != downloadingPercentage)
		{
			SPDLOG_INFO(
				"Update IngestionJob"
				", ingestionJobKey: {}"
				", downloadingPercentage: {}",
				_ingestionJobKey, downloadingPercentage
			);
			// downloadingStoppedByUser = _mmsEngineDBFacade->updateIngestionJobSourceDownloadingInProgress (
			//     ingestionJobKey, downloadingPercentage);

			_lastPercentageUpdated = downloadingPercentage;
		}

		// if (downloadingStoppedByUser)
		//     return 1;   // stop downloading
	}

	return 0;
}
*/

void FFMpegWrapper::renameOutputFfmpegPathFileName(int64_t ingestionJobKey, int64_t encodingJobKey, std::string outputFfmpegPathFileName)
{
	std::string debugOutputFfmpegPathFileName;
	try
	{
		tm tmUtcTimestamp = Datetime::utcSecondsToLocalTime(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

		debugOutputFfmpegPathFileName = std::format(
			"{}.{:0>4}-{:0>2}-{:0>2}-{:0>2}-{:0>2}-{:0>2}", outputFfmpegPathFileName, tmUtcTimestamp.tm_year + 1900, tmUtcTimestamp.tm_mon + 1,
			tmUtcTimestamp.tm_mday, tmUtcTimestamp.tm_hour, tmUtcTimestamp.tm_min, tmUtcTimestamp.tm_sec
		);

		SPDLOG_INFO(
			"move"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", outputFfmpegPathFileName: {}"
			", debugOutputFfmpegPathFileName: {}",
			ingestionJobKey, encodingJobKey, outputFfmpegPathFileName, debugOutputFfmpegPathFileName
		);
		// fs::rename works only if source and destination are on the same file systems
		if (fs::exists(outputFfmpegPathFileName))
			fs::rename(outputFfmpegPathFileName, debugOutputFfmpegPathFileName);
	}
	catch (std::exception &e)
	{
		SPDLOG_ERROR(
			"move failed"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", outputFfmpegPathFileName: {}"
			", debugOutputFfmpegPathFileName: {}",
			ingestionJobKey, encodingJobKey, outputFfmpegPathFileName, debugOutputFfmpegPathFileName
		);
	}
}

bool FFMpegWrapper::isNumber(int64_t ingestionJobKey, std::string number)
{
	try
	{
		for (int i = 0; i < number.length(); i++)
		{
			if (!isdigit(number[i]) && number[i] != '.' && number[i] != '-')
				return false;
		}

		return true;
	}
	catch (std::exception &e)
	{
		std::string errorMessage = std::format(
			"isNumber failed"
			", ingestionJobKey: {}"
			", number: {}"
			", exception: {}",
			ingestionJobKey, number, e.what()
		);
		SPDLOG_ERROR(errorMessage);

		throw std::runtime_error(errorMessage);
	}
}

// ritorna: secondi (double), centesimi (long). Es: 5.27 e 527. I centesimi sono piu precisi perchè evitano
//	i troncamenti di un double
std::pair<double, long> FFMpegWrapper::timeToSeconds(int64_t ingestionJobKey, std::string time)
{
	try
	{
		std::string localTime = StringUtils::trimTabToo(time);
		if (localTime == "")
			return std::make_pair(0.0, 0);

		if (isNumber(ingestionJobKey, localTime))
			return std::make_pair(stod(localTime), stod(localTime) * 100);

		// format: [-][HH:]MM:SS[.m...]

		bool isNegative = false;
		if (localTime[0] == '-')
			isNegative = true;

		// int semiColonNumber = count_if(localTime.begin(), localTime.end(), []( char c ){return c == ':';});
		bool hourPresent = false;
		int hours = 0;
		int minutes = 0;
		int seconds = 0;
		int decimals = 0; // centesimi di secondo

		bool hoursPresent = std::count_if(localTime.begin(), localTime.end(), [](char c) { return c == ':'; }) == 2;
		bool decimalPresent = localTime.find(".") != std::string::npos;

		std::stringstream ss(isNegative ? localTime.substr(1) : localTime);

		char delim = ':';

		if (hoursPresent)
		{
			std::string sHours;
			getline(ss, sHours, delim);
			hours = stoi(sHours);
		}

		std::string sMinutes;
		getline(ss, sMinutes, delim);
		minutes = stoi(sMinutes);

		delim = '.';
		std::string sSeconds;
		getline(ss, sSeconds, delim);
		seconds = stoi(sSeconds);

		if (decimalPresent)
		{
			std::string sDecimals;
			getline(ss, sDecimals, delim);
			decimals = stoi(sDecimals);
		}

		double dSeconds;
		long centsOfSeconds;
		if (decimals != 0)
		{
			sSeconds = std::format("{}.{}", (hours * 3600) + (minutes * 60) + seconds, decimals);
			dSeconds = stod(sSeconds);

			centsOfSeconds = ((hours * 3600) + (minutes * 60) + seconds) * 100 + decimals;
		}
		else
		{
			dSeconds = (hours * 3600) + (minutes * 60) + seconds;
			centsOfSeconds = ((hours * 3600) + (minutes * 60) + seconds) * 100;
		}

		if (isNegative)
		{
			dSeconds *= -1;
			centsOfSeconds *= -1;
		}

		return std::make_pair(dSeconds, centsOfSeconds);
	}
	catch (std::exception &e)
	{
		std::string errorMessage = std::format(
			"timeToSeconds failed"
			", ingestionJobKey: {}"
			", time: {}"
			", exception: {}",
			ingestionJobKey, time, e.what()
		);
		SPDLOG_ERROR(errorMessage);

		throw std::runtime_error(errorMessage);
	}
}

std::string FFMpegWrapper::secondsToTime(int64_t ingestionJobKey, double dSeconds)
{
	try
	{
		double dLocalSeconds = dSeconds;

		int hours = dLocalSeconds / 3600;
		dLocalSeconds -= (hours * 3600);

		int minutes = dLocalSeconds / 60;
		dLocalSeconds -= (minutes * 60);

		int seconds = dLocalSeconds;
		dLocalSeconds -= seconds;

		std::string time;
		{
			/*
			char buffer[64];
			sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
			time = buffer;
			*/
			time = std::format("{:0>2}:{:0>2}:{:0>2}", hours, minutes, seconds);
			// dLocalSeconds dovrebbe essere 0.12345
			if (dLocalSeconds > 0.0)
			{
				// poichè siamo interessati ai decimi di secondo
				int decimals = dLocalSeconds * 100;
				time += std::format(".{}", decimals);
				/*
				string decimals = to_string(dLocalSeconds);
				size_t decimalPoint = decimals.find(".");
				if (decimalPoint != string::npos)
					time += decimals.substr(decimalPoint);
				*/
			}
		}

		return time;
	}
	catch (std::exception &e)
	{
		std::string errorMessage = std::format(
			"secondsToTime failed"
			", ingestionJobKey: {}"
			", dSeconds: {}"
			", exception: {}",
			ingestionJobKey, dSeconds, e.what()
		);
		SPDLOG_ERROR(errorMessage);

		throw std::runtime_error(errorMessage);
	}
}

std::string FFMpegWrapper::centsOfSecondsToTime(int64_t ingestionJobKey, long centsOfSeconds)
{
	try
	{
		long localCentsOfSeconds = centsOfSeconds;

		int hours = localCentsOfSeconds / 360000;
		localCentsOfSeconds -= (hours * 360000);

		int minutes = localCentsOfSeconds / 6000;
		localCentsOfSeconds -= (minutes * 6000);

		int seconds = localCentsOfSeconds / 100;
		localCentsOfSeconds -= (seconds * 100);

		std::string time = std::format("{:0>2}:{:0>2}:{:0>2}.{:0>2}", hours, minutes, seconds, localCentsOfSeconds);
		{
			/*
			char buffer[64];
			sprintf(buffer, "%02d:%02d:%02d.%02ld", hours, minutes, seconds, localCentsOfSeconds);
			time = buffer;
			*/
		}

		return time;
	}
	catch (std::exception &e)
	{
		std::string errorMessage = std::format(
			"centsOfSecondsToTime failed"
			", ingestionJobKey: {}"
			", centsOfSeconds: {}"
			", exception: {}",
			ingestionJobKey, centsOfSeconds, e.what()
		);
		SPDLOG_ERROR(errorMessage);

		throw std::runtime_error(errorMessage);
	}
}
