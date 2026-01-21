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

#include <string>
#include "JSONUtils.h"
#include "spdlog/spdlog.h"


#ifndef __FILEREF__
#ifdef __APPLE__
#define __FILEREF__ string("[") + string(__FILE__).substr(string(__FILE__).find_last_of("/") + 1) + ":" + to_string(__LINE__) + "] "
#else
#define __FILEREF__ string("[") + basename((char *)__FILE__) + ":" + to_string(__LINE__) + "] "
#endif
#endif

class FFMpegEncodingParameters
{
  private:
	std::string _ffmpegTempDir;
	std::string _ffmpegTtfFontDir;
	std::string _multiTrackTemplateVariable;
	std::string _multiTrackTemplatePart;

	int64_t _ingestionJobKey;
	int64_t _encodingJobKey;
	std::string _encodedStagingAssetPathName;
	bool _isVideo;
	nlohmann::json _videoTracksRoot;
	nlohmann::json _audioTracksRoot;
	int _videoTrackIndexToBeUsed;
	int _audioTrackIndexToBeUsed;

	bool _initialized;

	std::string _ffmpegHttpStreamingParameter;

	std::string _ffmpegFileFormatParameter;

	std::string _ffmpegVideoCodecParameter;
	std::string _ffmpegVideoCodec;
	std::string _ffmpegVideoProfileParameter;
	std::string _ffmpegVideoOtherParameters;
	std::string _ffmpegVideoFrameRateParameter;
	std::string _ffmpegVideoKeyFramesRateParameter;
	std::vector<std::tuple<std::string, int, int, int, std::string, std::string, std::string>> _videoBitRatesInfo;

	std::string _ffmpegAudioCodecParameter;
	std::string _ffmpegAudioCodec;
	std::string _ffmpegAudioOtherParameters;
	std::string _ffmpegAudioChannelsParameter;
	std::string _ffmpegAudioSampleRateParameter;
	std::vector<std::string> _audioBitRatesInfo;

	std::string getManifestFileName();
	std::string getMultiTrackTemplatePart();
	std::string getMultiTrackEncodedStagingTemplateAssetPathName();

  public:
	std::string _httpStreamingFileFormat;

	FFMpegEncodingParameters(
		int64_t ingestionJobKey, int64_t encodingJobKey, nlohmann::json encodingProfileDetailsRoot,
		bool isVideo, // if false it means is audio
		int videoTrackIndexToBeUsed, int audioTrackIndexToBeUsed, std::string encodedStagingAssetPathName, nlohmann::json videoTracksRoot, nlohmann::json audioTracksRoot,

		bool &twoPasses, // out

		std::string ffmpegTempDir, std::string ffmpegTtfFontDir
	);

	~FFMpegEncodingParameters();

	void applyEncoding(
		// -1: NO two passes
		// 0: YES two passes, first step
		// 1: YES two passes, second step
		int stepNumber,

		// in alcuni casi i parametro del file di output non deve essere aggiunto, ad esempio
		// per il LiveRecorder o LiveProxy o nei casi in cui il file di output viene deciso
		// dal chiamante senza seguire il fileFormat dell'encoding profile
		bool outputFileToBeAdded,

		bool videoResolutionToBeAdded,

		nlohmann::json filtersRoot,

		// out (in append)
		std::vector<std::string> &ffmpegArgumentList
	);
	void applyEncoding(int stepNumber, bool outputFileToBeAdded, bool videoResolutionToBeAdded, const nlohmann::json &filtersRoot, FFMpegEngine &ffMpegEngine);

	void createManifestFile();

	void removeTwoPassesTemporaryFiles();

	bool getMultiTrackPathNames(std::vector<std::string> &sourcesPathName);
	void removeMultiTrackPathNames();

	void applyEncoding_audioGroup(
		// -1: NO two passes
		// 0: YES two passes, first step
		// 1: YES two passes, second step
		int stepNumber,

		// out (in append)
		std::vector<std::string> &ffmpegArgumentList
	);
	void applyEncoding_audioGroup(int stepNumber, FFMpegEngine &ffMpegEngine);

	void createManifestFile_audioGroup();

	static void settingFfmpegParameters(
		nlohmann::json encodingProfileDetailsRoot, bool isVideo,
		// if false it means is audio

		std::string &httpStreamingFileFormat, std::string &ffmpegHttpStreamingParameter,

		std::string &ffmpegFileFormatParameter,

		std::string &ffmpegVideoCodecParameter, std::string &ffmpegVideoCodec, std::string &ffmpegVideoProfileParameter, std::string &ffmpegVideoOtherParameters,
		bool &twoPasses, std::string &ffmpegVideoFrameRateParameter, std::string &ffmpegVideoKeyFramesRateParameter,

		std::vector<std::tuple<std::string, int, int, int, std::string, std::string, std::string>> &videoBitRatesInfo, std::string &ffmpegAudioCodecParameter, std::string &ffmpegAudioCodec,
		std::string &ffmpegAudioOtherParameters, std::string &ffmpegAudioChannelsParameter, std::string &ffmpegAudioSampleRateParameter,
		std::vector<std::string> &audioBitRatesInfo
	);

	static void addToArguments(std::string parameter, std::vector<std::string> &argumentList);

	static void encodingFileFormatValidation(std::string fileFormat);

	static void encodingAudioCodecValidation(std::string codec);

	static void encodingVideoProfileValidation(std::string codec, std::string profile);
};
