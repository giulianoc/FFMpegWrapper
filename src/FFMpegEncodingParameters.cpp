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
#include "FFMpegEncodingParameters.h"

#include "FFMpegEngine.h"
#include "FFMpegFilters.h"
#include "JSONUtils.h"
#include "spdlog/spdlog.h"
#include <filesystem>
#include <fstream>
#include <regex>

using namespace std;
namespace fs = std::filesystem;

FFMpegEncodingParameters::FFMpegEncodingParameters(
	const int64_t ingestionJobKey, const int64_t encodingJobKey, const nlohmann::json& encodingProfileDetailsRoot,
	const bool isVideo, // if false it means is audio
	const int videoTrackIndexToBeUsed, const int audioTrackIndexToBeUsed, const std::string &encodedStagingAssetPathName,
	const nlohmann::json &videoTracksRoot, // used only in case of _audioGroup
	const nlohmann::json &audioTracksRoot, // used only in case of _audioGroup

	bool &twoPasses, // out

	const std::string &ffmpegTempDir, const std::string &ffmpegTtfFontDir
)
{
	_ffmpegTempDir = ffmpegTempDir;
	_ffmpegTtfFontDir = ffmpegTtfFontDir;

	_multiTrackTemplateVariable = "__HEIGHT__";
	_multiTrackTemplatePart = _multiTrackTemplateVariable + "p";

	_ingestionJobKey = ingestionJobKey;
	_encodingJobKey = encodingJobKey;
	_encodedStagingAssetPathName = encodedStagingAssetPathName;
	_isVideo = isVideo;
	_videoTracksRoot = videoTracksRoot;
	_audioTracksRoot = audioTracksRoot;
	_videoTrackIndexToBeUsed = videoTrackIndexToBeUsed;
	_audioTrackIndexToBeUsed = audioTrackIndexToBeUsed;

	_initialized = false;
	twoPasses = false;
	if (encodingProfileDetailsRoot == nullptr)
		return;

	try
	{
		_httpStreamingFileFormat = "";
		_ffmpegHttpStreamingParameter = "";

		_ffmpegFileFormatParameter = "";

		_ffmpegVideoCodecParameter = "";
		_ffmpegVideoCodec = "";
		_ffmpegVideoProfileParameter = "";
		_ffmpegVideoOtherParameters = "";
		_ffmpegVideoFrameRateParameter = "";
		_ffmpegVideoKeyFramesRateParameter = "";
		_videoBitRatesInfo.clear();

		_ffmpegAudioCodecParameter = "";
		_ffmpegAudioCodec = "";
		_ffmpegAudioOtherParameters = "";
		_ffmpegAudioChannelsParameter = "";
		_ffmpegAudioSampleRateParameter = "";
		_audioBitRatesInfo.clear();

		settingFfmpegParameters(
			encodingProfileDetailsRoot, _isVideo,

			_httpStreamingFileFormat, _ffmpegHttpStreamingParameter,

			_ffmpegFileFormatParameter,

			_ffmpegVideoCodecParameter, _ffmpegVideoCodec, _ffmpegVideoProfileParameter, _ffmpegVideoOtherParameters, twoPasses, _ffmpegVideoFrameRateParameter,
			_ffmpegVideoKeyFramesRateParameter,

			_videoBitRatesInfo, _ffmpegAudioCodecParameter, _ffmpegAudioCodec, _ffmpegAudioOtherParameters, _ffmpegAudioChannelsParameter,
			_ffmpegAudioSampleRateParameter, _audioBitRatesInfo
		);

		_initialized = true;
	}
	catch (std::exception &e)
	{
		LOG_ERROR(
			"FFMpeg: init failed"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}"
			", exception: {}",
			_ingestionJobKey, _encodingJobKey, e.what()
		);

		throw;
	}
}

FFMpegEncodingParameters::~FFMpegEncodingParameters() = default;

void FFMpegEncodingParameters::applyEncoding(
	// -1: NO two passes
	// 0: YES two passes, first step
	// 1: YES two passes, second step
	int stepNumber,

	// In alcuni casi i parametro del file di output non deve essere aggiunto, ad esempio
	// per il LiveRecorder o LiveProxy o nei casi in cui il file di output viene deciso
	// dal chiamante senza seguire il fileFormat dell'encoding profile
	// Nel caso outputFileToBeAdded sia false, stepNumber has to be -1 (NO two passes)
	bool outputFileToBeAdded,

	// La risoluzione di un video viene gestita tramite un filtro video.
	// Il problema è che non possiamo avere due filtri video (-vf) in un comando.
	// Cosi', se un filtro video è stato già aggiunto al comando, non è possibile aggiungere qui
	// un ulteriore filtro video per configurare la risoluzione di un video.
	// Per cui, se abbiamo già un filtro video nel comando, la risoluzione deve essere inizializzata nel filtro
	// e quindi applyEncoding non la aggiunge
	bool videoResolutionToBeAdded,

	const nlohmann::json& filtersRoot,

	// out (in append)
	std::vector<std::string> &ffmpegArgumentList
)
{
	try
	{
		if (!_initialized)
		{
			std::string errorMessage = std::string("FFMpegEncodingParameters is not initialized") + ", _ingestionJobKey: " + std::to_string(_ingestionJobKey) +
								  ", _encodingJobKey: " + std::to_string(_encodingJobKey);
			throw std::runtime_error(errorMessage);
		}

		FFMpegFilters ffmpegFilters(_ffmpegTempDir, _ffmpegTtfFontDir, _ingestionJobKey, _encodingJobKey, -1);

		if (!_httpStreamingFileFormat.empty())
		{
			// hls or dash output

			std::string segmentTemplateDirectory;
			std::string segmentTemplatePathFileName;
			std::string stagingTemplateManifestAssetPathName;

			if (outputFileToBeAdded)
			{
				std::string manifestFileName = getManifestFileName();
				if (_httpStreamingFileFormat == "hls")
				{
					segmentTemplateDirectory = _encodedStagingAssetPathName + "/" + _multiTrackTemplatePart;

					segmentTemplatePathFileName =
						segmentTemplateDirectory + "/" + std::to_string(_ingestionJobKey) + "_" + std::to_string(_encodingJobKey) + "_%04d.ts";
				}

				stagingTemplateManifestAssetPathName = segmentTemplateDirectory + "/" + manifestFileName;
			}

			if (stepNumber == 0 || stepNumber == 1)
			{
				// YES two passes

				// check su if (outputFileToBeAdded) è inutile, con 2 passi outputFileToBeAdded deve essere true

				// used also in removeTwoPassesTemporaryFiles
				std::string prefixPasslogFileName = std::format("{}_{}", _ingestionJobKey, _encodingJobKey);
				std::string ffmpegTemplatePassLogPathFileName = std::format("{}/{}_{}.passlog",
					_ffmpegTempDir, prefixPasslogFileName, _multiTrackTemplatePart);

				if (stepNumber == 0) // YES two passes, first step
				{
					for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
					{
						videoIndex++;

						std::string ffmpegVideoResolutionParameter;
						int videoBitRateInKbps = -1;
						int videoHeight = -1;
						std::string ffmpegVideoBitRateParameter;
						std::string ffmpegVideoMaxRateParameter;
						std::optional<std::string> ffmpegVideoMinRateParameter;
						std::string ffmpegVideoBufSizeParameter;
						std::string ffmpegAudioBitRateParameter;

						tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
							ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
							ffmpegVideoBufSizeParameter) = videoBitRateInfo;

						if (_videoTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
						}
						addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
						if (ffmpegVideoMinRateParameter)
							addToArguments(*ffmpegVideoMinRateParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
						if (videoResolutionToBeAdded)
						{
							if (filtersRoot == nullptr)
								addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
							else
							{
								std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
									"", std::nullopt);

								if (!videoFilters.empty())
									addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
								else
									addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
							}
						}

						// It should be useless to add the audio parameters in phase 1 but,
						// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
						//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
						//  So, this is the reason, I'm adding phase 2 as well
						// + "-an "    // disable audio
						FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						if (_audioBitRatesInfo.size() > videoIndex)
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						else
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
						}

						ffmpegArgumentList.emplace_back("-threads");
						ffmpegArgumentList.emplace_back("0");
						ffmpegArgumentList.emplace_back("-pass");
						ffmpegArgumentList.emplace_back("1");
						ffmpegArgumentList.emplace_back("-passlogfile");
						{
							std::string ffmpegPassLogPathFileName =
								std::regex_replace(ffmpegTemplatePassLogPathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
						}

						// 2020-01-20: I removed the hls file format parameter
						//	because it was not working and added -f mp4.
						//	At the end it has to generate just the log file
						//	to be used in the second step
						// FFMpegEncodingParameters::addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);
						//
						// FFMpegEncodingParameters::addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
						ffmpegArgumentList.emplace_back("-f");
						// 2020-08-21: changed from mp4 to null
						ffmpegArgumentList.emplace_back("null");

						ffmpegArgumentList.emplace_back("/dev/null");
					}
				}
				else if (stepNumber == 1) // YES two passes, second step
				{
					for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
					{
						std::string ffmpegVideoResolutionParameter;
						int videoBitRateInKbps = -1;
						int videoHeight = -1;
						std::string ffmpegVideoBitRateParameter;
						std::string ffmpegVideoMaxRateParameter;
						std::optional<std::string> ffmpegVideoMinRateParameter;
						std::string ffmpegVideoBufSizeParameter;
						std::string ffmpegAudioBitRateParameter;

						tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
							ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
							ffmpegVideoBufSizeParameter) = videoBitRateInfo;

						{
							std::string segmentDirectory =
								std::regex_replace(segmentTemplateDirectory, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));

							LOG_INFO(
								"Creating directory"
								", segmentDirectory: {}",
								segmentDirectory
							);
							fs::create_directories(segmentDirectory);
							fs::permissions(
								segmentDirectory,
								fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec | fs::perms::group_read |
									fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
								fs::perm_options::replace
							);
						}

						if (_videoTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
						}
						addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
						if (ffmpegVideoMinRateParameter)
							addToArguments(*ffmpegVideoMinRateParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
						if (videoResolutionToBeAdded)
						{
							if (filtersRoot == nullptr)
								FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
							else
							{
								std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
									"", std::nullopt);

								if (!videoFilters.empty())
									addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
								else
									addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
							}
						}

						addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						if (_audioBitRatesInfo.size() > videoIndex)
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						else
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
						}

						addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);

						if (_httpStreamingFileFormat == "hls")
						{
							std::string segmentPathFileName =
								std::regex_replace(segmentTemplatePathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							ffmpegArgumentList.emplace_back("-hls_segment_filename");
							ffmpegArgumentList.push_back(segmentPathFileName);
						}

						{
							std::string stagingManifestAssetPathName =
								std::regex_replace(stagingTemplateManifestAssetPathName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
							ffmpegArgumentList.push_back(stagingManifestAssetPathName);
						}

						ffmpegArgumentList.emplace_back("-threads");
						ffmpegArgumentList.emplace_back("0");
						ffmpegArgumentList.emplace_back("-pass");
						ffmpegArgumentList.emplace_back("2");
						ffmpegArgumentList.emplace_back("-passlogfile");
						{
							std::string ffmpegPassLogPathFileName =
								std::regex_replace(ffmpegTemplatePassLogPathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
						}
					}
				}
			}
			else // NO two passes
			{
				for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
				{
					videoIndex++;

					std::string ffmpegVideoResolutionParameter;
					int videoBitRateInKbps = -1;
					int videoHeight = -1;
					std::string ffmpegVideoBitRateParameter;
					std::string ffmpegVideoMaxRateParameter;
					std::optional<std::string> ffmpegVideoMinRateParameter;
					std::string ffmpegVideoBufSizeParameter;
					std::string ffmpegAudioBitRateParameter;

					tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
						ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
						ffmpegVideoBufSizeParameter) = videoBitRateInfo;

					if (outputFileToBeAdded)
					{
						std::string segmentDirectory = std::regex_replace(segmentTemplateDirectory, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));

						LOG_INFO(
							"Creating directory"
							", segmentDirectory: {}",
							segmentDirectory
						);
						fs::create_directories(segmentDirectory);
						fs::permissions(
							segmentDirectory,
							fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec | fs::perms::group_read | fs::perms::group_exec |
								fs::perms::others_read | fs::perms::others_exec,
							fs::perm_options::replace
						);
					}

					if (_videoTrackIndexToBeUsed >= 0)
					{
						ffmpegArgumentList.emplace_back("-map");
						ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
					}
					if (_audioTrackIndexToBeUsed >= 0)
					{
						ffmpegArgumentList.emplace_back("-map");
						ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
					}
					addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
					addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
					addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
					addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
					addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
					if (ffmpegVideoMinRateParameter)
						addToArguments(*ffmpegVideoMinRateParameter, ffmpegArgumentList);
					addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
					addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
					addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
					if (videoResolutionToBeAdded)
					{
						if (filtersRoot == nullptr)
							addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
						else
						{
							std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
								"", std::nullopt);

							if (!videoFilters.empty())
								addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
							else
								addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
						}
					}
					ffmpegArgumentList.emplace_back("-threads");
					ffmpegArgumentList.emplace_back("0");
					addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
					if (_audioBitRatesInfo.size() > videoIndex)
						ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
					else
						ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
					addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
					addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
					addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
					addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
					if (filtersRoot != nullptr)
					{
						std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

						if (!audioFilters.empty())
							FFMpegEncodingParameters::addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
					}

					if (outputFileToBeAdded)
					{
						addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);

						if (_httpStreamingFileFormat == "hls")
						{
							std::string segmentPathFileName =
								std::regex_replace(segmentTemplatePathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							ffmpegArgumentList.emplace_back("-hls_segment_filename");
							ffmpegArgumentList.push_back(segmentPathFileName);
						}

						{
							std::string stagingManifestAssetPathName = StringUtils::replaceAll(stagingTemplateManifestAssetPathName,
								_multiTrackTemplateVariable, std::to_string(videoHeight));
							addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
							ffmpegArgumentList.push_back(stagingManifestAssetPathName);
						}
					}
				}
			}
		}
		else // NO hls or dash output
		{
			/* 2021-09-10: In case videoBitRatesInfo has more than one bitrates,
			 *	it has to be created one file for each bit rate and than
			 *	merge all in the last file with a copy command, i.e.:
			 *		- ffmpeg -i ./1.mp4 -i ./2.mp4 -c copy -map 0 -map 1 ./3.mp4
			 */

			if (stepNumber == 0 || stepNumber == 1) // YES two passes
			{
				// check su if (outputFileToBeAdded) è inutile, con 2 passi outputFileToBeAdded deve essere true

				// used also in removeTwoPassesTemporaryFiles
				std::string prefixPasslogFileName = std::format("{}_{}", _ingestionJobKey, _encodingJobKey);
				std::string ffmpegTemplatePassLogPathFileName = std::format("{}/{}_{}.passlog",
					_ffmpegTempDir, prefixPasslogFileName, _multiTrackTemplatePart);
				;

				if (stepNumber == 0) // YES two passes, first step
				{
					for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
					{
						std::string ffmpegVideoResolutionParameter;
						int videoBitRateInKbps = -1;
						int videoHeight = -1;
						std::string ffmpegVideoBitRateParameter;
						std::string ffmpegVideoMaxRateParameter;
						std::optional<std::string> ffmpegVideoMinRateParameter;
						std::string ffmpegVideoBufSizeParameter;
						std::string ffmpegAudioBitRateParameter;

						tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
							ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
							ffmpegVideoBufSizeParameter) = videoBitRateInfo;

						if (_videoTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
						}
						addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
						addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
						if (ffmpegVideoMinRateParameter)
							addToArguments(*ffmpegVideoMinRateParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
						if (videoResolutionToBeAdded)
						{
							if (filtersRoot == nullptr)
								addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
							else
							{
								std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
									"", std::nullopt);

								if (!videoFilters.empty())
									addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
								else
									addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
							}
						}
						ffmpegArgumentList.emplace_back("-threads");
						ffmpegArgumentList.emplace_back("0");
						ffmpegArgumentList.emplace_back("-pass");
						ffmpegArgumentList.emplace_back("1");
						ffmpegArgumentList.emplace_back("-passlogfile");
						{
							std::string ffmpegPassLogPathFileName =
								std::regex_replace(ffmpegTemplatePassLogPathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
						}
						// It should be useless to add the audio parameters in phase 1 but,
						// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
						//	it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
						//	So, this is the reason, I'm adding phase 2 as well
						// + "-an "    // disable audio
						addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						if (_audioBitRatesInfo.size() > videoIndex)
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						else
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
						}

						// 2020-08-21: changed from ffmpegFileFormatParameter to -f null
						// FFMpegEncodingParameters::addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
						ffmpegArgumentList.emplace_back("-f");
						ffmpegArgumentList.emplace_back("null");

						ffmpegArgumentList.emplace_back("/dev/null");
					}
				}
				else if (stepNumber == 1) // YES two passes, second step
				{
					std::string stagingTemplateEncodedAssetPathName = getMultiTrackEncodedStagingTemplateAssetPathName();

					for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
					{
						videoIndex++;

						std::string ffmpegVideoResolutionParameter;
						int videoBitRateInKbps = -1;
						int videoHeight = -1;
						std::string ffmpegVideoBitRateParameter;
						std::string ffmpegVideoMaxRateParameter;
						std::optional<std::string> ffmpegVideoMinRateParameter;
						std::string ffmpegVideoBufSizeParameter;
						std::string ffmpegAudioBitRateParameter;

						tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
							ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
							ffmpegVideoBufSizeParameter) = videoBitRateInfo;

						if (_videoTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
						}
						addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
						addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
						if (ffmpegVideoMinRateParameter)
							addToArguments(*ffmpegVideoMinRateParameter, ffmpegArgumentList);
						addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
						if (videoResolutionToBeAdded)
						{
							if (filtersRoot == nullptr)
								addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
							else
							{
								std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
									"", std::nullopt);

								if (!videoFilters.empty())
									addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
								else
									addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
							}
						}
						ffmpegArgumentList.emplace_back("-threads");
						ffmpegArgumentList.emplace_back("0");
						ffmpegArgumentList.emplace_back("-pass");
						ffmpegArgumentList.emplace_back("2");
						ffmpegArgumentList.emplace_back("-passlogfile");
						{
							std::string ffmpegPassLogPathFileName = StringUtils::replaceAll(ffmpegTemplatePassLogPathFileName,
								_multiTrackTemplateVariable, std::to_string(videoHeight));
							ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
						}
						addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						if (_audioBitRatesInfo.size() > videoIndex)
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						else
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
						}

						addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
						if (_videoBitRatesInfo.size() > 1)
						{
							std::string newStagingEncodedAssetPathName = StringUtils::replaceAll(stagingTemplateEncodedAssetPathName,
								_multiTrackTemplateVariable, std::to_string(videoHeight));
							ffmpegArgumentList.push_back(newStagingEncodedAssetPathName);
						}
						else
							ffmpegArgumentList.push_back(_encodedStagingAssetPathName);
					}
				}
			}
			else // NO two passes
			{
				std::string stagingTemplateEncodedAssetPathName;
				if (outputFileToBeAdded)
					stagingTemplateEncodedAssetPathName = getMultiTrackEncodedStagingTemplateAssetPathName();

				if (_isVideo)
				{
					if (!_videoBitRatesInfo.empty())
					{
						for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
						{
							videoIndex++;

							std::string ffmpegVideoResolutionParameter;
							int videoBitRateInKbps = -1;
							int videoHeight = -1;
							std::string ffmpegVideoBitRateParameter;
							std::string ffmpegVideoMaxRateParameter;
							std::optional<std::string> ffmpegVideoMinRateParameter;
							std::string ffmpegVideoBufSizeParameter;
							std::string ffmpegAudioBitRateParameter;

							tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
								ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
								ffmpegVideoBufSizeParameter) = videoBitRateInfo;

							if (_videoTrackIndexToBeUsed >= 0)
							{
								ffmpegArgumentList.emplace_back("-map");
								ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
							}
							if (_audioTrackIndexToBeUsed >= 0)
							{
								ffmpegArgumentList.emplace_back("-map");
								ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
							}
							addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
							addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
							addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
							addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
							addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
							if (ffmpegVideoMinRateParameter)
								addToArguments(*ffmpegVideoMinRateParameter, ffmpegArgumentList);
							addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
							addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
							addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
							if (videoResolutionToBeAdded)
							{
								if (filtersRoot == nullptr)
									addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
								else
								{
									std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
										"", std::nullopt);

									if (!videoFilters.empty())
										addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
									else
										addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
								}
							}
							ffmpegArgumentList.emplace_back("-threads");
							ffmpegArgumentList.emplace_back("0");
							addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
							if (_audioBitRatesInfo.size() > videoIndex)
								ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
							else
								ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
							addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
							addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
							addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
							addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
							if (filtersRoot != nullptr)
							{
								std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

								if (!audioFilters.empty())
									addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
							}

							if (outputFileToBeAdded)
							{
								addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
								if (_videoBitRatesInfo.size() > 1)
								{
									std::string newStagingEncodedAssetPathName = StringUtils::replaceAll(stagingTemplateEncodedAssetPathName,
										_multiTrackTemplateVariable, std::to_string(videoHeight));
									ffmpegArgumentList.push_back(newStagingEncodedAssetPathName);
								}
								else
									ffmpegArgumentList.push_back(_encodedStagingAssetPathName);
							}
						}
					}
					else
					{
						// 2023-05-07: è un video senza videoBitRates. E' lo scenario in cui gli è stato dato
						//	un profile di encoding solo audio.
						//	In questo scenario _ffmpegVideoCodecParameter è stato inizializzato con "c:v copy "
						//	in settingFfmpegParameters

						std::string ffmpegAudioBitRateParameter;

						if (_videoTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.push_back("-map");
							ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
						}
						addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
						// if (videoResolutionToBeAdded)
						// 	FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter,
						// 		ffmpegArgumentList);
						ffmpegArgumentList.emplace_back("-threads");
						ffmpegArgumentList.emplace_back("0");
						addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						// if (_audioBitRatesInfo.size() > videoIndex)
						// 	ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						// else
						ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								addToArguments(std::format("-filter:a {}", audioFilters), ffmpegArgumentList);
						}

						if (outputFileToBeAdded)
						{
							// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
							// if (_videoBitRatesInfo.size() > 1)
							// {
							// 	std::string newStagingEncodedAssetPathName =
							// 		std::regex_replace(stagingTemplateEncodedAssetPathName,
							// 			std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							// 	ffmpegArgumentList.push_back(newStagingEncodedAssetPathName);
							// }
							// else
							ffmpegArgumentList.push_back(_encodedStagingAssetPathName);
						}
					}
				}
				else
				{
					for (auto ffmpegAudioBitRateParameter : _audioBitRatesInfo)
					{
						if (_audioTrackIndexToBeUsed >= 0)
						{
							ffmpegArgumentList.emplace_back("-map");
							ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
						}
						ffmpegArgumentList.emplace_back("-threads");
						ffmpegArgumentList.emplace_back("0");
						addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
						}

						if (outputFileToBeAdded)
						{
							addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
							/*
							if (videoBitRatesInfo.size() > 1)
							{
								std::string newStagingEncodedAssetPathName =
									std::regex_replace(stagingTemplateEncodedAssetPathName,
										std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
								ffmpegArgumentList.push_back(newStagingEncodedAssetPathName);
							}
							else
							*/
							ffmpegArgumentList.push_back(_encodedStagingAssetPathName);
						}
					}
				}
			}
		}
	}
	catch (std::exception &e)
	{
		LOG_ERROR(
			"FFMpeg: applyEncoding failed"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}"
			", exception: {}",
			_ingestionJobKey, _encodingJobKey, e.what()
		);

		throw;
	}
}

