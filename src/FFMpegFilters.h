/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/*
 * File:   FFMPEGFilters.h
 * Author: giuliano
 *
 * Created on February 18, 2018, 1:27 AM
 */

#pragma once

#include <chrono>
#include "JSONUtils.h"
#include <string>
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif
#include "spdlog/spdlog.h"

using namespace std;

class FFMpegFilters
{
  public:
	FFMpegFilters(string ffmpegTempDir, string ffmpegTtfFontDir, int64_t ingestionJobKey, int64_t encodingJobKey, int outputIndex = -1);

	~FFMpegFilters();

	tuple<string, string, string>
	addFilters(json filtersRoot, const string& ffmpegVideoResolutionParameter, const string& ffmpegDrawTextFilter,
		optional<int32_t> inputDurationInSeconds) const;

	[[nodiscard]] string addVideoFilters(
		json filtersRoot, const string &ffmpegVideoResolutionParameter, const string &ffmpegDrawTextFilter, optional<int32_t> inputDurationInSeconds
	) const;

	[[nodiscard]] string addAudioFilters(const json &filtersRoot, optional<int32_t> inputDurationInSeconds) const;

	[[nodiscard]] string getFilter(const json &filterRoot, optional<int32_t> inputDurationInSeconds) const;

	static json mergeFilters(const json &filters_1Root, const json &filters_2Root);

	static string getDrawTextTemporaryPathName(const string &ffmpegTempDir, int64_t ingestionJobKey, int64_t encodingJobKey, int outputIndex);
	static json createTimecodeDrawTextFilter();

  private:
	string _ffmpegTempDir;
	string _ffmpegTtfFontDir;

	int64_t _ingestionJobKey;
	int64_t _encodingJobKey;
	int _outputIndex;
};
