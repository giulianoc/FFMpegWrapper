
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <functional>
#include <atomic>
#include <thread>

using namespace std;

class FFmpegEngine {
public:
	/*
    struct Progress {
        optional<int64_t> frame;
        optional<double> fps;
        optional<int64_t> totalSize; // bytes
        optional<int64_t> outTimeMs;
        optional<string> progress; // "continue" or "end"
    };
    */

    class Input {
    	friend FFmpegEngine;

		string _source;
        vector<string> _args;
		int32_t _durationSeconds = -1;
    public:
    	Input() = default;
    	explicit Input(const string_view& source) : _source(source) {}
		Input& setSource(const string_view& source) { _source = source; return *this; }
		Input& setDurationSeconds(const int32_t durationSeconds) { _durationSeconds = durationSeconds; return *this; }
		Input& addArg(const string_view& parameter);
    	Input& addArgs(const string& parameters);
    };

    class Output {
    	friend FFmpegEngine;

    	string _path;
        vector<string> _maps;
        vector<string> _videoFilters;
        vector<string> _audioFilters;
        optional<string> _videoCodec;
        optional<string> _audioCodec;
        vector<string> _extraArgs;
    public:
        Output() = default;
    	explicit Output(const string_view& path) : _path(path) {}
		Output& setPath(const string_view& path) { _path = path; return *this; }
        Output& map(string_view m) { _maps.emplace_back(m); return *this; }
        Output& withVideoCodec(string_view c) { _videoCodec = string(c); return *this; }
        Output& withAudioCodec(string_view c) { _audioCodec = string(c); return *this; }
        Output& addVideoFilter(string_view f) { _videoFilters.emplace_back(f); return *this; }
        Output& addAudioFilter(string_view f) { _audioFilters.emplace_back(f); return *this; }
        Output& addArg(const string_view& parameter);
     	Output& addArgs(const string& parameters);
   };

    FFmpegEngine() = default;

	FFmpegEngine& setUserAgent(const string_view& ua);

    // builder
    FFmpegEngine& addGlobalArg(const string_view &a);
	FFmpegEngine& addGlobalArgs(const string& parameters);
    Input& addInput(string_view source);
    Input& addInput();
    Output& addOutput(string_view path);
    Output& addOutput();
    FFmpegEngine& addFilterComplex(const string_view &fc);

    // convenience inputs
	Input& addUdpInput(const string_view& target, optional<int> listenTimeoutMilliSeconds = {});
    Input& addSrtInput(const string_view &target, optional<int> latencyMilliSeconds = {});
    Input& addRtmpInput(const string_view &target);
    Input& addPipeInput(const string_view &spec);

    // HW accel
    FFmpegEngine& enableNvenc();
    FFmpegEngine& enableVaapi(const string_view &device = "/dev/dri/renderD128");
    FFmpegEngine& enableVideoToolbox();

    // VAAPI convenience: prepare upload and choose codec names (adds filters/args as needed)
    // After calling this, for VAAPI outputs prefer videoCodec "h264_vaapi" or "hevc_vaapi"
    FFmpegEngine& vaapiPrepareUpload();

    // watermark / drawtext
    FFmpegEngine& addWatermark(Output& out, string_view overlayLabel, string_view pos = "10:10");

    // duration for percent calculation (ms). If set, progress percent = out_time_ms / durationMilliSeconds
    void setDurationMilliSeconds(int64_t durationMilliSeconds);

    // build command (not shell-quoted). useProgressPipe true adds -progress pipe:1
    [[nodiscard]] string build(bool useProgressPipe = false) const;
    [[nodiscard]] vector<string> buildArgs(bool useProgressPipe = false) const;

	[[nodiscard]] string toPrettyString(int indentSpaces = 2) const;
	[[nodiscard]] string toSingleLine() const;

private:
    // internals
	optional<string> _userAgent;
    vector<Input> _inputs;
    vector<Output> _outputs;
    vector<string> _filterComplex;
    vector<string> _globalArgs;
    optional<string> _hwAccel;
    optional<string> _vaapiDevice;

    optional<int64_t> _durationMilliSeconds;
};