void FFMpegEncodingParameters::applyEncoding(
	// -1: NO two passes
	// 0: YES two passes, first step
	// 1: YES two passes, second step
	int stepNumber,

	// In alcuni casi i parametro del file di output non deve essere aggiunto, ad esempio
	// per il LiveRecorder o LiveProxy o nei casi in cui il file di output viene deciso
	// dal chiamante senza seguire il fileFormat dell'encoding profile
	// Nel caso outputFileToBeAdded sia false, stepNumber has to be -1 (NO two passes)
	bool outputFileToBeAdded,

	// La risoluzione di un video viene gestita tramite un filtro video.
	// Il problema è che non possiamo avere due filtri video (-vf) in un comando.
	// Cosi', se un filtro video è stato già aggiunto al comando, non è possibile aggiungere qui
	// un ulteriore filtro video per configurare la risoluzione di un video.
	// Per cui, se abbiamo già un filtro video nel comando, la risoluzione deve essere inizializzata nel filtro
	// e quindi applyEncoding non la aggiunge
	bool videoResolutionToBeAdded,

	const nlohmann::json& filtersRoot,

	// out (in append)
	FFMpegEngine& ffMpegEngine
)
{
	try
	{
		if (!_initialized)
		{
			std::string errorMessage = std::string("FFMpegEncodingParameters is not initialized") + ", _ingestionJobKey: " + std::to_string(_ingestionJobKey) +
								  ", _encodingJobKey: " + std::to_string(_encodingJobKey);
			throw std::runtime_error(errorMessage);
		}

		FFMpegFilters ffmpegFilters(_ffmpegTempDir, _ffmpegTtfFontDir, _ingestionJobKey, _encodingJobKey, -1);

		if (!_httpStreamingFileFormat.empty())
		{
			// hls or dash output

			std::string segmentTemplateDirectory;
			std::string segmentTemplatePathFileName;
			std::string stagingTemplateManifestAssetPathName;

			if (outputFileToBeAdded)
			{
				std::string manifestFileName = getManifestFileName();
				if (_httpStreamingFileFormat == "hls")
				{
					segmentTemplateDirectory = _encodedStagingAssetPathName + "/" + _multiTrackTemplatePart;

					segmentTemplatePathFileName =
						segmentTemplateDirectory + "/" + std::to_string(_ingestionJobKey) + "_" + std::to_string(_encodingJobKey) + "_%04d.ts";
				}

				stagingTemplateManifestAssetPathName = segmentTemplateDirectory + "/" + manifestFileName;
			}

			if (stepNumber == 0 || stepNumber == 1)
			{
				// YES two passes

				// check su if (outputFileToBeAdded) è inutile, con 2 passi outputFileToBeAdded deve essere true

				// used also in removeTwoPassesTemporaryFiles
				std::string prefixPasslogFileName = std::format("{}_{}", _ingestionJobKey, _encodingJobKey);
				std::string ffmpegTemplatePassLogPathFileName = std::format("{}/{}_{}.passlog",
					_ffmpegTempDir, prefixPasslogFileName, _multiTrackTemplatePart);
				;

				if (stepNumber == 0) // YES two passes, first step
				{
					for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
					{
						videoIndex++;

						std::string ffmpegVideoResolutionParameter;
						int videoBitRateInKbps = -1;
						int videoHeight = -1;
						std::string ffmpegVideoBitRateParameter;
						std::string ffmpegVideoMaxRateParameter;
						std::optional<std::string> ffmpegVideoMinRateParameter;
						std::string ffmpegVideoBufSizeParameter;
						std::string ffmpegAudioBitRateParameter;

						tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
							ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
							ffmpegVideoBufSizeParameter) = videoBitRateInfo;

						FFMpegEngine::Output& mainOutput = ffMpegEngine.addOutput();

						if (_videoTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
							mainOutput.map(std::format("0:v:{}", _videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
							mainOutput.map(std::format("0:a:{}", _audioTrackIndexToBeUsed));
						}
						mainOutput.withVideoCodec(_ffmpegVideoCodec);
						mainOutput.addArgs(_ffmpegVideoProfileParameter);
						mainOutput.addArgs(ffmpegVideoBitRateParameter);
						mainOutput.addArgs(_ffmpegVideoOtherParameters);
						mainOutput.addArgs(_ffmpegVideoFrameRateParameter);
						mainOutput.addArgs(_ffmpegVideoKeyFramesRateParameter);
						mainOutput.addArgs(ffmpegVideoMaxRateParameter);
						if (ffmpegVideoMinRateParameter)
							mainOutput.addArgs(*ffmpegVideoMinRateParameter);
						mainOutput.addArgs(ffmpegVideoBufSizeParameter);
						if (videoResolutionToBeAdded)
						{
							if (filtersRoot == nullptr)
								// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
								mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
							else
							{
								std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
									"", std::nullopt);

								if (!videoFilters.empty())
									// FFMpegEncodingParameters::addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
									mainOutput.addArgs(std::format("-filter:v {}", videoFilters));
								else
									// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
									mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
							}
						}

						// It should be useless to add the audio parameters in phase 1 but,
						// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
						//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
						//  So, this is the reason, I'm adding phase 2 as well
						// + "-an "    // disable audio
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						mainOutput.withAudioCodec(_ffmpegAudioCodec);
						if (_audioBitRatesInfo.size() > videoIndex)
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						else
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(ffmpegAudioBitRateParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioOtherParameters);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioChannelsParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioSampleRateParameter);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								// FFMpegEncodingParameters::addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
								mainOutput.addArgs(std::format("-filter:a {}", audioFilters));
						}

						// ffmpegArgumentList.push_back("-threads");
						// ffmpegArgumentList.push_back("0");
						// ffmpegArgumentList.push_back("-pass");
						// ffmpegArgumentList.push_back("1");
						// ffmpegArgumentList.push_back("-passlogfile");
						mainOutput.addArgs("-threads 0 -pass 1 -passlogfile");
						{
							std::string ffmpegPassLogPathFileName =
								std::regex_replace(ffmpegTemplatePassLogPathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							// ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
							mainOutput.addArg(ffmpegPassLogPathFileName);
						}

						// 2020-01-20: I removed the hls file format parameter
						//	because it was not working and added -f mp4.
						//	At the end it has to generate just the log file
						//	to be used in the second step
						// FFMpegEncodingParameters::addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);
						//
						// FFMpegEncodingParameters::addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
						// ffmpegArgumentList.push_back("-f");
						// 2020-08-21: changed from mp4 to null
						// ffmpegArgumentList.push_back("null");
						mainOutput.addArgs("-f null");

						// ffmpegArgumentList.push_back("/dev/null");
						mainOutput.setPath("/dev/null");
					}
				}
				else if (stepNumber == 1) // YES two passes, second step
				{
					for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
					{
						videoIndex++;

						std::string ffmpegVideoResolutionParameter;
						int videoBitRateInKbps = -1;
						int videoHeight = -1;
						std::string ffmpegVideoBitRateParameter;
						std::string ffmpegVideoMaxRateParameter;
						std::optional<std::string> ffmpegVideoMinRateParameter;
						std::string ffmpegVideoBufSizeParameter;
						std::string ffmpegAudioBitRateParameter;

						tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
							ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
							ffmpegVideoBufSizeParameter) = videoBitRateInfo;

						{
							std::string segmentDirectory = StringUtils::replaceAll(segmentTemplateDirectory,
								_multiTrackTemplateVariable, std::to_string(videoHeight));

							LOG_INFO(
								"Creating directory"
								", segmentDirectory: {}",
								segmentDirectory
							);
							fs::create_directories(segmentDirectory);
							fs::permissions(
								segmentDirectory,
								fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec | fs::perms::group_read |
									fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
								fs::perm_options::replace
							);
						}

						FFMpegEngine::Output& mainOutput = ffMpegEngine.addOutput();

						if (_videoTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
							mainOutput.map(std::format("0:v:{}", _videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
							mainOutput.map(std::format("0:a:{}", _audioTrackIndexToBeUsed));
						}
						mainOutput.withVideoCodec(_ffmpegVideoCodec);
						mainOutput.addArgs(_ffmpegVideoProfileParameter);
						mainOutput.addArgs(ffmpegVideoBitRateParameter);
						mainOutput.addArgs(_ffmpegVideoOtherParameters);
						mainOutput.addArgs(_ffmpegVideoFrameRateParameter);
						mainOutput.addArgs(_ffmpegVideoKeyFramesRateParameter);
						mainOutput.addArgs(ffmpegVideoMaxRateParameter);
						if (ffmpegVideoMinRateParameter)
							mainOutput.addArgs(*ffmpegVideoMinRateParameter);
						mainOutput.addArgs(ffmpegVideoBufSizeParameter);
						if (videoResolutionToBeAdded)
						{
							if (filtersRoot == nullptr)
								// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
								mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
							else
							{
								std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
									"", std::nullopt);

								if (!videoFilters.empty())
									// FFMpegEncodingParameters::addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
									mainOutput.addArgs(std::format("-filter:v {}", videoFilters));
								else
									// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
									mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
							}
						}

						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						mainOutput.withAudioCodec(_ffmpegAudioCodec);
						if (_audioBitRatesInfo.size() > videoIndex)
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						else
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(ffmpegAudioBitRateParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioOtherParameters);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioChannelsParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioSampleRateParameter);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								// FFMpegEncodingParameters::addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
								mainOutput.addArgs(std::format("-filter:a {}", audioFilters));
						}

						// FFMpegEncodingParameters::addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegHttpStreamingParameter);

						if (_httpStreamingFileFormat == "hls")
						{
							std::string segmentPathFileName =
								std::regex_replace(segmentTemplatePathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							// ffmpegArgumentList.push_back("-hls_segment_filename");
							// ffmpegArgumentList.push_back(segmentPathFileName);
							mainOutput.addArg("-hls_segment_filename");
							mainOutput.addArg(segmentPathFileName);
						}

						{
							std::string stagingManifestAssetPathName =
								std::regex_replace(stagingTemplateManifestAssetPathName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
							mainOutput.addArgs(_ffmpegFileFormatParameter);
							// ffmpegArgumentList.push_back(stagingManifestAssetPathName);
							mainOutput.setPath(stagingManifestAssetPathName);
						}

						// ffmpegArgumentList.push_back("-threads");
						// ffmpegArgumentList.push_back("0");
						// ffmpegArgumentList.push_back("-pass");
						// ffmpegArgumentList.push_back("2");
						// ffmpegArgumentList.push_back("-passlogfile");
						mainOutput.addArgs("-threads 0 -pass 2 -passlogfile");
						{
							std::string ffmpegPassLogPathFileName =
								std::regex_replace(ffmpegTemplatePassLogPathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							// ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
							mainOutput.addArg(ffmpegPassLogPathFileName);
						}
					}
				}
			}
			else // NO two passes
			{
				for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
				{
					videoIndex++;

					std::string ffmpegVideoResolutionParameter;
					int videoBitRateInKbps = -1;
					int videoHeight = -1;
					std::string ffmpegVideoBitRateParameter;
					std::string ffmpegVideoMaxRateParameter;
					std::optional<std::string> ffmpegVideoMinRateParameter;
					std::string ffmpegVideoBufSizeParameter;
					std::string ffmpegAudioBitRateParameter;

					tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
						ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
						ffmpegVideoBufSizeParameter) = videoBitRateInfo;

					if (outputFileToBeAdded)
					{
						std::string segmentDirectory = StringUtils::replaceAll(segmentTemplateDirectory,
							_multiTrackTemplateVariable, std::to_string(videoHeight));

						LOG_INFO(
							"Creating directory"
							", segmentDirectory: {}",
							segmentDirectory
						);
						fs::create_directories(segmentDirectory);
						fs::permissions(
							segmentDirectory,
							fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec | fs::perms::group_read | fs::perms::group_exec |
								fs::perms::others_read | fs::perms::others_exec,
							fs::perm_options::replace
						);
					}

					FFMpegEngine::Output& mainOutput = ffMpegEngine.addOutput();

					if (_videoTrackIndexToBeUsed >= 0)
					{
						// ffmpegArgumentList.push_back("-map");
						// ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
						mainOutput.map(std::format("0:v:{}", _videoTrackIndexToBeUsed));
					}
					if (_audioTrackIndexToBeUsed >= 0)
					{
						// ffmpegArgumentList.push_back("-map");
						// ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
						mainOutput.map(std::format("0:a:{}", _audioTrackIndexToBeUsed));
					}
					mainOutput.withVideoCodec(_ffmpegVideoCodec);
					mainOutput.addArgs(_ffmpegVideoProfileParameter);
					mainOutput.addArgs(ffmpegVideoBitRateParameter);
					mainOutput.addArgs(_ffmpegVideoOtherParameters);
					mainOutput.addArgs(ffmpegVideoMaxRateParameter);
					if (ffmpegVideoMinRateParameter)
						mainOutput.addArgs(*ffmpegVideoMinRateParameter);
					mainOutput.addArgs(ffmpegVideoBufSizeParameter);
					mainOutput.addArgs(_ffmpegVideoFrameRateParameter);
					mainOutput.addArgs(_ffmpegVideoKeyFramesRateParameter);
					if (videoResolutionToBeAdded)
					{
						if (filtersRoot == nullptr)
							// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
							mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
						else
						{
							std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
								"", std::nullopt);

							if (!videoFilters.empty())
								// FFMpegEncodingParameters::addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
								mainOutput.addArgs(std::format("-filter:v {}", videoFilters));
							else
								// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
								mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
						}
					}
					// ffmpegArgumentList.push_back("-threads");
					// ffmpegArgumentList.push_back("0");
					mainOutput.addArgs("-threads 0");
					// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
					mainOutput.withAudioCodec(_ffmpegAudioCodec);
					if (_audioBitRatesInfo.size() > videoIndex)
						ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
					else
						ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
					// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
					mainOutput.addArgs(ffmpegAudioBitRateParameter);
					// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
					mainOutput.addArgs(_ffmpegAudioOtherParameters);
					// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
					mainOutput.addArgs(_ffmpegAudioChannelsParameter);
					// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
					mainOutput.addArgs(_ffmpegAudioSampleRateParameter);
					if (filtersRoot != nullptr)
					{
						std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

						if (!audioFilters.empty())
							// FFMpegEncodingParameters::addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
							mainOutput.addArgs(std::format("-filter:a {}", audioFilters));
					}

					if (outputFileToBeAdded)
					{
						// FFMpegEncodingParameters::addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegHttpStreamingParameter);

						if (_httpStreamingFileFormat == "hls")
						{
							std::string segmentPathFileName =
								std::regex_replace(segmentTemplatePathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							// ffmpegArgumentList.push_back("-hls_segment_filename");
							// ffmpegArgumentList.push_back(segmentPathFileName);
							mainOutput.addArg("-hls_segment_filename");
							mainOutput.addArg(segmentPathFileName);
						}

						{
							std::string stagingManifestAssetPathName = StringUtils::replaceAll(stagingTemplateManifestAssetPathName,
								_multiTrackTemplateVariable, std::to_string(videoHeight));
							// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
							mainOutput.addArgs(_ffmpegFileFormatParameter);
							// ffmpegArgumentList.push_back(stagingManifestAssetPathName);
							mainOutput.setPath(stagingManifestAssetPathName);
						}
					}
				}
			}
		}
		else // NO hls or dash output
		{
			/* 2021-09-10: In case videoBitRatesInfo has more than one bitrates,
			 *	it has to be created one file for each bit rate and than
			 *	merge all in the last file with a copy command, i.e.:
			 *		- ffmpeg -i ./1.mp4 -i ./2.mp4 -c copy -map 0 -map 1 ./3.mp4
			 */

			if (stepNumber == 0 || stepNumber == 1) // YES two passes
			{
				// check su if (outputFileToBeAdded) è inutile, con 2 passi outputFileToBeAdded deve essere true

				// used also in removeTwoPassesTemporaryFiles
				std::string prefixPasslogFileName = std::format("{}_{}", _ingestionJobKey, _encodingJobKey);
				std::string ffmpegTemplatePassLogPathFileName = std::format("{}/{}_{}.passlog",
					_ffmpegTempDir, prefixPasslogFileName, _multiTrackTemplatePart);

				if (stepNumber == 0) // YES two passes, first step
				{
					for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
					{
						videoIndex++;

						std::string ffmpegVideoResolutionParameter;
						int videoBitRateInKbps = -1;
						int videoHeight = -1;
						std::string ffmpegVideoBitRateParameter;
						std::string ffmpegVideoMaxRateParameter;
						std::optional<std::string> ffmpegVideoMinRateParameter;
						std::string ffmpegVideoBufSizeParameter;
						std::string ffmpegAudioBitRateParameter;

						tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
							ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
							ffmpegVideoBufSizeParameter) = videoBitRateInfo;

						FFMpegEngine::Output& mainOutput = ffMpegEngine.addOutput();

						if (_videoTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
							mainOutput.map(std::format(("0:v:{}"), _videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
							mainOutput.map(std::format(("0:a:{}"), _audioTrackIndexToBeUsed));
						}
						mainOutput.withVideoCodec(_ffmpegVideoCodec);
						mainOutput.addArgs(_ffmpegVideoProfileParameter);
						mainOutput.addArgs(ffmpegVideoBitRateParameter);
						mainOutput.addArgs(_ffmpegVideoOtherParameters);
						mainOutput.addArgs(ffmpegVideoMaxRateParameter);
						if (ffmpegVideoMinRateParameter)
							mainOutput.addArgs(*ffmpegVideoMinRateParameter);
						mainOutput.addArgs(ffmpegVideoBufSizeParameter);
						mainOutput.addArgs(_ffmpegVideoFrameRateParameter);
						mainOutput.addArgs(_ffmpegVideoKeyFramesRateParameter);
						if (videoResolutionToBeAdded)
						{
							if (filtersRoot == nullptr)
								// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
								mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
							else
							{
								std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
									"", std::nullopt);

								if (!videoFilters.empty())
									// FFMpegEncodingParameters::addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
									mainOutput.addArgs(std::format("-filter:v {}", videoFilters));
								else
									// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
									mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
							}
						}
						mainOutput.addArgs("-threads 0 -pass 1 -passlogfile");
						{
							std::string ffmpegPassLogPathFileName = StringUtils::replaceAll(ffmpegTemplatePassLogPathFileName,
								_multiTrackTemplateVariable, std::to_string(videoHeight));
							mainOutput.addArg(ffmpegPassLogPathFileName);
						}
						// It should be useless to add the audio parameters in phase 1 but,
						// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
						//	it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
						//	So, this is the reason, I'm adding phase 2 as well
						// + "-an "    // disable audio
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						mainOutput.withAudioCodec(_ffmpegAudioCodec);
						if (_audioBitRatesInfo.size() > videoIndex)
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						else
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						mainOutput.addArgs(ffmpegAudioBitRateParameter);
						mainOutput.addArgs(_ffmpegAudioOtherParameters);
						mainOutput.addArgs(_ffmpegAudioChannelsParameter);
						mainOutput.addArgs(_ffmpegAudioSampleRateParameter);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								mainOutput.addArgs("-filter:a " + audioFilters);
						}

						// 2020-08-21: changed from ffmpegFileFormatParameter to -f null
						// FFMpegEncodingParameters::addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
						// ffmpegArgumentList.push_back("-f");
						// ffmpegArgumentList.push_back("null");
						mainOutput.addArgs("-f null");

						// ffmpegArgumentList.push_back("/dev/null");
						mainOutput.setPath("/dev/null");
					}
				}
				else if (stepNumber == 1) // YES two passes, second step
				{
					std::string stagingTemplateEncodedAssetPathName = getMultiTrackEncodedStagingTemplateAssetPathName();

					for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
					{
						videoIndex++;

						std::string ffmpegVideoResolutionParameter;
						int videoBitRateInKbps = -1;
						int videoHeight = -1;
						std::string ffmpegVideoBitRateParameter;
						std::string ffmpegVideoMaxRateParameter;
						std::optional<std::string> ffmpegVideoMinRateParameter;
						std::string ffmpegVideoBufSizeParameter;
						std::string ffmpegAudioBitRateParameter;

						tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
							ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
							ffmpegVideoBufSizeParameter) = videoBitRateInfo;

						FFMpegEngine::Output& mainOutput = ffMpegEngine.addOutput();

						if (_videoTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
							mainOutput.map(std::format("0:v:{}", _videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
							mainOutput.map(std::format("0:a:{}", _audioTrackIndexToBeUsed));
						}
						mainOutput.withVideoCodec(_ffmpegVideoCodec);
						mainOutput.addArgs(_ffmpegVideoProfileParameter);
						mainOutput.addArgs(ffmpegVideoBitRateParameter);
						mainOutput.addArgs(_ffmpegVideoOtherParameters);
						mainOutput.addArgs(ffmpegVideoMaxRateParameter);
						if (ffmpegVideoMinRateParameter)
							mainOutput.addArgs(*ffmpegVideoMinRateParameter);
						mainOutput.addArgs(ffmpegVideoBufSizeParameter);
						mainOutput.addArgs(_ffmpegVideoFrameRateParameter);
						mainOutput.addArgs(_ffmpegVideoKeyFramesRateParameter);
						if (videoResolutionToBeAdded)
						{
							if (filtersRoot == nullptr)
								// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
								mainOutput.addArgs("-vf " + ffmpegVideoResolutionParameter);
							else
							{
								std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
									"", std::nullopt);

								if (!videoFilters.empty())
									// FFMpegEncodingParameters::addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
									mainOutput.addArgs("-filter:v " + videoFilters);
								else
									// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
									mainOutput.addArgs("-vf " + ffmpegVideoResolutionParameter);
							}
						}
						// ffmpegArgumentList.push_back("-threads");
						// ffmpegArgumentList.push_back("0");
						// ffmpegArgumentList.push_back("-pass");
						// ffmpegArgumentList.push_back("2");
						// ffmpegArgumentList.push_back("-passlogfile");
						mainOutput.addArgs("-threads 0 -pass 2 -passlogfile");
						{
							std::string ffmpegPassLogPathFileName =
								std::regex_replace(ffmpegTemplatePassLogPathFileName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							// ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
							mainOutput.addArg(ffmpegPassLogPathFileName);
						}
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						mainOutput.withAudioCodec(_ffmpegAudioCodec);
						if (_audioBitRatesInfo.size() > videoIndex)
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						else
							ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(ffmpegAudioBitRateParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioOtherParameters);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioChannelsParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioSampleRateParameter);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								// FFMpegEncodingParameters::addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
								mainOutput.addArgs("-filter:a " + audioFilters);
						}

						// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegFileFormatParameter);
						if (_videoBitRatesInfo.size() > 1)
						{
							std::string newStagingEncodedAssetPathName = StringUtils::replaceAll(stagingTemplateEncodedAssetPathName,
								_multiTrackTemplateVariable, std::to_string(videoHeight));
							// ffmpegArgumentList.push_back(newStagingEncodedAssetPathName);
							mainOutput.setPath(newStagingEncodedAssetPathName);
						}
						else
							// ffmpegArgumentList.push_back(_encodedStagingAssetPathName);
							mainOutput.setPath(_encodedStagingAssetPathName);
					}
				}
			}
			else // NO two passes
			{
				std::string stagingTemplateEncodedAssetPathName;
				if (outputFileToBeAdded)
					stagingTemplateEncodedAssetPathName = getMultiTrackEncodedStagingTemplateAssetPathName();

				if (_isVideo)
				{
					if (!_videoBitRatesInfo.empty())
					{
						for (int videoIndex = -1; const auto& videoBitRateInfo: _videoBitRatesInfo)
						{
							videoIndex++;

							std::string ffmpegVideoResolutionParameter;
							int videoBitRateInKbps = -1;
							int videoHeight = -1;
							std::string ffmpegVideoBitRateParameter;
							std::string ffmpegVideoMaxRateParameter;
							std::optional<std::string> ffmpegVideoMinRateParameter;
							std::string ffmpegVideoBufSizeParameter;
							std::string ffmpegAudioBitRateParameter;

							tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, videoHeight,
								ffmpegVideoBitRateParameter, ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter,
								ffmpegVideoBufSizeParameter) = videoBitRateInfo;

							FFMpegEngine::Output& mainOutput = ffMpegEngine.addOutput();

							if (_videoTrackIndexToBeUsed >= 0)
							{
								// ffmpegArgumentList.push_back("-map");
								// ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
								mainOutput.map(std::format("0:v:{}", _videoTrackIndexToBeUsed));
							}
							if (_audioTrackIndexToBeUsed >= 0)
							{
								// ffmpegArgumentList.push_back("-map");
								// ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
								mainOutput.addArgs(std::format("0:a:{}", _audioTrackIndexToBeUsed));
							}
							mainOutput.withVideoCodec(_ffmpegVideoCodec);
							mainOutput.addArgs(_ffmpegVideoProfileParameter);
							mainOutput.addArgs(ffmpegVideoBitRateParameter);
							mainOutput.addArgs(_ffmpegVideoOtherParameters);
							mainOutput.addArgs(ffmpegVideoMaxRateParameter);
							if (ffmpegVideoMinRateParameter)
								mainOutput.addArgs(*ffmpegVideoMinRateParameter);
							mainOutput.addArgs(ffmpegVideoBufSizeParameter);
							mainOutput.addArgs(_ffmpegVideoFrameRateParameter);
							mainOutput.addArgs(_ffmpegVideoKeyFramesRateParameter);
							if (videoResolutionToBeAdded)
							{
								if (filtersRoot == nullptr)
									// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
									mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
								else
								{
									std::string videoFilters = ffmpegFilters.addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter,
										"", std::nullopt);

									if (!videoFilters.empty())
										// FFMpegEncodingParameters::addToArguments(std::string("-filter:v ") + videoFilters, ffmpegArgumentList);
										mainOutput.addArgs(std::format("-vf {}", videoFilters));
									else
										// FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
										mainOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
								}
							}
							// ffmpegArgumentList.push_back("-threads");
							// ffmpegArgumentList.push_back("0");
							mainOutput.addArgs("-threads 0");
							// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
							mainOutput.withVideoCodec(_ffmpegVideoCodec);
							if (_audioBitRatesInfo.size() > videoIndex)
								ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
							else
								ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
							// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
							mainOutput.addArgs(ffmpegAudioBitRateParameter);
							// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
							mainOutput.addArgs(_ffmpegAudioOtherParameters);
							// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
							mainOutput.addArgs(_ffmpegAudioChannelsParameter);
							// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
							mainOutput.addArgs(_ffmpegAudioSampleRateParameter);
							if (filtersRoot != nullptr)
							{
								std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

								if (!audioFilters.empty())
									// FFMpegEncodingParameters::addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
									mainOutput.addArgs(std::format("-filter:a {}", audioFilters));
							}

							if (outputFileToBeAdded)
							{
								// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
								mainOutput.addArgs(_ffmpegFileFormatParameter);
								if (_videoBitRatesInfo.size() > 1)
								{
									std::string newStagingEncodedAssetPathName = std::regex_replace(
										stagingTemplateEncodedAssetPathName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight)
									);
									// ffmpegArgumentList.push_back(newStagingEncodedAssetPathName);
									mainOutput.setPath(newStagingEncodedAssetPathName);
								}
								else
									// ffmpegArgumentList.push_back(_encodedStagingAssetPathName);
									mainOutput.setPath(_encodedStagingAssetPathName);
							}
						}
					}
					else
					{
						// 2023-05-07: è un video senza videoBitRates. E' lo scenario in cui gli è stato dato
						//	un profile di encoding solo audio.
						//	In questo scenario _ffmpegVideoCodecParameter è stato inizializzato con "c:v copy "
						//	in settingFfmpegParameters

						std::string ffmpegAudioBitRateParameter;

						FFMpegEngine::Output& mainOutput = ffMpegEngine.addOutput();

						if (_videoTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:v:") + std::to_string(_videoTrackIndexToBeUsed));
							mainOutput.addArgs(std::format("0:v:{}", _videoTrackIndexToBeUsed));
						}
						if (_audioTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
							mainOutput.addArgs(std::format("0:a:{}", _audioTrackIndexToBeUsed));
						}
						// FFMpegEncodingParameters::addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
						mainOutput.withVideoCodec(_ffmpegVideoCodec);
						// FFMpegEncodingParameters::addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
						// FFMpegEncodingParameters::addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
						// if (videoResolutionToBeAdded)
						// 	FFMpegEncodingParameters::addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter,
						// 		ffmpegArgumentList);
						// ffmpegArgumentList.push_back("-threads");
						// ffmpegArgumentList.push_back("0");
						mainOutput.addArgs("-threads 0");
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						mainOutput.withAudioCodec(_ffmpegAudioCodec);
						// if (_audioBitRatesInfo.size() > videoIndex)
						// 	ffmpegAudioBitRateParameter = _audioBitRatesInfo[videoIndex];
						// else
						ffmpegAudioBitRateParameter = _audioBitRatesInfo[_audioBitRatesInfo.size() - 1];
						// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(ffmpegAudioBitRateParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioOtherParameters);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioChannelsParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioSampleRateParameter);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								// FFMpegEncodingParameters::addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
								mainOutput.addArgs(std::format("-filter:a {}", audioFilters));
						}

						if (outputFileToBeAdded)
						{
							// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
							// if (_videoBitRatesInfo.size() > 1)
							// {
							// 	std::string newStagingEncodedAssetPathName =
							// 		std::regex_replace(stagingTemplateEncodedAssetPathName,
							// 			std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
							// 	ffmpegArgumentList.push_back(newStagingEncodedAssetPathName);
							// }
							// else
							// ffmpegArgumentList.push_back(_encodedStagingAssetPathName);
							mainOutput.setPath(_encodedStagingAssetPathName);
						}
					}
				}
				else
				{
					for (const auto& ffmpegAudioBitRateParameter : _audioBitRatesInfo)
					{
						FFMpegEngine::Output& mainOutput = ffMpegEngine.addOutput();

						if (_audioTrackIndexToBeUsed >= 0)
						{
							// ffmpegArgumentList.push_back("-map");
							// ffmpegArgumentList.push_back(std::string("0:a:") + std::to_string(_audioTrackIndexToBeUsed));
							mainOutput.map(std::format("0:a:{}", _audioTrackIndexToBeUsed));
						}
						// ffmpegArgumentList.push_back("-threads");
						// ffmpegArgumentList.push_back("0");
						mainOutput.addArgs("-threads 0");
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						mainOutput.withAudioCodec(_ffmpegAudioCodec);
						// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(ffmpegAudioBitRateParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioOtherParameters);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioChannelsParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						mainOutput.addArgs(_ffmpegAudioSampleRateParameter);
						if (filtersRoot != nullptr)
						{
							std::string audioFilters = ffmpegFilters.addAudioFilters(filtersRoot, std::nullopt);

							if (!audioFilters.empty())
								// FFMpegEncodingParameters::addToArguments(std::string("-filter:a ") + audioFilters, ffmpegArgumentList);
								mainOutput.addArgs(std::format("-filter:a {}", audioFilters));
						}

						if (outputFileToBeAdded)
						{
							// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
							mainOutput.addArgs(_ffmpegFileFormatParameter);
							/*
							if (videoBitRatesInfo.size() > 1)
							{
								std::string newStagingEncodedAssetPathName =
									std::regex_replace(stagingTemplateEncodedAssetPathName,
										std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
								ffmpegArgumentList.push_back(newStagingEncodedAssetPathName);
							}
							else
							*/
							// ffmpegArgumentList.push_back(_encodedStagingAssetPathName);
							mainOutput.setPath(_encodedStagingAssetPathName);
						}
					}
				}
			}
		}
	}
	catch (std::exception &e)
	{
		LOG_ERROR(
			"FFMpeg: applyEncoding failed"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}"
			", e.what(): {}",
			_ingestionJobKey, _encodingJobKey, e.what()
		);

		throw;
	}
}

