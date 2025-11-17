
#include "FFMpegEngine.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <ranges>
#include <sstream>

// ---------------- Input methods ----------------

FFMpegEngine::Input& FFMpegEngine::Input::addArg(const string_view& parameter)
{
	_args.emplace_back(parameter);
	return *this;
}

FFMpegEngine::Input& FFMpegEngine::Input::addArgs(const string& parameters)
{
	for (auto&& tok :
		parameters | views::split(' ') | views::filter([](auto &&rng){ return !ranges::empty(rng); }))
		_args.emplace_back(tok.begin(), tok.end());
	return *this;
}

// ---------------- Output methods ----------------

FFMpegEngine::Output& FFMpegEngine::Output::addArg(const string_view& parameter)
{
	_extraArgs.emplace_back(parameter);
	return *this;
}

FFMpegEngine::Output& FFMpegEngine::Output::addArgs(const string& parameters)
{
	for (auto&& tok :
		parameters | views::split(' ') | views::filter([](auto &&rng){ return !ranges::empty(rng); }))
		_extraArgs.emplace_back(tok.begin(), tok.end());
	return *this;
}

// ---------------- builder methods ----------------

FFMpegEngine& FFMpegEngine::addGlobalArg(const string_view& a) {
    _globalArgs.emplace_back(a);
    return *this;
}

FFMpegEngine& FFMpegEngine::addGlobalArgs(const string& parameters)
{
	for (auto&& tok :
		parameters | views::split(' ') | views::filter([](auto &&rng){ return !ranges::empty(rng); }))
		_globalArgs.emplace_back(tok.begin(), tok.end());
    return *this;
}

FFMpegEngine& FFMpegEngine::setUserAgent(const string_view& ua) {
	_userAgent = string(ua);
	return *this;
}

FFMpegEngine::Input& FFMpegEngine::addInput(const string_view source) {
	_inputs.emplace_back(source);
	return _inputs.back();
}

FFMpegEngine::Input& FFMpegEngine::addInput() {
	_inputs.emplace_back();
    return _inputs.back();
}

FFMpegEngine::Output& FFMpegEngine::addOutput(const string_view path) {
	_outputs.emplace_back(path);
	return _outputs.back();
}

FFMpegEngine::Output& FFMpegEngine::addOutput() {
    _outputs.emplace_back();
    return _outputs.back();
}

FFMpegEngine& FFMpegEngine::addFilterComplex(const string_view& fc) {
    _filterComplex.emplace_back(fc);
    return *this;
}

FFMpegEngine::Input& FFMpegEngine::addSrtInput(const string_view& target, optional<int> latencyMilliSeconds) {
    auto& in = addInput(std::format("srt://{}", target));
    if (latencyMilliSeconds)
    	in.addArg(string("-timeout ") + to_string(*latencyMilliSeconds));
    return in;
}

FFMpegEngine::Input& FFMpegEngine::addUdpInput(const string_view& target, optional<int> listenTimeoutMilliSeconds) {
	if (listenTimeoutMilliSeconds)
		return addInput(std::format("udp://{}?timeout=", target, *listenTimeoutMilliSeconds * 1000));
	return addInput(std::format("udp://{}", target));
}

FFMpegEngine::Input& FFMpegEngine::addRtmpInput(const string_view& target) {
    return addInput(std::format("rtmp://{}", target));
}

FFMpegEngine::Input& FFMpegEngine::addPipeInput(const string_view& spec) {
    return addInput(spec);
}

FFMpegEngine& FFMpegEngine::enableNvenc() {
    _hwAccel = "nvenc";
    return *this;
}

FFMpegEngine& FFMpegEngine::enableVaapi(const string_view& device) {
    _hwAccel = "vaapi";
    _vaapiDevice = string(device);
    return *this;
}

FFMpegEngine& FFMpegEngine::enableVideoToolbox() {
    _hwAccel = "videotoolbox";
    return *this;
}

// Prepare VAAPI upload filter and ensure device arg is present
FFMpegEngine& FFMpegEngine::vaapiPrepareUpload() {
    // Add a default hwupload filter for use in filter_complex consumers
    // Caller should add [in] format/ hwupload and map the output to encoder
    if (!_vaapiDevice)
    	_vaapiDevice = "/dev/dri/renderD128";
    // It's user's job to craft proper filter_complex, but we add a helper global arg
    _globalArgs.push_back(std::format("-vaapi_device {}", *_vaapiDevice));
    return *this;
}

FFMpegEngine& FFMpegEngine::addWatermark(Output& out, string_view overlayLabel, string_view pos) {
    out.addVideoFilter(string(overlayLabel) + " overlay=" + string(pos));
    return *this;
}

