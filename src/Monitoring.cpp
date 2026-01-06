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
#include "StringUtils.h"
#include "spdlog/spdlog.h"
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <regex>

using namespace std;
using json = nlohmann::json;

string FFMpegWrapper::getOutputFfmpegPathFileName() const
{
	return _outputFfmpegPathFileName;
}

time_t FFMpegWrapper::getOutputFFMpegFileLastModificationTime()
{
	try
	{
		if (!fs::exists(_outputFfmpegPathFileName.c_str()))
		{
			SPDLOG_INFO(
				"getOutputFFMpegFileLastModificationTime: Encoding status not available"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", _outputFfmpegPathFileName: {}"
				", _currentMMSSourceAssetPathName: {}"
				", _currentStagingEncodedAssetPathName: {}",
				_currentIngestionJobKey, _currentEncodingJobKey, _outputFfmpegPathFileName, _currentMMSSourceAssetPathName,
				_currentStagingEncodedAssetPathName
			);

			throw FFMpegEncodingStatusNotAvailable();
		}

		// last_write_time ritorna un file_time_type, che usa un clock diverso da system_clock. Per questo serve la conversione.
		auto ftime = fs::last_write_time(_outputFfmpegPathFileName);

		// Converti in time_t (secondi da epoch)
		auto sctp = chrono::time_point_cast<chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + chrono::system_clock::now());

		return chrono::system_clock::to_time_t(sctp);
	}
	catch (FFMpegEncodingStatusNotAvailable &e)
	{
		SPDLOG_WARN(
			"getOutputFFMpegFileLastModificationTime failed"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", _outputFfmpegPathFileName: {}"
			", _currentMMSSourceAssetPathName: {}"
			", _currentStagingEncodedAssetPathName: {}"
			", e.what: {}",
			_currentIngestionJobKey, _currentEncodingJobKey, _outputFfmpegPathFileName, _currentMMSSourceAssetPathName,
			_currentStagingEncodedAssetPathName, e.what()
		);

		throw e;
	}
	catch (exception &e)
	{
		SPDLOG_ERROR(
			"getOutputFFMpegFileLastModificationTime failed"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", _outputFfmpegPathFileName: {}"
			", _currentMMSSourceAssetPathName: {}"
			", _currentStagingEncodedAssetPathName: {}"
			", e.what: {}",
			_currentIngestionJobKey, _currentEncodingJobKey, _outputFfmpegPathFileName, _currentMMSSourceAssetPathName,
			_currentStagingEncodedAssetPathName, e.what()
		);

		throw;
	}
}

uintmax_t FFMpegWrapper::getOutputFFMpegFileSize()
{
	try
	{
		if (!fs::exists(_outputFfmpegPathFileName.c_str()))
		{
			SPDLOG_INFO(
				"getOutputFFMpegFileSize: Encoding status not available"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", _outputFfmpegPathFileName: {}"
				", _currentMMSSourceAssetPathName: {}"
				", _currentStagingEncodedAssetPathName: {}",
				_currentIngestionJobKey, _currentEncodingJobKey, _outputFfmpegPathFileName, _currentMMSSourceAssetPathName,
				_currentStagingEncodedAssetPathName
			);

			throw FFMpegEncodingStatusNotAvailable();
		}

		auto size = fs::file_size(_outputFfmpegPathFileName);

		return size;
	}
	catch (FFMpegEncodingStatusNotAvailable &e)
	{
		SPDLOG_WARN(
			"getOutputFFMpegFileSize failed"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", _outputFfmpegPathFileName: {}"
			", _currentMMSSourceAssetPathName: {}"
			", _currentStagingEncodedAssetPathName: {}"
			", e.what: {}",
			_currentIngestionJobKey, _currentEncodingJobKey, _outputFfmpegPathFileName, _currentMMSSourceAssetPathName,
			_currentStagingEncodedAssetPathName, e.what()
		);

		throw e;
	}
	catch (exception &e)
	{
		SPDLOG_ERROR(
			"getOutputFFMpegFileSize failed"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", _outputFfmpegPathFileName: {}"
			", _currentMMSSourceAssetPathName: {}"
			", _currentStagingEncodedAssetPathName: {}"
			", e.what: {}",
			_currentIngestionJobKey, _currentEncodingJobKey, _outputFfmpegPathFileName, _currentMMSSourceAssetPathName,
			_currentStagingEncodedAssetPathName, e.what()
		);

		throw;
	}
}

/*
string FFMpegWrapper::getLastPartOfFile(string pathFileName, int lastCharsToBeRead)
{
	string lastPartOfFile = "";
	char *buffer = nullptr;

	try
	{
		ifstream ifPathFileName(pathFileName);
		if (ifPathFileName)
		{
			int charsToBeRead;

			// get length of file:
			ifPathFileName.seekg(0, ifPathFileName.end);
			int fileSize = ifPathFileName.tellg();
			if (fileSize >= lastCharsToBeRead)
			{
				ifPathFileName.seekg(fileSize - lastCharsToBeRead, ifPathFileName.beg);
				charsToBeRead = lastCharsToBeRead;
			}
			else
			{
				ifPathFileName.seekg(0, ifPathFileName.beg);
				charsToBeRead = fileSize;
			}

			buffer = new char[charsToBeRead];
			ifPathFileName.read(buffer, charsToBeRead);
			if (ifPathFileName)
			{
				// all characters read successfully
				lastPartOfFile.assign(buffer, charsToBeRead);
			}
			else
			{
				// error: only is.gcount() could be read";
				lastPartOfFile.assign(buffer, ifPathFileName.gcount());
			}
			ifPathFileName.close();

			delete[] buffer;
		}
	}
	catch (exception &e)
	{
		if (buffer != nullptr)
			delete[] buffer;

		SPDLOG_ERROR("getLastPartOfFile failed");
	}

	return lastPartOfFile;
}
*/