void FFMpegEncodingParameters::createManifestFile()
{
	try
	{
		if (!_initialized)
		{
			std::string errorMessage = std::format("FFMpegEncodingParameters is not initialized"
				", _ingestionJobKey: {}"
				", _encodingJobKey: {}", _ingestionJobKey, _encodingJobKey);
			LOG_ERROR(errorMessage);
			throw std::runtime_error(errorMessage);
		}

		std::string manifestFileName = getManifestFileName();

		// create the master playlist
		/*
			#EXTM3U
			#EXT-X-VERSION:3
			#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360
			360p.m3u8
			#EXT-X-STREAM-INF:BANDWIDTH=1400000,RESOLUTION=842x480
			480p.m3u8
			#EXT-X-STREAM-INF:BANDWIDTH=2800000,RESOLUTION=1280x720
			720p.m3u8
			#EXT-X-STREAM-INF:BANDWIDTH=5000000,RESOLUTION=1920x1080
			1080p.m3u8
		 */
		std::string endLine = "\n";
		std::string masterManifest = std::format("#EXTM3U{}#EXT-X-VERSION:3", endLine, endLine);

		for (const auto& videoBitRateInfo : _videoBitRatesInfo)
		{
			int videoBitRateInKbps = -1;
			int videoWidth = -1;
			int videoHeight = -1;

			tie(std::ignore, videoBitRateInKbps, videoWidth, videoHeight, std::ignore, std::ignore,
				std::ignore, std::ignore) = videoBitRateInfo;

			masterManifest += std::format("#EXT-X-STREAM-INF:BANDWIDTH={},RESOLUTION={}x{}{}",
				videoBitRateInKbps * 1000, videoWidth, videoHeight, endLine);

			std::string manifestRelativePathName;
			{
				manifestRelativePathName = std::format("{}/{}", _multiTrackTemplatePart, manifestFileName);
				manifestRelativePathName = StringUtils::replaceAll(manifestRelativePathName,
					_multiTrackTemplateVariable, std::to_string(videoHeight));
			}
			masterManifest += std::format ("{}{}", manifestRelativePathName, endLine);
		}

		std::string masterManifestPathFileName = std::format("{}/{}", _encodedStagingAssetPathName, manifestFileName);

		LOG_INFO(
			"Writing Master Manifest File"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}"
			", masterManifestPathFileName: {}"
			", masterManifest: {}",
			_ingestionJobKey, _encodingJobKey, masterManifestPathFileName, masterManifest
		);
		std::ofstream ofMasterManifestFile(masterManifestPathFileName);
		ofMasterManifestFile << masterManifest;
	}
	catch (std::exception &e)
	{
		LOG_ERROR(
			"FFMpeg: createManifestFile_audioGroup failed"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}"
			", exception: {}",
			_ingestionJobKey, _encodingJobKey, e.what()
		);

		throw;
	}
}