void FFMpegEngine::setDurationMilliSeconds(int64_t durationMilliSeconds) {
    _durationMilliSeconds = durationMilliSeconds;
}

// ---------------- build args (vector) ----------------

vector<string> FFMpegEngine::buildArgs(bool useProgressPipe) const {
    vector<string> args;

	args.emplace_back("ffmpeg");

    for (auto& g : _globalArgs)
    	args.emplace_back(g);

    // inputs
    for (auto& in : _inputs)
	{
        for (auto& a : in._args) args.emplace_back(a);
		if (in._durationSeconds > 0)
		{
            args.emplace_back("-t");
            args.emplace_back(std::format("{}", in._durationSeconds));
        }
        args.emplace_back("-i");
        args.emplace_back(in._source);
    }

    if (!_filterComplex.empty())
    {
        args.emplace_back("-filter_complex");
        // join
        string fc;
        for (size_t i = 0; i < _filterComplex.size(); ++i)
        {
            fc += _filterComplex[i];
            if (i + 1 < _filterComplex.size())
            	fc += ";";
        }
        args.emplace_back(fc);
    }

    // outputs
    for (auto& output : _outputs)
    {
        for (auto& map : output._maps)
        {
	        args.emplace_back("-map");
        	args.emplace_back(map);
        }
        if (output._videoCodec)
        {
	        args.emplace_back("-c:v");
        	args.emplace_back(*output._videoCodec);
        }
        if (output._audioCodec)
        {
	        args.emplace_back("-c:a");
        	args.emplace_back(*output._audioCodec);
        }
        if (!output._videoFilters.empty())
        {
            string vf;
            for (size_t index=0; index < output._videoFilters.size(); ++index)
            {
                vf += output._videoFilters[index];
                if (index + 1 < output._videoFilters.size())
                	vf += ",";
            }
            args.emplace_back("-vf");
        	args.emplace_back(vf);
        }
        if (!output._audioFilters.empty())
        {
            string af;
            for (size_t index = 0; index < output._audioFilters.size(); ++index)
            {
                af += output._audioFilters[index];
                if (index + 1 < output._audioFilters.size())
                	af += ",";
            }
            args.emplace_back("-af");
        	args.emplace_back(af);
        }
        for (auto& extraArg : output._extraArgs)
        	args.emplace_back(extraArg);
        args.emplace_back(output._path);
    }

    if (useProgressPipe)
    {
        args.emplace_back("-nostats");
        args.emplace_back("-progress");
        args.emplace_back("pipe:1");
    }

    return args;
}

string FFMpegEngine::build(bool useProgressPipe) const
{
    auto args = buildArgs(useProgressPipe);

	ostringstream ffmpegArgumentListStream;

	// if (!ffmpegArgumentList.empty())
	// 	copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(), ostream_iterator<string>(ffmpegArgumentListStream, " "));

    for (size_t index = 0; index < args.size(); ++index)
    {
        if (index)
        	ffmpegArgumentListStream << " ";
        // simple quoting for visualization
        if (args[index].find(' ') != string::npos)
        	ffmpegArgumentListStream << "\"" << args[index] << "\"";
        else
        	ffmpegArgumentListStream << args[index];
    }

    return ffmpegArgumentListStream.str();
}

// ----------------- formatter per mostrare i comandi -----------------
string FFMpegEngine::toPrettyString(const int indentSpaces) const {
	std::ostringstream oss;
	const std::string indent(indentSpaces, ' ');

	// --- INPUTS ---
	oss << "Inputs:\n";
	for (const auto &inp : _inputs) {
		std::string line;
		for (auto &opt : inp._args)
			line += opt + " ";
		line += "-i " + inp._source;
		oss << indent << line << "\n";
	}
	oss << "\n";

	// --- FILTERS ---
	if (!_filterComplex.empty()) {
		oss << "Filters:\n";
		std::vector<std::string> filterLines;
		for (const auto& f : _filterComplex)
				oss << indent << f << "\n";
		oss << "\n";
	}

	// --- OUTPUT ---
	for (const auto& out : _outputs)
	{
		oss << "Output: " << out._path << "\n";
		oss << indent << "Video codec: " << *out._videoCodec << "\n";
		oss << indent << "Audio codec: " << *out._audioCodec << "\n";
		for (const auto& opt : out._extraArgs)
			oss << indent << opt << "\n";
	}
	oss << "\n";

	return oss.str();
}

/// Formato singola linea, come vero comando ffmpeg
std::string FFMpegEngine::toSingleLine() const {

	return build();
}