void FFMpegEncodingParameters::applyEncoding_audioGroup(
	// -1: NO two passes
	// 0: YES two passes, first step
	// 1: YES two passes, second step
	int stepNumber,

	// out (in append)
	std::vector<std::string> &ffmpegArgumentList
)
{
	try
	{
		if (!_initialized)
		{
			std::string errorMessage = std::format("FFMpegEncodingParameters is not initialized"
				", _ingestionJobKey: {}"
				", _encodingJobKey: {}", _ingestionJobKey, _encodingJobKey);
			LOG_ERROR(errorMessage);
			throw std::runtime_error(errorMessage);
		}

		/*
		 * The command will be like this:

		ffmpeg -y -i /var/mms/storage/MMSRepository/MMS_0000/ws2/000/228/001/1247989_source.mp4

			-map 0:1 -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 0 -hls_segment_filename /home/mms/tmp/ita/1247992_384637_%04d.ts -f hls
		/home/mms/tmp/ita/1247992_384637.m3u8

			-map 0:2 -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 0 -hls_segment_filename /home/mms/tmp/eng/1247992_384637_%04d.ts -f hls
		/home/mms/tmp/eng/1247992_384637.m3u8

			-map 0:0 -codec:v libx264 -profile:v high422 -b:v 800k -preset veryfast -level 4.0 -crf 22 -r 25 -vf scale=640:360 -threads 0 -hls_time 10
		-hls_list_size 0 -hls_segment_filename /home/mms/tmp/low/1247992_384637_%04d.ts -f hls /home/mms/tmp/low/1247992_384637.m3u8

		Manifest will be like:
		#EXTM3U
		#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",LANGUAGE="ita",NAME="ita",AUTOSELECT=YES, DEFAULT=YES,URI="ita/8896718_1509416.m3u8"
		#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",LANGUAGE="eng",NAME="eng",AUTOSELECT=YES, DEFAULT=YES,URI="eng/8896718_1509416.m3u8"
		#EXT-X-STREAM-INF:PROGRAM-ID=1,AUDIO="audio"
		0/8896718_1509416.m3u8


		https://developer.apple.com/documentation/http_live_streaming/example_playlists_for_http_live_streaming/adding_alternate_media_to_a_playlist#overview
		https://github.com/videojs/http-streaming/blob/master/docs/multiple-alternative-audio-tracks.md

		*/

		std::string ffmpegVideoResolutionParameter;
		int videoBitRateInKbps = -1;
		std::string ffmpegVideoBitRateParameter;
		std::string ffmpegVideoMaxRateParameter;
		std::optional<std::string> ffmpegVideoMinRateParameter;
		std::string ffmpegVideoBufSizeParameter;
		std::string ffmpegAudioBitRateParameter;

		std::tuple<std::string, int, int, int, std::string, std::string, std::optional<std::string>,
			std::string> videoBitRateInfo = _videoBitRatesInfo[0];
		tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, std::ignore, ffmpegVideoBitRateParameter,
			ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter, ffmpegVideoBufSizeParameter) = videoBitRateInfo;

		ffmpegAudioBitRateParameter = _audioBitRatesInfo[0];

		LOG_INFO(
			"Special encoding in order to allow audio/language selection by the player"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}",
			_ingestionJobKey, _encodingJobKey
		);

		// the manifestFileName naming convention is used also in EncoderVideoAudioProxy.cpp
		std::string manifestFileName = getManifestFileName();

		if (stepNumber == 0 || stepNumber == 1) // YES two passes
		{
			// used also in removeTwoPassesTemporaryFiles
			std::string prefixPasslogFileName = std::format("{}_{}.passlog", _ingestionJobKey, _encodingJobKey);
			std::string ffmpegPassLogPathFileName = std::format("{}/{}", _ffmpegTempDir, prefixPasslogFileName);

			if (stepNumber == 0) // YES two passes, first step
			{
				// It should be useless to add the audio parameters in phase 1 but,
				// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
				//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
				//  So, this is the reason, I'm adding phase 2 as well
				// + "-an "    // disable audio
				if (_audioTracksRoot != nullptr)
				{
					for (const auto& audioTrack : _audioTracksRoot)
					{
						ffmpegArgumentList.emplace_back("-map");
						ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(audioTrack, "trackIndex")));

						addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);

						addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);

						std::string audioTrackDirectoryName = JSONUtils::as<string>(audioTrack, "language", "");

						{
							std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
								_encodedStagingAssetPathName, audioTrackDirectoryName, _ingestionJobKey,
								_encodingJobKey);
							ffmpegArgumentList.emplace_back("-hls_segment_filename");
							ffmpegArgumentList.push_back(segmentPathFileName);
						}

						addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
						{
							std::string stagingManifestAssetPathName = std::format("{}/{}/{}",
								_encodedStagingAssetPathName, audioTrackDirectoryName, manifestFileName);
							ffmpegArgumentList.push_back(stagingManifestAssetPathName);
						}
					}
				}

				if (_videoTracksRoot != nullptr)
				{
					nlohmann::json videoTrack = _videoTracksRoot[0];

					ffmpegArgumentList.emplace_back("-map");
					ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex")));
				}
				addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				if (ffmpegVideoMinRateParameter)
					addToArguments(*ffmpegVideoMinRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.emplace_back("-threads");
				ffmpegArgumentList.emplace_back("0");
				ffmpegArgumentList.emplace_back("-pass");
				ffmpegArgumentList.emplace_back("1");
				ffmpegArgumentList.emplace_back("-passlogfile");
				ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
				// 2020-01-20: I removed the hls file format parameter because it was not working
				//	and added -f mp4. At the end it has to generate just the log file
				//	to be used in the second step
				// FFMpegEncodingParameters::addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);
				//
				// FFMpegEncodingParameters::addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				ffmpegArgumentList.emplace_back("-f");
				// 2020-08-21: changed from mp4 to null
				ffmpegArgumentList.emplace_back("null");

				ffmpegArgumentList.emplace_back("/dev/null");
			}
			else if (stepNumber == 1) // YES two passes, second step
			{
				// It should be useless to add the audio parameters in phase 1 but,
				// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
				//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
				//  So, this is the reason, I'm adding phase 2 as well
				// + "-an "    // disable audio
				if (_audioTracksRoot != nullptr)
				{
					for (const auto& audioTrack : _audioTracksRoot)
					{
						ffmpegArgumentList.emplace_back("-map");
						ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(audioTrack, "trackIndex")));

						addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);

						addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);

						std::string audioTrackDirectoryName = JSONUtils::as<string>(audioTrack, "language", "");

						{
							std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
								_encodedStagingAssetPathName, audioTrackDirectoryName, _ingestionJobKey,
								_encodingJobKey);
							ffmpegArgumentList.emplace_back("-hls_segment_filename");
							ffmpegArgumentList.push_back(segmentPathFileName);
						}

						addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
						{
							std::string stagingManifestAssetPathName = std::format("{}/{}/{}",
								_encodedStagingAssetPathName, audioTrackDirectoryName, manifestFileName);
							ffmpegArgumentList.push_back(stagingManifestAssetPathName);
						}
					}
				}

				if (_videoTracksRoot != nullptr)
				{
					nlohmann::json videoTrack = _videoTracksRoot[0];

					ffmpegArgumentList.emplace_back("-map");
					ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex")));
				}
				addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				if (ffmpegVideoMinRateParameter)
					addToArguments(*ffmpegVideoMinRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.emplace_back("-threads");
				ffmpegArgumentList.emplace_back("0");
				ffmpegArgumentList.emplace_back("-pass");
				ffmpegArgumentList.emplace_back("2");
				ffmpegArgumentList.emplace_back("-passlogfile");
				ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);

				addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);

				std::string videoTrackDirectoryName;
				if (_videoTracksRoot != nullptr)
				{
					nlohmann::json videoTrack = _videoTracksRoot[0];

					videoTrackDirectoryName = std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex"));
				}

				{
					std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
						_encodedStagingAssetPathName, videoTrackDirectoryName, _ingestionJobKey,
						_encodingJobKey);
					ffmpegArgumentList.emplace_back("-hls_segment_filename");
					ffmpegArgumentList.push_back(segmentPathFileName);
				}

				addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
				{
					std::string stagingManifestAssetPathName = std::format("{}/{}/{}",
						_encodedStagingAssetPathName, videoTrackDirectoryName, manifestFileName);
					ffmpegArgumentList.push_back(stagingManifestAssetPathName);
				}
			}
		}
		else
		{
			// It should be useless to add the audio parameters in phase 1 but,
			// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
			//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
			//  So, this is the reason, I'm adding phase 2 as well
			// + "-an "    // disable audio
			if (_audioTracksRoot != nullptr)
			{
				for (const auto& audioTrack : _audioTracksRoot)
				{
					ffmpegArgumentList.emplace_back("-map");
					ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(audioTrack, "trackIndex")));

					addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
					addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
					addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
					addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);

					addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);

					std::string audioTrackDirectoryName = JSONUtils::as<string>(audioTrack, "language", "");

					{
						std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
							_encodedStagingAssetPathName, audioTrackDirectoryName, _ingestionJobKey,
							_encodingJobKey);
						ffmpegArgumentList.emplace_back("-hls_segment_filename");
						ffmpegArgumentList.push_back(segmentPathFileName);
					}

					addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
					{
						std::string stagingManifestAssetPathName = std::format("{}/{}/{}",
							_encodedStagingAssetPathName, audioTrackDirectoryName, manifestFileName);
						ffmpegArgumentList.push_back(stagingManifestAssetPathName);
					}
				}
			}

			if (_videoTracksRoot != nullptr)
			{
				nlohmann::json videoTrack = _videoTracksRoot[0];

				ffmpegArgumentList.emplace_back("-map");
				ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex")));
			}
			addToArguments(_ffmpegVideoCodecParameter, ffmpegArgumentList);
			addToArguments(_ffmpegVideoProfileParameter, ffmpegArgumentList);
			addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
			addToArguments(_ffmpegVideoOtherParameters, ffmpegArgumentList);
			addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
			if (ffmpegVideoMinRateParameter)
				addToArguments(*ffmpegVideoMinRateParameter, ffmpegArgumentList);
			addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
			addToArguments(_ffmpegVideoFrameRateParameter, ffmpegArgumentList);
			addToArguments(_ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
			addToArguments(std::string("-vf ") + ffmpegVideoResolutionParameter, ffmpegArgumentList);
			ffmpegArgumentList.emplace_back("-threads");
			ffmpegArgumentList.emplace_back("0");

			addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);

			std::string videoTrackDirectoryName;
			if (_videoTracksRoot != nullptr)
			{
				nlohmann::json videoTrack = _videoTracksRoot[0];

				videoTrackDirectoryName = std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex"));
			}

			{
				std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
					_encodedStagingAssetPathName, videoTrackDirectoryName, _ingestionJobKey, _encodingJobKey);
				ffmpegArgumentList.emplace_back("-hls_segment_filename");
				ffmpegArgumentList.push_back(segmentPathFileName);
			}

			addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
			{
				std::string stagingManifestAssetPathName = std::format("{}/{}/{}",
					_encodedStagingAssetPathName, videoTrackDirectoryName, manifestFileName);
				ffmpegArgumentList.push_back(stagingManifestAssetPathName);
			}
		}
	}
	catch (std::exception &e)
	{
		LOG_ERROR(
			"FFMpeg: applyEncoding_audioGroup failed"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}"
			", exception: {}",
			_ingestionJobKey, _encodingJobKey, e.what()
		);

		throw;
	}
}

void FFMpegEncodingParameters::applyEncoding_audioGroup(
	// -1: NO two passes
	// 0: YES two passes, first step
	// 1: YES two passes, second step
	int stepNumber,

	// out (in append)
	FFMpegEngine& ffMpegEngine
)
{
	try
	{
		if (!_initialized)
		{
			std::string errorMessage = std::format("FFMpegEncodingParameters is not initialized"
				", _ingestionJobKey: {}"
				", _encodingJobKey: {}", _ingestionJobKey, _encodingJobKey);
			LOG_ERROR(errorMessage);
			throw std::runtime_error(errorMessage);
		}

		/*
		 * The command will be like this:

		ffmpeg -y -i /var/mms/storage/MMSRepository/MMS_0000/ws2/000/228/001/1247989_source.mp4

			-map 0:1 -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 0 -hls_segment_filename /home/mms/tmp/ita/1247992_384637_%04d.ts -f hls
		/home/mms/tmp/ita/1247992_384637.m3u8

			-map 0:2 -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 0 -hls_segment_filename /home/mms/tmp/eng/1247992_384637_%04d.ts -f hls
		/home/mms/tmp/eng/1247992_384637.m3u8

			-map 0:0 -codec:v libx264 -profile:v high422 -b:v 800k -preset veryfast -level 4.0 -crf 22 -r 25 -vf scale=640:360 -threads 0 -hls_time 10
		-hls_list_size 0 -hls_segment_filename /home/mms/tmp/low/1247992_384637_%04d.ts -f hls /home/mms/tmp/low/1247992_384637.m3u8

		Manifest will be like:
		#EXTM3U
		#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",LANGUAGE="ita",NAME="ita",AUTOSELECT=YES, DEFAULT=YES,URI="ita/8896718_1509416.m3u8"
		#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",LANGUAGE="eng",NAME="eng",AUTOSELECT=YES, DEFAULT=YES,URI="eng/8896718_1509416.m3u8"
		#EXT-X-STREAM-INF:PROGRAM-ID=1,AUDIO="audio"
		0/8896718_1509416.m3u8


		https://developer.apple.com/documentation/http_live_streaming/example_playlists_for_http_live_streaming/adding_alternate_media_to_a_playlist#overview
		https://github.com/videojs/http-streaming/blob/master/docs/multiple-alternative-audio-tracks.md

		*/

		std::string ffmpegVideoResolutionParameter;
		int videoBitRateInKbps = -1;
		std::string ffmpegVideoBitRateParameter;
		std::string ffmpegVideoMaxRateParameter;
		std::optional<std::string> ffmpegVideoMinRateParameter;
		std::string ffmpegVideoBufSizeParameter;
		std::string ffmpegAudioBitRateParameter;

		std::tuple<std::string, int, int, int, std::string, std::string, std::optional<std::string>,
			std::string> videoBitRateInfo = _videoBitRatesInfo[0];
		tie(ffmpegVideoResolutionParameter, videoBitRateInKbps, std::ignore, std::ignore, ffmpegVideoBitRateParameter,
			ffmpegVideoMaxRateParameter, ffmpegVideoMinRateParameter, ffmpegVideoBufSizeParameter) = videoBitRateInfo;

		ffmpegAudioBitRateParameter = _audioBitRatesInfo[0];

		LOG_INFO(
			"Special encoding in order to allow audio/language selection by the player"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}",
			_ingestionJobKey, _encodingJobKey
		);

		// the manifestFileName naming convention is used also in EncoderVideoAudioProxy.cpp
		std::string manifestFileName = getManifestFileName();

		if (stepNumber == 0 || stepNumber == 1) // YES two passes
		{
			// used also in removeTwoPassesTemporaryFiles
			std::string prefixPasslogFileName = std::format("{}_{}.passlog", _ingestionJobKey, _encodingJobKey);
			std::string ffmpegPassLogPathFileName = std::format("{}/{}", _ffmpegTempDir, prefixPasslogFileName);

			if (stepNumber == 0) // YES two passes, first step
			{
				// It should be useless to add the audio parameters in phase 1 but,
				// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
				//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
				//  So, this is the reason, I'm adding phase 2 as well
				// + "-an "    // disable audio
				if (_audioTracksRoot != nullptr)
				{
					for (const auto& audioTrack : _audioTracksRoot)
					{
						FFMpegEngine::Output audioOutput = ffMpegEngine.addOutput();

						// ffmpegArgumentList.push_back("-map");
						// ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(audioTrack, "trackIndex")));
						audioOutput.map(std::format("0:{}", JSONUtils::as<int32_t>(audioTrack, "trackIndex")));

						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						audioOutput.withAudioCodec(_ffmpegAudioCodec);
						// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						audioOutput.addArgs(ffmpegAudioBitRateParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						audioOutput.addArgs(_ffmpegAudioOtherParameters);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						audioOutput.addArgs(_ffmpegAudioChannelsParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						audioOutput.addArgs(_ffmpegAudioSampleRateParameter);

						// FFMpegEncodingParameters::addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);
						audioOutput.addArgs(_ffmpegHttpStreamingParameter);

						std::string audioTrackDirectoryName = JSONUtils::as<string>(audioTrack, "language", "");

						{
							std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
								_encodedStagingAssetPathName, audioTrackDirectoryName, _ingestionJobKey,
								_encodingJobKey);
							// ffmpegArgumentList.push_back("-hls_segment_filename");
							// ffmpegArgumentList.push_back(segmentPathFileName);
							audioOutput.addArg("-hls_segment_filename");
							audioOutput.addArg(segmentPathFileName);
						}

						// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
						audioOutput.addArgs(_ffmpegFileFormatParameter);
						{
							std::string stagingManifestAssetPathName = std::format("{}/{}/{}",
								_encodedStagingAssetPathName, audioTrackDirectoryName, manifestFileName);
							// ffmpegArgumentList.push_back(stagingManifestAssetPathName);
							audioOutput.setPath(stagingManifestAssetPathName);
						}
					}
				}

				FFMpegEngine::Output videoOutput = ffMpegEngine.addOutput();
				if (_videoTracksRoot != nullptr)
				{
					nlohmann::json videoTrack = _videoTracksRoot[0];

					// ffmpegArgumentList.push_back("-map");
					// ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex")));
					videoOutput.map(std::format("0:{}", JSONUtils::as<int32_t>(videoTrack, "trackIndex")));
				}
				videoOutput.withVideoCodec(_ffmpegVideoCodec);
				videoOutput.addArgs(_ffmpegVideoProfileParameter);
				videoOutput.addArgs(ffmpegVideoBitRateParameter);
				videoOutput.addArgs(_ffmpegVideoOtherParameters);
				videoOutput.addArgs(ffmpegVideoMaxRateParameter);
				if (ffmpegVideoMinRateParameter)
					videoOutput.addArgs(*ffmpegVideoMinRateParameter);
				videoOutput.addArgs(ffmpegVideoBufSizeParameter);
				videoOutput.addArgs(_ffmpegVideoFrameRateParameter);
				videoOutput.addArgs(_ffmpegVideoKeyFramesRateParameter);
				videoOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
				// ffmpegArgumentList.push_back("-threads");
				// ffmpegArgumentList.push_back("0");
				// ffmpegArgumentList.push_back("-pass");
				// ffmpegArgumentList.push_back("1");
				// ffmpegArgumentList.push_back("-passlogfile");
				// ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
				videoOutput.addArgs("-threads 0 -pass 1 -passlogfile");
				videoOutput.addArg(ffmpegPassLogPathFileName);
				// 2020-01-20: I removed the hls file format parameter because it was not working
				//	and added -f mp4. At the end it has to generate just the log file
				//	to be used in the second step
				// FFMpegEncodingParameters::addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);
				//
				// FFMpegEncodingParameters::addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				// ffmpegArgumentList.push_back("-f");
				// 2020-08-21: changed from mp4 to null
				// ffmpegArgumentList.push_back("null");
				videoOutput.addArgs("-f null");

				// ffmpegArgumentList.push_back("/dev/null");
				videoOutput.setPath("/dev/null");
			}
			else if (stepNumber == 1) // YES two passes, second step
			{
				// It should be useless to add the audio parameters in phase 1 but,
				// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
				//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
				//  So, this is the reason, I'm adding phase 2 as well
				// + "-an "    // disable audio
				if (_audioTracksRoot != nullptr)
				{
					for (const auto& audioTrack : _audioTracksRoot)
					{
						FFMpegEngine::Output audioOutput = ffMpegEngine.addOutput();
						// ffmpegArgumentList.push_back("-map");
						// ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(audioTrack, "trackIndex")));
						audioOutput.map(std::format("0:{}", JSONUtils::as<int32_t>(audioTrack, "trackIndex")));

						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
						audioOutput.withAudioCodec(_ffmpegAudioCodec);
						// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
						audioOutput.addArgs(ffmpegAudioBitRateParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
						audioOutput.addArgs(_ffmpegAudioOtherParameters);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
						audioOutput.addArgs(_ffmpegAudioChannelsParameter);
						// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
						audioOutput.addArgs(_ffmpegAudioSampleRateParameter);

						// FFMpegEncodingParameters::addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);
						audioOutput.addArgs(_ffmpegHttpStreamingParameter);

						std::string audioTrackDirectoryName = JSONUtils::as<string>(audioTrack, "language", "");

						{
							std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
								_encodedStagingAssetPathName, audioTrackDirectoryName, _ingestionJobKey,
								_encodingJobKey);
							// ffmpegArgumentList.push_back("-hls_segment_filename");
							// ffmpegArgumentList.push_back(segmentPathFileName);
							audioOutput.addArg("-hls_segment_filename");
							audioOutput.addArg(segmentPathFileName);
						}

						// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
						audioOutput.addArgs(_httpStreamingFileFormat);
						{
							std::string stagingManifestAssetPathName = std::format("{}/{}/{}",
								_encodedStagingAssetPathName, audioTrackDirectoryName, manifestFileName);
							// ffmpegArgumentList.push_back(stagingManifestAssetPathName);
							audioOutput.setPath(stagingManifestAssetPathName);
						}
					}
				}

				FFMpegEngine::Output videoOutput = ffMpegEngine.addOutput();

				if (_videoTracksRoot != nullptr)
				{
					nlohmann::json videoTrack = _videoTracksRoot[0];

					// ffmpegArgumentList.push_back("-map");
					// ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex")));
					videoOutput.map(std::format("0:{}", JSONUtils::as<int32_t>(videoTrack, "trackIndex")));
				}
				videoOutput.withVideoCodec(_ffmpegVideoCodec);
				videoOutput.addArgs(_ffmpegVideoProfileParameter);
				videoOutput.addArgs(ffmpegVideoBitRateParameter);
				videoOutput.addArgs(_ffmpegVideoOtherParameters);
				videoOutput.addArgs(ffmpegVideoMaxRateParameter);
				if (ffmpegVideoMinRateParameter)
					videoOutput.addArgs(*ffmpegVideoMinRateParameter);
				videoOutput.addArgs(ffmpegVideoBufSizeParameter);
				videoOutput.addArgs(_ffmpegVideoFrameRateParameter);
				videoOutput.addArgs(_ffmpegVideoKeyFramesRateParameter);
				videoOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
				// ffmpegArgumentList.push_back("-threads");
				// ffmpegArgumentList.push_back("0");
				// ffmpegArgumentList.push_back("-pass");
				// ffmpegArgumentList.push_back("2");
				// ffmpegArgumentList.push_back("-passlogfile");
				// ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
				videoOutput.addArgs("-threads 0 -pass 2 -passlogfile");
				videoOutput.addArg(ffmpegPassLogPathFileName);

				// FFMpegEncodingParameters::addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);
				videoOutput.addArgs(_ffmpegHttpStreamingParameter);

				std::string videoTrackDirectoryName;
				if (_videoTracksRoot != nullptr)
				{
					nlohmann::json videoTrack = _videoTracksRoot[0];

					videoTrackDirectoryName = std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex"));
				}

				{
					std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
								_encodedStagingAssetPathName, videoTrackDirectoryName, _ingestionJobKey,
								_encodingJobKey);
					// ffmpegArgumentList.push_back("-hls_segment_filename");
					// ffmpegArgumentList.push_back(segmentPathFileName);
					videoOutput.addArg("-hls_segment_filename");
					videoOutput.addArg(segmentPathFileName);
				}

				// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
				videoOutput.addArgs(_ffmpegFileFormatParameter);
				{
					std::string stagingManifestAssetPathName = std::format("{}/{}/{}",
						_encodedStagingAssetPathName, videoTrackDirectoryName, manifestFileName);
					// ffmpegArgumentList.push_back(stagingManifestAssetPathName);
					videoOutput.setPath(stagingManifestAssetPathName);
				}
			}
		}
		else
		{
			// It should be useless to add the audio parameters in phase 1 but,
			// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
			//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
			//  So, this is the reason, I'm adding phase 2 as well
			// + "-an "    // disable audio
			if (_audioTracksRoot != nullptr)
			{
				for (const auto& audioTrack : _audioTracksRoot)
				{
					FFMpegEngine::Output audioOutput = ffMpegEngine.addOutput();

					// ffmpegArgumentList.push_back("-map");
					// ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(audioTrack, "trackIndex")));
					audioOutput.map(std::format("0:{}", JSONUtils::as<int32_t>(audioTrack, "trackIndex")));

					// FFMpegEncodingParameters::addToArguments(_ffmpegAudioCodecParameter, ffmpegArgumentList);
					audioOutput.withAudioCodec(_ffmpegAudioCodec);
					// FFMpegEncodingParameters::addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
					audioOutput.addArgs(ffmpegAudioBitRateParameter);
					// FFMpegEncodingParameters::addToArguments(_ffmpegAudioOtherParameters, ffmpegArgumentList);
					audioOutput.addArgs(_ffmpegAudioOtherParameters);
					// FFMpegEncodingParameters::addToArguments(_ffmpegAudioChannelsParameter, ffmpegArgumentList);
					audioOutput.addArgs(_ffmpegAudioChannelsParameter);
					// FFMpegEncodingParameters::addToArguments(_ffmpegAudioSampleRateParameter, ffmpegArgumentList);
					audioOutput.addArgs(_ffmpegAudioSampleRateParameter);

					// FFMpegEncodingParameters::addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);
					audioOutput.addArgs(_ffmpegHttpStreamingParameter);

					std::string audioTrackDirectoryName = JSONUtils::as<string>(audioTrack, "language", "");

					{
						std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
							_encodedStagingAssetPathName, audioTrackDirectoryName, _ingestionJobKey,
							_encodingJobKey);
						// ffmpegArgumentList.push_back("-hls_segment_filename");
						// ffmpegArgumentList.push_back(segmentPathFileName);
						audioOutput.addArgs("-hls_segment_filename");
						audioOutput.addArg(segmentPathFileName);
					}

					// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
					audioOutput.addArgs(_ffmpegFileFormatParameter);
					{
						std::string stagingManifestAssetPathName = _encodedStagingAssetPathName + "/" + audioTrackDirectoryName + "/" + manifestFileName;
						// ffmpegArgumentList.push_back(stagingManifestAssetPathName);
						audioOutput.setPath(stagingManifestAssetPathName);
					}
				}
			}

			FFMpegEngine::Output videoOutput = ffMpegEngine.addOutput();

			if (_videoTracksRoot != nullptr)
			{
				nlohmann::json videoTrack = _videoTracksRoot[0];

				// ffmpegArgumentList.push_back("-map");
				// ffmpegArgumentList.push_back(std::string("0:") + std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex")));
				videoOutput.map(std::format("0:{}", JSONUtils::as<int32_t>(videoTrack, "trackIndex")));
			}
			videoOutput.withVideoCodec(_ffmpegVideoCodec);
			videoOutput.addArgs(_ffmpegVideoProfileParameter);
			videoOutput.addArgs(ffmpegVideoBitRateParameter);
			videoOutput.addArgs(_ffmpegVideoOtherParameters);
			videoOutput.addArgs(ffmpegVideoMaxRateParameter);
			if (ffmpegVideoMinRateParameter)
				videoOutput.addArgs(*ffmpegVideoMinRateParameter);
			videoOutput.addArgs(ffmpegVideoBufSizeParameter);
			videoOutput.addArgs(_ffmpegVideoFrameRateParameter);
			videoOutput.addArgs(_ffmpegVideoKeyFramesRateParameter);
			videoOutput.addArgs(std::format("-vf {}", ffmpegVideoResolutionParameter));
			// ffmpegArgumentList.push_back("-threads");
			// ffmpegArgumentList.push_back("0");
			videoOutput.addArgs("-threads 0");

			// FFMpegEncodingParameters::addToArguments(_ffmpegHttpStreamingParameter, ffmpegArgumentList);
			videoOutput.addArgs(_ffmpegHttpStreamingParameter);

			std::string videoTrackDirectoryName;
			if (_videoTracksRoot != nullptr)
			{
				nlohmann::json videoTrack = _videoTracksRoot[0];

				videoTrackDirectoryName = std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex"));
			}

			{
				std::string segmentPathFileName = std::format("{}/{}/{}_{}_%04d.ts",
							_encodedStagingAssetPathName, videoTrackDirectoryName, _ingestionJobKey,
							_encodingJobKey);
				// ffmpegArgumentList.push_back("-hls_segment_filename");
				// ffmpegArgumentList.push_back(segmentPathFileName);
				videoOutput.addArg("-hls_segment_filename");
				videoOutput.addArg(segmentPathFileName);
			}

			// FFMpegEncodingParameters::addToArguments(_ffmpegFileFormatParameter, ffmpegArgumentList);
			videoOutput.addArgs(_ffmpegFileFormatParameter);
			{
				std::string stagingManifestAssetPathName = _encodedStagingAssetPathName + "/" + videoTrackDirectoryName + "/" + manifestFileName;
				// ffmpegArgumentList.push_back(stagingManifestAssetPathName);
				videoOutput.setPath(stagingManifestAssetPathName);
			}
		}
	}
	catch (std::exception &e)
	{
		LOG_ERROR(
			"FFMpeg: applyEncoding_audioGroup failed"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}"
			", e.what(): {}",
			_ingestionJobKey, _encodingJobKey, e.what()
		);

		throw;
	}
}

void FFMpegEncodingParameters::createManifestFile_audioGroup()
{
	try
	{
		if (!_initialized)
		{
			std::string errorMessage = std::format("FFMpegEncodingParameters is not initialized"
				", _ingestionJobKey: {}"
				", _encodingJobKey: {}", _ingestionJobKey, _encodingJobKey);
			LOG_ERROR(errorMessage);
			throw std::runtime_error(errorMessage);
		}

		std::string manifestFileName = getManifestFileName();

		std::string mainManifestPathName = std::format("{}/{}", _encodedStagingAssetPathName, manifestFileName);

		std::string mainManifest;

		mainManifest = std::format("#EXTM3U{}", "\n");

		if (_audioTracksRoot != nullptr)
		{
			for (const auto& audioTrack : _audioTracksRoot)
			{
				std::string audioTrackDirectoryName = JSONUtils::as<string>(audioTrack, "language", "");

				std::string audioLanguage = JSONUtils::as<string>(audioTrack, "language", "");

				// std::string audioManifestLine = "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",LANGUAGE=\"" + audioLanguage + "\",NAME=\"" + audioLanguage +
				//	"\",AUTOSELECT=YES, DEFAULT=YES,URI=\"" + audioTrackDirectoryName + "/" + manifestFileName + "\"";
				std::string audioManifestLine = std::format("#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",LANGUAGE=\"{}"
					"\",NAME=\"{}\",AUTOSELECT=YES, DEFAULT=YES,URI=\"{}/{}\"",
					audioLanguage, audioLanguage, audioTrackDirectoryName, manifestFileName);

				mainManifest += std::format("{}\n", audioManifestLine);
			}
		}

		std::string videoManifestLine = "#EXT-X-STREAM-INF:PROGRAM-ID=1,AUDIO=\"audio\"";
		mainManifest += std::format("{}\n", videoManifestLine);

		std::string videoTrackDirectoryName;
		if (_videoTracksRoot != nullptr)
		{
			nlohmann::json videoTrack = _videoTracksRoot[0];

			videoTrackDirectoryName = std::to_string(JSONUtils::as<int32_t>(videoTrack, "trackIndex"));
		}
		mainManifest += std::format("{}/{}\n", videoTrackDirectoryName, manifestFileName);

		std::ofstream manifestFile(mainManifestPathName);
		manifestFile << mainManifest;
	}
	catch (std::exception &e)
	{
		LOG_ERROR(
			"FFMpeg: createManifestFile_audioGroup failed"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}"
			", exception: {}",
			_ingestionJobKey, _encodingJobKey, e.what()
		);

		throw;
	}
}

std::string FFMpegEncodingParameters::getManifestFileName()
{
	try
	{
		if (!_initialized)
		{
			std::string errorMessage = std::format("FFMpegEncodingParameters is not initialized"
				", _ingestionJobKey: {}"
				", _encodingJobKey: {}", _ingestionJobKey, _encodingJobKey);
			LOG_ERROR(errorMessage);
			throw std::runtime_error(errorMessage);
		}

		std::string manifestFileName = std::format("{}_{}", _ingestionJobKey, _encodingJobKey);
		if (_httpStreamingFileFormat == "hls")
			manifestFileName += ".m3u8";
		else // if (_httpStreamingFileFormat == "dash")
			manifestFileName += ".mpd";

		return manifestFileName;
	}
	catch (std::exception &e)
	{
		LOG_ERROR(
			"FFMpeg: createManifestFile_audioGroup failed"
			", _ingestionJobKey: {}"
			", _encodingJobKey: {}"
			", exception: {}",
			_ingestionJobKey, _encodingJobKey, e.what()
		);

		throw;
	}
}

void FFMpegEncodingParameters::settingFfmpegParameters(
	nlohmann::json encodingProfileDetailsRoot,
	bool isVideo, // if false it means is audio

	std::string &httpStreamingFileFormat, std::string &ffmpegHttpStreamingParameter,

	std::string &ffmpegFileFormatParameter,

	std::string &ffmpegVideoCodecParameter, std::string &ffmpegVideoCodec, std::string &ffmpegVideoProfileParameter, std::string &ffmpegVideoOtherParameters,
	bool &twoPasses, std::string &ffmpegVideoFrameRateParameter, std::string &ffmpegVideoKeyFramesRateParameter,
	std::vector<std::tuple<std::string, int, int, int, std::string, std::string, std::optional<std::string>, std::string>> &videoBitRatesInfo,

	std::string &ffmpegAudioCodecParameter, std::string &ffmpegAudioCodec, std::string &ffmpegAudioOtherParameters, std::string &ffmpegAudioChannelsParameter,
	std::string &ffmpegAudioSampleRateParameter, std::vector<std::string> &audioBitRatesInfo
)
{
	std::string field;

	{
		std::string sEncodingProfileDetailsRoot = JSONUtils::toString(encodingProfileDetailsRoot);

		LOG_INFO(
			"settingFfmpegParameters"
			", sEncodingProfileDetailsRoot: {}",
			sEncodingProfileDetailsRoot
		);
	}

	// fileFormat
	{
		std::string fileFormatLowerCase;
		std::string fileFormat;

		field = "fileFormat";
		if (!JSONUtils::isPresent(encodingProfileDetailsRoot, field, true))
		{
			std::string errorMessage = std::format(
				"FFMpeg: Field is not present or it is null"
				", Field: {}",
				field
			);
			LOG_ERROR(errorMessage);

			throw std::runtime_error(errorMessage);
		}
		fileFormat = JSONUtils::as<string>(encodingProfileDetailsRoot, field, "");
		fileFormatLowerCase = StringUtils::lowerCase(fileFormat);

		encodingFileFormatValidation(fileFormat);

		if (fileFormatLowerCase == "hls")
		{
			httpStreamingFileFormat = "hls";

			ffmpegFileFormatParameter = "-f hls ";

			long segmentDurationInSeconds = 10;

			field = "HLS";
			if (JSONUtils::isPresent(encodingProfileDetailsRoot, field, true))
			{
				nlohmann::json hlsRoot = encodingProfileDetailsRoot[field];

				field = "segmentDuration";
				segmentDurationInSeconds = JSONUtils::as<int32_t>(hlsRoot, field, 10);
			}

			ffmpegHttpStreamingParameter = std::format("-hls_time {} ", segmentDurationInSeconds);

			// hls_list_size: set the maximum number of playlist entries. If set to 0 the list file
			//	will contain all the segments. Default value is 5.
			ffmpegHttpStreamingParameter += "-hls_list_size 0 ";
		}
		else if (fileFormatLowerCase == "dash")
		{
			httpStreamingFileFormat = "dash";

			ffmpegFileFormatParameter = "-f dash ";

			long segmentDurationInSeconds = 10;

			field = "DASH";
			if (JSONUtils::isPresent(encodingProfileDetailsRoot, field, true))
			{
				nlohmann::json dashRoot = encodingProfileDetailsRoot[field];

				field = "segmentDuration";
				segmentDurationInSeconds = JSONUtils::as<int32_t>(dashRoot, field, 10);
			}
			ffmpegHttpStreamingParameter = std::format("-seg_duration {} ", segmentDurationInSeconds);

			// hls_list_size: set the maximum number of playlist entries. If set to 0 the list file
			//	will contain all the segments. Default value is 5.
			// ffmpegHttpStreamingParameter += "-hls_list_size 0 ";

			// it is important to specify -init_seg_name because those files
			// will not be removed in EncoderVideoAudioProxy.cpp
			ffmpegHttpStreamingParameter += "-init_seg_name init-stream$RepresentationID$.$ext$ ";

			// the only difference with the ffmpeg default is that default is $Number%05d$
			// We had to change it to $Number%01d$ because otherwise the generated file containing
			// 00001 00002 ... but the videojs player generates file name like 1 2 ...
			// and the streaming was not working
			ffmpegHttpStreamingParameter += "-media_seg_name chunk-stream$RepresentationID$-$Number%01d$.$ext$ ";
		}
		else
		{
			httpStreamingFileFormat = "";

			if (fileFormatLowerCase == "ts" || fileFormatLowerCase == "mts")
			{
				// if "-f ts filename.ts" is added the following error happens:
				//		...Requested output format 'ts' is not a suitable output format
				// Without "-f ts", just filename.ts works fine
				// Same for mts
				ffmpegFileFormatParameter = "";
			}
			else
			{
				ffmpegFileFormatParameter = std::format(" -f {} ", fileFormatLowerCase);
			}
		}
	}

	if (isVideo)
	{
		field = "video";
		if (JSONUtils::isPresent(encodingProfileDetailsRoot, field, true))
		{
			nlohmann::json videoRoot = encodingProfileDetailsRoot[field];

			// codec
			{
				field = "codec";
				if (!JSONUtils::isPresent(videoRoot, field, true))
				{
					std::string errorMessage = std::format(
						"FFMpeg: Field is not present or it is null"
						", Field: {}",
						field
					);
					LOG_ERROR(errorMessage);

					throw std::runtime_error(errorMessage);
				}

				ffmpegVideoCodec = JSONUtils::as<string>(videoRoot, field, "");

				// 2020-03-27: commented just to avoid to add the check every time a new codec is added
				//		In case the codec is wrong, ffmpeg will generate the error later
				// FFMpeg::encodingVideoCodecValidation(codec, logger);

				ffmpegVideoCodecParameter = std::format("-codec:v {} ", ffmpegVideoCodec);
			}

			// profile
			{
				field = "profile";
				if (JSONUtils::isPresent(videoRoot, field, true))
				{
					std::string profile = JSONUtils::as<string>(videoRoot, field, "");

					if (ffmpegVideoCodec == "libx264" || ffmpegVideoCodec == "libvpx"
						|| ffmpegVideoCodec == "mpeg4" || ffmpegVideoCodec == "xvid")
					{
						encodingVideoProfileValidation(ffmpegVideoCodec, profile);
						if (ffmpegVideoCodec == "libx264" && !profile.empty())
							ffmpegVideoProfileParameter = std::format("-profile:v {} ", profile);
						else if (ffmpegVideoCodec == "libvpx" && !profile.empty())
							ffmpegVideoProfileParameter = std::format("-quality {} ", profile);
						else if ((ffmpegVideoCodec == "mpeg4" || ffmpegVideoCodec == "xvid") && !profile.empty())
							ffmpegVideoProfileParameter = std::format("-qscale:v {} ", profile);
					}
					else if (!profile.empty())
						ffmpegVideoProfileParameter = std::format("-profile:v {} ", profile);
				}
			}

			// OtherOutputParameters
			{
				field = "otherOutputParameters";
				if (JSONUtils::isPresent(videoRoot, field, true))
				{
					std::string otherOutputParameters = JSONUtils::as<string>(videoRoot, field, "");

					ffmpegVideoOtherParameters = otherOutputParameters + " ";
				}
			}

			// twoPasses
			{
				field = "twoPasses";
				if (!JSONUtils::isPresent(videoRoot, field, true))
				{
					std::string errorMessage = std::format(
						"FFMpeg: Field is not present or it is null"
						", Field: {}",
						field
					);
					LOG_ERROR(errorMessage);

					throw std::runtime_error(errorMessage);
				}
				twoPasses = JSONUtils::as<bool>(videoRoot, field, false);
			}

			// frameRate
			{
				field = "frameRate";
				if (JSONUtils::isPresent(videoRoot, field, true))
				{
					int frameRate = JSONUtils::as<int32_t>(videoRoot, field, 0);

					if (frameRate != 0)
					{
						ffmpegVideoFrameRateParameter = std::format("-r {} ", frameRate);

						// keyFrameIntervalInSeconds
						{
							/*
								Un tipico codec video utilizza la compressione temporale, ovvero la maggior parte
								dei fotogrammi memorizza solo la differenza rispetto ai fotogrammi precedenti
								(e in alcuni casi futuri). Quindi, per decodificare questi fotogrammi, è necessario
								fare riferimento ai fotogrammi precedenti, al fine di generare un'immagine completa.
								In breve, i fotogrammi chiave sono fotogrammi che non si basano su altri fotogrammi
								per la decodifica e su cui si basano altri fotogrammi per essere decodificati.

								Se un video deve essere tagliato o segmentato, senza transcodifica (ricompressione),
								la segmentazione può avvenire solo in corrispondenza dei fotogrammi chiave, in modo
								che il primo fotogramma di un segmento sia un fotogramma chiave. Se così non fosse,
								i fotogrammi di un segmento fino al fotogramma chiave successivo non potrebbero
								essere riprodotti.

								Un codificatore come x264 in genere genera fotogrammi chiave solo se rileva che si è verificato
								un cambio di scena*. Ciò non favorisce la segmentazione, poiché i fotogrammi chiave
								possono essere generati a intervalli irregolari. Per garantire la creazione di segmenti
								di lunghezze identiche e prevedibili, è possibile utilizzare l'opzione force_key_frames
								per garantire il posizionamento desiderato dei fotogrammi chiave.
							*/

							field = "keyFrameIntervalInSeconds";
							if (JSONUtils::isPresent(videoRoot, field, true))
							{
								int keyFrameIntervalInSeconds = JSONUtils::as<int32_t>(videoRoot, field, 5);

								field = "forceKeyFrames";
								bool forceKeyFrames = JSONUtils::as<bool>(videoRoot, field, false);

								// -g specifies the number of frames in a GOP
								if (forceKeyFrames)
									ffmpegVideoKeyFramesRateParameter = std::format(
										"-force_key_frames expr:gte(t,n_forced*{}) ", keyFrameIntervalInSeconds);
								else
									ffmpegVideoKeyFramesRateParameter = std::format("-g {} ",
										frameRate * keyFrameIntervalInSeconds);
							}
						}
					}
				}
			}

			field = "bitRates";
			if (!JSONUtils::isPresent(videoRoot, field, true))
			{
				std::string errorMessage = std::format(
					"FFMpeg: Field is not present or it is null"
					", Field: {}",
					field
				);
				LOG_ERROR(errorMessage);

				throw std::runtime_error(errorMessage);
			}
			nlohmann::json bitRatesRoot = videoRoot[field];

			videoBitRatesInfo.clear();
			{
				for (const auto& bitRateInfo : bitRatesRoot)
				{
					// resolution
					std::string ffmpegVideoResolution;
					int videoWidth;
					int videoHeight;
					{
						field = "width";
						if (!JSONUtils::isPresent(bitRateInfo, field, true))
						{
							std::string errorMessage = std::format(
								"FFMpeg: Field is not present or it is null"
								", Field: {}",
								field
							);
							LOG_ERROR(errorMessage);

							throw std::runtime_error(errorMessage);
						}
						videoWidth = JSONUtils::as<int32_t>(bitRateInfo, field, 0);
						if (videoWidth == -1 && ffmpegVideoCodec == "libx264")
							videoWidth = -2; // h264 requires always a even width/height

						field = "height";
						if (!JSONUtils::isPresent(bitRateInfo, field, true))
						{
							std::string errorMessage = std::format(
								"FFMpeg: Field is not present or it is null"
								", Field: {}",
								field
							);
							LOG_ERROR(errorMessage);

							throw std::runtime_error(errorMessage);
						}
						videoHeight = JSONUtils::as<int32_t>(bitRateInfo, field, 0);
						if (videoHeight == -1 && ffmpegVideoCodec == "libx264")
							videoHeight = -2; // h264 requires always a even width/height

						// forceOriginalAspectRatio could be: decrease or increase
						std::string forceOriginalAspectRatio;
						field = "forceOriginalAspectRatio";
						forceOriginalAspectRatio = JSONUtils::as<string>(bitRateInfo, field, "");

						bool pad = false;
						if (!forceOriginalAspectRatio.empty())
						{
							field = "pad";
							pad = JSONUtils::as<bool>(bitRateInfo, field, false);
						}

						// -vf "scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2"

						// ffmpegVideoResolution = "-vf scale=w=" + std::to_string(videoWidth)
						ffmpegVideoResolution = std::format("scale=w={}:h={}", videoWidth, videoHeight);
						if (!forceOriginalAspectRatio.empty())
						{
							ffmpegVideoResolution += std::format(":force_original_aspect_ratio={}", forceOriginalAspectRatio);
							if (pad)
								ffmpegVideoResolution += std::format(",pad={}:{}:(ow-iw)/2:(oh-ih)/2", videoWidth, videoHeight);
						}

						// ffmpegVideoResolution += " ";
					}

					std::string ffmpegVideoBitRate;
					int kBitRate;
					{
						field = "kBitRate";
						if (!JSONUtils::isPresent(bitRateInfo, field, true))
						{
							std::string errorMessage = std::format(
								"FFMpeg: Field is not present or it is null"
								", Field: {}",
								field
							);
							LOG_ERROR(errorMessage);

							throw std::runtime_error(errorMessage);
						}

						kBitRate = JSONUtils::as<int32_t>(bitRateInfo, field, 0);

						ffmpegVideoBitRate = std::format("-b:v {}k ", kBitRate);
					}

					// maxRate
					std::string ffmpegVideoMaxRate;
					{
						field = "kMaxRate";
						if (JSONUtils::isPresent(bitRateInfo, field, true))
						{
							int maxRate = JSONUtils::as<int32_t>(bitRateInfo, field, 0);

							ffmpegVideoMaxRate = std::format("-maxrate {}k ", maxRate);
						}
					}

					// minRate
					std::optional<std::string> ffmpegVideoMinRate{};
					{
						field = "kMinRate";
						if (JSONUtils::isPresent(bitRateInfo, field, true))
						{
							int minRate = JSONUtils::as<int32_t>(bitRateInfo, field, 0);

							ffmpegVideoMinRate = std::format("-minrate {}k ", minRate);
						}
					}

					// bufSize
					std::string ffmpegVideoBufSize;
					{
						field = "kBufferSize";
						if (JSONUtils::isPresent(bitRateInfo, field, true))
						{
							int bufferSize = JSONUtils::as<int32_t>(bitRateInfo, field, 0);

							ffmpegVideoBufSize = std::format("-bufsize {}k ", bufferSize);
						}
					}

					videoBitRatesInfo.emplace_back(ffmpegVideoResolution, kBitRate, videoWidth, videoHeight,
						ffmpegVideoBitRate, ffmpegVideoMaxRate, ffmpegVideoMinRate, ffmpegVideoBufSize
					);
				}
			}
		}
		else
		{
			// 2023-05-07: Si tratta di un video e l'encoding profile non ha il field "video".
			//	Per cui sarà un Encoding Profile solo audio. In questo caso "copy" le traccie video sorgenti

			twoPasses = false;
			ffmpegVideoCodecParameter = "-codec:v copy ";
		}
	}

	// if (contentType == "video" || contentType == "audio")
	{
		field = "audio";
		if (!JSONUtils::isPresent(encodingProfileDetailsRoot, field, true))
		{
			std::string errorMessage = std::format(
				"FFMpeg: Field is not present or it is null"
				", Field: {}",
				field
			);
			LOG_ERROR(errorMessage);

			throw std::runtime_error(errorMessage);
		}

		nlohmann::json audioRoot = encodingProfileDetailsRoot[field];

		// codec
		{
			field = "codec";
			if (!JSONUtils::isPresent(audioRoot, field, true))
			{
				std::string errorMessage = std::format(
					"FFMpeg: Field is not present or it is null"
					", Field: {}",
					field
				);
				LOG_ERROR(errorMessage);

				throw std::runtime_error(errorMessage);
			}
			ffmpegAudioCodec = JSONUtils::as<string>(audioRoot, field, "");

			FFMpegEncodingParameters::encodingAudioCodecValidation(ffmpegAudioCodec);

			ffmpegAudioCodecParameter = std::format("-acodec {} ", ffmpegAudioCodec);
		}

		// kBitRate
		/*
		{
			field = "kBitRate";
			if (JSONUtils::isPresent(audioRoot, field))
			{
				int bitRate = JSONUtils::as<int32_t>(audioRoot, field, 0);

				ffmpegAudioBitRateParameter =
						"-b:a " + std::to_string(bitRate) + "k "
				;
			}
		}
		*/

		// OtherOutputParameters
		{
			field = "otherOutputParameters";
			if (JSONUtils::isPresent(audioRoot, field, true))
			{
				std::string otherOutputParameters = JSONUtils::as<string>(audioRoot, field, "");

				ffmpegAudioOtherParameters = otherOutputParameters + " ";
			}
		}

		// channelsNumber
		{
			field = "channelsNumber";
			if (JSONUtils::isPresent(audioRoot, field, true))
			{
				int channelsNumber = JSONUtils::as<int32_t>(audioRoot, field, 0);

				ffmpegAudioChannelsParameter = std::format("-ac {} ", channelsNumber);
			}
		}

		// sample rate
		{
			field = "sampleRate";
			if (JSONUtils::isPresent(audioRoot, field, true))
			{
				int sampleRate = JSONUtils::as<int32_t>(audioRoot, field, 0);

				ffmpegAudioSampleRateParameter = std::format("-ar {} ", sampleRate);
			}
		}

		field = "bitRates";
		if (!JSONUtils::isPresent(audioRoot, field, true))
		{
			std::string errorMessage = std::format(
				"FFMpeg: Field is not present or it is null"
				", Field: {}",
				field
			);
			LOG_ERROR(errorMessage);

			throw std::runtime_error(errorMessage);
		}
		nlohmann::json bitRatesRoot = audioRoot[field];

		audioBitRatesInfo.clear();
		{
			for (const auto& bitRateInfo : bitRatesRoot)
			{
				std::string ffmpegAudioBitRate;
				{
					field = "kBitRate";
					if (!JSONUtils::isPresent(bitRateInfo, field, true))
					{
						std::string errorMessage = std::format(
							"FFMpeg: Field is not present or it is null"
							", Field: {}",
							field
						);
						LOG_ERROR(errorMessage);

						throw std::runtime_error(errorMessage);
					}

					int kBitRate = JSONUtils::as<int32_t>(bitRateInfo, field, 0);

					ffmpegAudioBitRate = std::format("-b:a {}k ", kBitRate);
				}

				audioBitRatesInfo.push_back(ffmpegAudioBitRate);
			}
		}
	}
}

void FFMpegEncodingParameters::addToArguments(const std::string& parameter, std::vector<std::string> &argumentList)
{
	if (!parameter.empty())
	{
		std::string item;
		std::stringstream parameterStream(parameter);

		while (getline(parameterStream, item, ' '))
		{
			if (!item.empty())
				argumentList.push_back(item);
		}
	}
}

void FFMpegEncodingParameters::encodingFileFormatValidation(std::string fileFormat)
{
	std::string fileFormatLowerCase;
	fileFormatLowerCase.resize(fileFormat.size());
	std::ranges::transform(fileFormat, fileFormatLowerCase.begin(), [](unsigned char c) { return tolower(c); });

	if (fileFormatLowerCase != "3gp" && fileFormatLowerCase != "mp4" && fileFormatLowerCase != "mov" && fileFormatLowerCase != "webm" &&
		fileFormatLowerCase != "hls" && fileFormatLowerCase != "dash" && fileFormatLowerCase != "ts" && fileFormatLowerCase != "mts" &&
		fileFormatLowerCase != "mkv" && fileFormatLowerCase != "avi" && fileFormatLowerCase != "flv" && fileFormatLowerCase != "ogg" &&
		fileFormatLowerCase != "wmv" && fileFormatLowerCase != "yuv" && fileFormatLowerCase != "mpg" && fileFormatLowerCase != "mpeg" &&
		fileFormatLowerCase != "mjpeg" && fileFormatLowerCase != "mxf")
	{
		const std::string errorMessage = std::format(
			"FFMpeg: fileFormat is wrong"
			", fileFormatLowerCase: {}",
			fileFormatLowerCase
		);
		LOG_ERROR(errorMessage);

		throw std::runtime_error(errorMessage);
	}
}

void FFMpegEncodingParameters::encodingAudioCodecValidation(std::string codec)
{
	if (codec != "aac" && codec != "libfdk_aac" && codec != "libvo_aacenc" && codec != "libvorbis" && codec != "pcm_s16le" && codec != "pcm_s32le")
	{
		const std::string errorMessage = std::format(
			"FFMpeg: Audio codec is wrong"
			", codec: {}",
			codec
		);
		LOG_ERROR(errorMessage);

		throw std::runtime_error(errorMessage);
	}
}

void FFMpegEncodingParameters::encodingVideoProfileValidation(std::string codec, std::string profile)
{
	if (codec == "libx264")
	{
		if (profile != "high" && profile != "baseline" && profile != "main" && profile != "high422" // used in case of mxf
		)
		{
			const std::string errorMessage = std::format(
				"FFMpeg: Profile is wrong"
				", codec: {}"
				", profile: {}",
				codec, profile
			);
			LOG_ERROR(errorMessage);

			throw std::runtime_error(errorMessage);
		}
	}
	else if (codec == "libvpx")
	{
		if (profile != "best" && profile != "good")
		{
			const std::string errorMessage = std::format(
				"FFMpeg: Profile is wrong"
				", codec: {}"
				", profile: {}",
				codec, profile
			);
			LOG_ERROR(errorMessage);

			throw std::runtime_error(errorMessage);
		}
	}
	else if (codec == "mpeg4" || codec == "xvid")
	{
		if (!profile.empty())
		{
			if (profile.find_first_not_of("0123456789") != std::string::npos || stoi(profile) < 1 || stoi(profile) > 31)
			{
				std::string errorMessage = std::format(
					"FFMpeg: Profile is wrong"
					", codec: {}"
					", profile: {}",
					codec, profile
				);
				LOG_ERROR(errorMessage);

				throw std::runtime_error(errorMessage);
			}
		}
	}
	else
	{
		std::string errorMessage = std::format(
			"FFMpeg: codec is wrong"
			", codec: {}",
			codec
		);
		LOG_ERROR(errorMessage);

		throw std::runtime_error(errorMessage);
	}
}

void FFMpegEncodingParameters::removeTwoPassesTemporaryFiles() const
{
	try
	{
		std::string prefixPasslogFileName = std::to_string(_ingestionJobKey) + "_" + std::to_string(_encodingJobKey);

		for (fs::directory_entry const &entry : fs::directory_iterator(_ffmpegTempDir))
		{
			try
			{
				if (!entry.is_regular_file())
					continue;

				if (entry.path().filename().string().size() >= prefixPasslogFileName.size() &&
					entry.path().filename().string().compare(0, prefixPasslogFileName.size(), prefixPasslogFileName) == 0)
				{
					LOG_INFO(
						"Remove"
						", pathFileName: {}",
						entry.path().string()
					);
					fs::remove_all(entry.path());
				}
			}
			catch (std::runtime_error &e)
			{
				std::string errorMessage = std::format(
					"listing directory failed"
					", e.what(): {}",
					e.what()
				);
				LOG_ERROR(errorMessage);

				throw e;
			}
			catch (std::exception &e)
			{
				std::string errorMessage = std::format(
					"listing directory failed"
					", e.what(): {}",
					e.what()
				);
				LOG_ERROR(errorMessage);

				throw e;
			}
		}
	}
	catch (std::exception &e)
	{
		LOG_ERROR(
			"removeTwoPassesTemporaryFiles failed"
			", exception: {}",
			e.what()
		);
	}
}

std::string FFMpegEncodingParameters::getMultiTrackEncodedStagingTemplateAssetPathName()
{

	size_t extensionIndex = _encodedStagingAssetPathName.find_last_of('.');
	if (extensionIndex == std::string::npos)
	{
		std::string errorMessage = std::format(
			"No extension found"
			", _encodedStagingAssetPathName: {}",
			_encodedStagingAssetPathName
		);
		LOG_ERROR(errorMessage);

		throw std::runtime_error(errorMessage);
	}

	// I tried the std::string::insert method but it did not work
	return _encodedStagingAssetPathName.substr(0, extensionIndex) + "_" + _multiTrackTemplatePart +
		   _encodedStagingAssetPathName.substr(extensionIndex);
}

bool FFMpegEncodingParameters::getMultiTrackPathNames(std::vector<std::string> &sourcesPathName)
{
	if (_videoBitRatesInfo.size() <= 1)
		return false; // no multi tracks

	// all the tracks generated in different files have to be copied
	// into the encodedStagingAssetPathName file
	// The command willl be:
	//		ffmpeg -i ... -i ... -c copy -map 0 -map 1 ... <dest file>

	sourcesPathName.clear();

	for (const auto& videoBitRateInfo : _videoBitRatesInfo)
	{
		int videoHeight = -1;

		tie(std::ignore, std::ignore, std::ignore, videoHeight, std::ignore, std::ignore,
			std::ignore, std::ignore) = videoBitRateInfo;

		std::string encodedStagingTemplateAssetPathName = getMultiTrackEncodedStagingTemplateAssetPathName();

		std::string newStagingEncodedAssetPathName =
			std::regex_replace(encodedStagingTemplateAssetPathName, std::regex(_multiTrackTemplateVariable), std::to_string(videoHeight));
		sourcesPathName.push_back(newStagingEncodedAssetPathName);
	}

	return true; // yes multi tracks
}

void FFMpegEncodingParameters::removeMultiTrackPathNames()
{
	std::vector<std::string> sourcesPathName;

	if (!getMultiTrackPathNames(sourcesPathName))
		return; // no multi tracks

	for (std::string sourcePathName : sourcesPathName)
	{
		LOG_INFO(
			"Remove"
			", sourcePathName: {}",
			sourcePathName
		);
		fs::remove_all(sourcePathName);
	}
}
