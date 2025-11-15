
#include "FFMpegFilters.h"
#include "JSONUtils.h"
#include "StringUtils.h"

#include <fstream>
#include <utility>

FFMpegFilters::FFMpegFilters(string  ffmpegTempDir, string  ffmpegTtfFontDir, int64_t ingestionJobKey, int64_t encodingJobKey, int outputIndex)
	: _ffmpegTempDir(std::move(ffmpegTempDir)), _ffmpegTtfFontDir(std::move(ffmpegTtfFontDir)), _ingestionJobKey(ingestionJobKey), _encodingJobKey(encodingJobKey),
	  _outputIndex(outputIndex)
{
}

FFMpegFilters::~FFMpegFilters() = default;

tuple<string, string, string>
FFMpegFilters::addFilters(json filtersRoot, const string& ffmpegVideoResolutionParameter, const string& ffmpegDrawTextFilter,
	int64_t streamingDurationInSeconds)
{
	string videoFilters = addVideoFilters(filtersRoot, ffmpegVideoResolutionParameter, ffmpegDrawTextFilter, streamingDurationInSeconds);
	string audioFilters = addAudioFilters(filtersRoot, streamingDurationInSeconds);
	string complexFilters;

	if (filtersRoot != nullptr)
	{
		if (JSONUtils::isMetadataPresent(filtersRoot, "complex"))
		{
			for (int filterIndex = 0; filterIndex < filtersRoot["complex"].size(); filterIndex++)
			{
				json filterRoot = filtersRoot["complex"][filterIndex];

				string filter = getFilter(filterRoot, streamingDurationInSeconds);
				if (!complexFilters.empty())
					complexFilters += ",";
				complexFilters += filter;
			}
		}
	}

	// Simple and complex filtering cannot be used together for the same stream
	if (!complexFilters.empty())
	{
		if (!audioFilters.empty())
			complexFilters = std::format("{},{}", audioFilters, complexFilters);
		if (!videoFilters.empty())
			complexFilters = std::format("{},{}", videoFilters, complexFilters);
		videoFilters = "";
		audioFilters = "";
	}

	return make_tuple(videoFilters, audioFilters, complexFilters);
}

string FFMpegFilters::addVideoFilters(
	json filtersRoot, const string& ffmpegVideoResolutionParameter, const string& ffmpegDrawTextFilter, int64_t streamingDurationInSeconds
) const
{
	string videoFilters;

	if (!ffmpegVideoResolutionParameter.empty())
	{
		if (!videoFilters.empty())
			videoFilters += ",";
		videoFilters += ffmpegVideoResolutionParameter;
	}
	if (!ffmpegDrawTextFilter.empty())
	{
		if (!videoFilters.empty())
			videoFilters += ",";
		videoFilters += ffmpegDrawTextFilter;
	}

	if (filtersRoot != nullptr)
	{
		if (JSONUtils::isMetadataPresent(filtersRoot, "video"))
		{
			for (int filterIndex = 0; filterIndex < filtersRoot["video"].size(); filterIndex++)
			{
				json filterRoot = filtersRoot["video"][filterIndex];

				string filter = getFilter(filterRoot, streamingDurationInSeconds);
				if (!videoFilters.empty())
					videoFilters += ",";
				videoFilters += filter;
			}
		}
	}

	return videoFilters;
}

string FFMpegFilters::addAudioFilters(const json& filtersRoot, const int64_t streamingDurationInSeconds) const
{
	string audioFilters;

	if (filtersRoot != nullptr)
	{
		if (JSONUtils::isMetadataPresent(filtersRoot, "audio"))
		{
			for (const auto& filterRoot : filtersRoot["audio"])
			{
				const string filter = getFilter(filterRoot, streamingDurationInSeconds);
				if (!audioFilters.empty())
					audioFilters += ",";
				audioFilters += filter;
			}
		}
	}

	return audioFilters;
}

string FFMpegFilters::getFilter(const json& filterRoot, int64_t streamingDurationInSeconds) const
{
	string filter;

	if (!JSONUtils::isMetadataPresent(filterRoot, "type"))
	{
		string errorMessage = "filterRoot->type field does not exist";
		SPDLOG_ERROR(errorMessage);

		throw runtime_error(errorMessage);
	}
	string type = JSONUtils::asString(filterRoot, "type", "");

	if (type == "ametadata")
	{
		filter = ("ametadata=mode=print");
	}
	else if (type == "ashowinfo")
	{
		filter = ("ashowinfo");
	}
	else if (type == "blackdetect")
	{
		// Viene eseguita la scansione dei fotogrammi con il valore di luminanza indicato da pixel_black_th
		// lunghi almeno black_min_duration secondi
		double black_min_duration = JSONUtils::asDouble(filterRoot, "black_min_duration", 2);
		double pixel_black_th = JSONUtils::asDouble(filterRoot, "pixel_black_th", 0.0);

		filter = ("blackdetect=d=" + to_string(black_min_duration) + ":pix_th=" + to_string(pixel_black_th));
	}
	else if (type == "blackframe")
	{
		int amount = JSONUtils::asInt(filterRoot, "amount", 98);
		int threshold = JSONUtils::asInt(filterRoot, "threshold", 32);

		filter = ("blackframe=amount=" + to_string(amount) + ":threshold=" + to_string(threshold));
	}
	else if (type == "crop")
	{
		// x,y 0,0 indica il punto in basso a sinistra del video
		// in_h e in_w indicano input width and height
		string out_w = JSONUtils::asString(filterRoot, "out_w", "in_w");
		string out_h = JSONUtils::asString(filterRoot, "out_h", "in_h");
		// La posizione orizzontale, nel video di input, del bordo sinistro del video di output
		string x = JSONUtils::asString(filterRoot, "x", "(in_w-out_w)/2");
		// La posizione verticale, nel video in input, del bordo superiore del video in output
		string y = JSONUtils::asString(filterRoot, "y", "(in_h-out_h)/2");
		bool keep_aspect = JSONUtils::asBool(filterRoot, "keep_aspect", false);
		// Enable exact cropping. If enabled, subsampled videos will be cropped at exact width/height/x/y
		// as specified and will not be rounded to nearest smaller value
		bool exact = JSONUtils::asBool(filterRoot, "exact", false);

		// crop=w=100:h=100:x=12:y=34
		filter = std::format("crop=out_w={}:out_h={}:x={}:y={}:keep_aspect={}:exact={}", out_w, out_h, x, y, keep_aspect, exact);
	}
	else if (type == "drawbox")
	{
		// x,y 0,0 indica il punto in basso a sinistra del video
		// in_h e in_w indicano input width and height
		string x = JSONUtils::asString(filterRoot, "x", "0");
		string y = JSONUtils::asString(filterRoot, "y", "0");
		string width = JSONUtils::asString(filterRoot, "width", "300");
		string height = JSONUtils::asString(filterRoot, "height", "300");
		string fontColor = JSONUtils::asString(filterRoot, "fontColor", "red");
		int percentageOpacity = JSONUtils::asInt(filterRoot, "percentageOpacity", -1);
		// thickness: il valore speciale di "fill" riempie il box
		string thickness = JSONUtils::asString(filterRoot, "thickness", "3");

		string opacity;
		if (percentageOpacity != -1)
		{
			// char cOpacity[64];

			// sprintf(cOpacity, "%.1f", ((float)percentageOpacity) / 100.0);
			// opacity = ("@" + string(cOpacity));
			opacity = std::format("@{:.1}", ((float)percentageOpacity) / 100.0);
		}

		// drawbox=x=700:y=400:w=160:h=90:color=blue:t=5
		filter = std::format("drawbox=x={}:y={}:w={}:h={}:color={}{}:t={}", x, y, width, height, fontColor, opacity, thickness);
	}
	else if (type == "drawtext")
	{
		string text = JSONUtils::asString(filterRoot, "text", "");
		// timecode: none, editorial, pts
		string timecode = JSONUtils::asString(filterRoot, "timecode", "none");
		int reloadAtFrameInterval = JSONUtils::asInt(filterRoot, "reloadAtFrameInterval", -1);
		string textPosition_X_InPixel = JSONUtils::asString(filterRoot, "textPosition_X_InPixel", "");
		string textPosition_Y_InPixel = JSONUtils::asString(filterRoot, "textPosition_Y_InPixel", "");
		string fontType = JSONUtils::asString(filterRoot, "fontType", "");
		int fontSize = JSONUtils::asInt(filterRoot, "fontSize", -1);
		string fontColor = JSONUtils::asString(filterRoot, "fontColor", "");
		int textPercentageOpacity = JSONUtils::asInt(filterRoot, "textPercentageOpacity", -1);
		int shadowX = JSONUtils::asInt(filterRoot, "shadowX", 0);
		int shadowY = JSONUtils::asInt(filterRoot, "shadowY", 0);
		bool boxEnable = JSONUtils::asBool(filterRoot, "boxEnable", false);
		string boxColor = JSONUtils::asString(filterRoot, "boxColor", "");
		int boxPercentageOpacity = JSONUtils::asInt(filterRoot, "boxPercentageOpacity", -1);
		int boxBorderW = JSONUtils::asInt(filterRoot, "boxBorderW", 0);

		/* TIMECODE
		1) editorialTimecode: è un’informazione “editoriale”, non tecnica per la riproduzione. Questo timecode NON è usato internamente
			dal player o dal decoder video. E' usato in ambito broadcast, editing e post-produzione.
			Formato tipico SMPTE: HH:MM:SS:FF, FF = frame number (dipende dal frame rate)
			Indica un orario leggibile (es. 10:03:05:12) che rappresenta la posizione temporale del frame all’interno di un contenuto.
			Si trova nel file o nel flusso:
			- come metadata del flusso video (tag timecode=10:03:05:00);
			- o incorporato nel segnale SDI (VITC/LTC);
			- oppure codificato come stream separato (es. timecode stream)
		2) ptsTimecode: è il timecode tecnico basato sui PTS (Presentation Time Stamp) del flusso video. Questo timecode è usato
			dal player o dal decoder video per la riproduzione. Indica quando un frame (video o audio) deve essere mostrato o riprodotto.
			Si trova nel transport stream (es. MPEG-TS) o nel container (es. MP4) — per ogni pacchetto o frame.
			Serve a:
			- Mantenere la sincronia tra audio e video.
			- Ricostruire la temporizzazione corretta in decodifica.
			- Fare seeking e tagli precisi nei file.
		3) DTS (Decoding Time Stamp) è un altro tipo di timestamp usato per indicare quando un frame deve essere decodificato.
			Non sempre è uguale al PTS, specialmente nei flussi con B-frames. Infatti serve soprattutto per codec con B-frame (bidirezionali),
			dove l’ordine di decodifica ≠ ordine di presentazione.

		Scenario AWS con MediaLive e timecode insertion PIC_TIMING_SEI
			Se il flusso in input contiene il timecode nel SEI pic_timing, AWS con Media Live è in grado di leggerlo e usarlo per l’inserimento
			del timecode nel video in output.
			FFMpeg invece non puo leggerlo direttamente in drawtext perchè drawtext può leggere solo:
			- metadata del container
			- metadata del frame (se FFmpeg li mappa)
			- variabili interne (pts, t, n, ecc.)
			- SMPTE timecode presente come metadata timecode
			- un file esterno
			- variabili LTC/ATC tramite asetpts
		Ma il SEI pic_timing di solito non viene esposto come metadata timecode.
		*/

		{
			string textFilePathName;
			// serve un file se
			// - è presente reloadAtFrameInterval
			// - sono presenti caratteri speciali come '
			if (reloadAtFrameInterval > 0 ||
				// caratteri dove non si puo usare escape
				text.find('\'') != string::npos)
				textFilePathName = getDrawTextTemporaryPathName(_ffmpegTempDir, _ingestionJobKey, _encodingJobKey, _outputIndex);

			// in case of file, there is no need of escape
			string escape = textFilePathName.empty() ? "\\" : "";

			// text = regex_replace(text, regex(":"), escape + ":");
			text = StringUtils::replaceAll(text, ":", std::format("{}:", escape));
			// text = regex_replace(text, regex("'"), escape + "'");
			text = StringUtils::replaceAll(text, "'", std::format("{}'", escape));

			if (streamingDurationInSeconds != -1)
			{
				// see https://ffmpeg.org/ffmpeg-filters.html
				// see https://ffmpeg.org/ffmpeg-utils.html
				//
				// expr_int_format, eif
				//	Evaluate the expression’s value and output as formatted integer.
				//	The first argument is the expression to be evaluated, just as for the expr function.
				//	The second argument specifies the output format. Allowed values are ‘x’, ‘X’, ‘d’ and ‘u’. They are treated exactly as in the
				// printf function. 	The third parameter is optional and sets the number of positions taken by the output. It can be used to add
				// padding with zeros from the left.
				//

				{
					// text = regex_replace(
					// 	text, regex("days_counter"), "%{eif" + escape + ":trunc((countDownDurationInSecs-t)/86400)" + escape + ":d" + escape + ":2}"
					// );
					text = StringUtils::replaceAll(text, "days_counter",
						"%{eif" + escape + ":trunc((countDownDurationInSecs-t)/86400)" + escape + ":d" + escape + ":2}"
					);
					// text = regex_replace(
					// 	text, regex("hours_counter"),
					// 	"%{eif" + escape + ":trunc(mod(((countDownDurationInSecs-t)/3600),24))" + escape + ":d" + escape + ":2}"
					// );
					text = StringUtils::replaceAll(text, "hours_counter",
						"%{eif" + escape + ":trunc(mod(((countDownDurationInSecs-t)/3600),24))" + escape + ":d" + escape + ":2}"
					);
					// text = regex_replace(
					// 	text, regex("hours_counter"),
					// 	"%{eif" + escape + ":trunc(mod(((countDownDurationInSecs-t)/3600),24))" + escape + ":d" + escape + ":2}"
					// );
					text = StringUtils::replaceAll(text, "hours_counter",
						"%{eif" + escape + ":trunc(mod(((countDownDurationInSecs-t)/3600),24))" + escape + ":d" + escape + ":2}"
					);
					// text = regex_replace(
					// 	text, regex("mins_counter"),
					// 	"%{eif" + escape + ":trunc(mod(((countDownDurationInSecs-t)/60),60))" + escape + ":d" + escape + ":2}"
					// );
					text = StringUtils::replaceAll(text, "mins_counter",
						"%{eif" + escape + ":trunc(mod(((countDownDurationInSecs-t)/60),60))" + escape + ":d" + escape + ":2}"
					);
					// text = regex_replace(
					// 	text, regex("secs_counter"),
					// 	"%{eif" + escape + ":trunc(mod(countDownDurationInSecs-t" + escape + ",60))" + escape + ":d" + escape + ":2}"
					// );
					text = StringUtils::replaceAll(text, "secs_counter",
						"%{eif" + escape + ":trunc(mod(countDownDurationInSecs-t" + escape + ",60))" + escape + ":d" + escape + ":2}"
					);
					// text = regex_replace(
					// 	text, regex("cents_counter"),
					// 	"%{eif" + escape + ":(mod(countDownDurationInSecs-t" + escape + ",1)*pow(10,2))" + escape + ":d" + escape + ":2}"
					// );
					text = StringUtils::replaceAll(text, "cents_counter",
						"%{eif" + escape + ":(mod(countDownDurationInSecs-t" + escape + ",1)*pow(10,2))" + escape + ":d" + escape + ":2}"
					);
					// text = regex_replace(text, regex("countDownDurationInSecs"), to_string(streamingDurationInSeconds));
					text = StringUtils::replaceAll(text, "countDownDurationInSecs", std::format("{}", streamingDurationInSeconds));
				}
			}

			if (timecode == "editorialTimecode" && text.find(std::format("%{{metadata{}:timecode}}", escape)) == string::npos)
			{
				if (!text.empty())
					text += " ";
				text += std::format("%{{metadata{}:timecode}}", escape);
			}
			else if (timecode == "ptsTimecode" && text.find(std::format("%{{pts{}:", escape)) == string::npos)
			{
				if (!text.empty())
					text += " ";
				// text += "time: %{localtime:%Y-%m-%d %H.%M.%S}";
				// text += "time: %{pts:localtime}";
				// time_t utcTime = chrono::system_clock::to_time_t(chrono::system_clock::now());
				// text += std::format("time: %{{pts:gmtime:{}}}", utcTime);
				text += std::format("%{{pts{}:hms}}", escape);
			}

			if (!textFilePathName.empty())
			{
				// questo file dovrà essere rimosso dallo script di retention
				ofstream of(textFilePathName, ofstream::trunc);
				of << text;
				of.flush();
			}

			/*
			* -vf "drawtext=fontfile='C\:\\Windows\\fonts\\Arial.ttf':
			fontcolor=yellow:fontsize=45:x=100:y=65:
			text='%{eif\:trunc((5447324-t)/86400)\:d\:2} days
			%{eif\:trunc(mod(((5447324-t)/3600),24))\:d\:2} hrs
			%{eif\:trunc(mod(((5447324-t)/60),60))\:d\:2} m
			%{eif\:trunc(mod(5447324-t\,60))\:d\:2} s'"

			* 5447324 is the countdown duration expressed in seconds
			*/
			string ffmpegTextPosition_X_InPixel;
			if (textPosition_X_InPixel == "left")
				ffmpegTextPosition_X_InPixel = "20";
			else if (textPosition_X_InPixel == "center")
				ffmpegTextPosition_X_InPixel = "(w - text_w)/2";
			else if (textPosition_X_InPixel == "right")
				ffmpegTextPosition_X_InPixel = "w - (text_w + 20)";

			// t (timestamp): 0, 1, 2, ...
			else if (textPosition_X_InPixel == "leftToRight_5")
				ffmpegTextPosition_X_InPixel = "(5 * t) - text_w";
			else if (textPosition_X_InPixel == "leftToRight_10")
				ffmpegTextPosition_X_InPixel = "(10 * t) - text_w";
			else if (textPosition_X_InPixel == "loopLeftToRight_5")
				ffmpegTextPosition_X_InPixel = "mod(5 * t\\, w + text_w) - text_w";
			else if (textPosition_X_InPixel == "loopLeftToRight_10")
				ffmpegTextPosition_X_InPixel = "mod(10 * t\\, w + text_w) - text_w";

			// 15 and 30 sono stati decisi usando un video 1920x1080
			else if (textPosition_X_InPixel == "rightToLeft_15")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 15) * t)";
			else if (textPosition_X_InPixel == "rightToLeft_30")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 30) * t)";
			else if (textPosition_X_InPixel == "loopRightToLeft_15")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 15) * mod(t\\, 15))";
			// loopRightToLeft_slow deve essere rimosso
			else if (textPosition_X_InPixel == "loopRightToLeft_slow" || textPosition_X_InPixel == "loopRightToLeft_30")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 30) * mod(t\\, 30))";
			else if (textPosition_X_InPixel == "loopRightToLeft_60")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 60) * mod(t\\, 60))";
			else if (textPosition_X_InPixel == "loopRightToLeft_90")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 90) * mod(t\\, 90))";
			else if (textPosition_X_InPixel == "loopRightToLeft_120")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 120) * mod(t\\, 120))";
			else if (textPosition_X_InPixel == "loopRightToLeft_150")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 150) * mod(t\\, 150))";
			else if (textPosition_X_InPixel == "loopRightToLeft_180")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 180) * mod(t\\, 180))";
			else if (textPosition_X_InPixel == "loopRightToLeft_210")
				ffmpegTextPosition_X_InPixel = "w - (((w + text_w) / 210) * mod(t\\, 210))";
			else
			{
				// ffmpegTextPosition_X_InPixel = regex_replace(textPosition_X_InPixel, regex("video_width"), "w");
				ffmpegTextPosition_X_InPixel = StringUtils::replaceAll(textPosition_X_InPixel, "video_width", "w");
				// ffmpegTextPosition_X_InPixel = regex_replace(ffmpegTextPosition_X_InPixel, regex("text_width"), "text_w"); // text_w or tw
				ffmpegTextPosition_X_InPixel = StringUtils::replaceAll(ffmpegTextPosition_X_InPixel, "text_width", "text_w"); // text_w or tw
				// ffmpegTextPosition_X_InPixel = regex_replace(ffmpegTextPosition_X_InPixel, regex("line_width"), "line_w");
				ffmpegTextPosition_X_InPixel = StringUtils::replaceAll(ffmpegTextPosition_X_InPixel, "line_width", "line_w");
				// ffmpegTextPosition_X_InPixel = regex_replace(ffmpegTextPosition_X_InPixel, regex("timestampInSeconds"), "t");
				ffmpegTextPosition_X_InPixel = StringUtils::replaceAll(ffmpegTextPosition_X_InPixel, "timestampInSeconds", "t");
			}

			string ffmpegTextPosition_Y_InPixel;
			if (textPosition_Y_InPixel == "below")
				ffmpegTextPosition_Y_InPixel = "h - (text_h + 20)";
			else if (textPosition_Y_InPixel == "center")
				ffmpegTextPosition_Y_InPixel = "(h - text_h)/2";
			else if (textPosition_Y_InPixel == "high")
				ffmpegTextPosition_Y_InPixel = "20";

			// t (timestamp): 0, 1, 2, ...
			else if (textPosition_Y_InPixel == "bottomToTop_50")
				ffmpegTextPosition_Y_InPixel = "h - (t * 50)";
			else if (textPosition_Y_InPixel == "bottomToTop_100")
				ffmpegTextPosition_Y_InPixel = "h - (t * 100)";
			else if (textPosition_Y_InPixel == "loopBottomToTop_50")
				ffmpegTextPosition_Y_InPixel = "h - mod(t * 50\\, h)";
			else if (textPosition_Y_InPixel == "loopBottomToTop_100")
				ffmpegTextPosition_Y_InPixel = "h - mod(t * 100\\, h)";

			else if (textPosition_Y_InPixel == "topToBottom_50")
				ffmpegTextPosition_Y_InPixel = "t * 50";
			else if (textPosition_Y_InPixel == "topToBottom_100")
				ffmpegTextPosition_Y_InPixel = "t * 100";
			else if (textPosition_Y_InPixel == "loopTopToBottom_50")
				ffmpegTextPosition_Y_InPixel = "mod(t * 50\\, h)";
			else if (textPosition_Y_InPixel == "loopTopToBottom_100")
				ffmpegTextPosition_Y_InPixel = "mod(t * 100\\, h)";
			else
			{
				// ffmpegTextPosition_Y_InPixel = regex_replace(textPosition_Y_InPixel, regex("video_height"), "h");
				ffmpegTextPosition_Y_InPixel = StringUtils::replaceAll(textPosition_Y_InPixel, "video_height", "h");
				// ffmpegTextPosition_Y_InPixel = regex_replace(ffmpegTextPosition_Y_InPixel, regex("text_height"), "text_h");
				ffmpegTextPosition_Y_InPixel = StringUtils::replaceAll(ffmpegTextPosition_Y_InPixel, "text_height", "text_h");
				// ffmpegTextPosition_Y_InPixel = regex_replace(ffmpegTextPosition_Y_InPixel, regex("line_height"), "line_h");
				ffmpegTextPosition_Y_InPixel = StringUtils::replaceAll(ffmpegTextPosition_Y_InPixel, "line_height", "line_h");
				// ffmpegTextPosition_Y_InPixel = regex_replace(ffmpegTextPosition_Y_InPixel, regex("timestampInSeconds"), "t");
				ffmpegTextPosition_Y_InPixel = StringUtils::replaceAll(ffmpegTextPosition_Y_InPixel, "timestampInSeconds", "t");
			}

			{
				if (!textFilePathName.empty())
				{
					filter = std::format("drawtext=textfile='{}'", textFilePathName);
					if (reloadAtFrameInterval > 0)
						filter += std::format(":reload={}", reloadAtFrameInterval);
				}
				else
					filter = std::format("drawtext=text='{}'", text);
			}
			if (!textPosition_X_InPixel.empty())
				filter += (":x=" + ffmpegTextPosition_X_InPixel);
			if (!textPosition_Y_InPixel.empty())
				filter += (":y=" + ffmpegTextPosition_Y_InPixel);
			if (!fontType.empty())
				filter += (":fontfile='" + _ffmpegTtfFontDir + "/" + fontType + "'");
			if (fontSize != -1)
				filter += (":fontsize=" + to_string(fontSize));
			if (!fontColor.empty())
			{
				filter += (":fontcolor=" + fontColor);
				if (textPercentageOpacity != -1)
				{
					/*
					char opacity[64];

					sprintf(opacity, "%.1f", ((float)textPercentageOpacity) / 100.0);

					filter += ("@" + string(opacity));
					*/
					filter += std::format("@{:.1}", ((float)textPercentageOpacity) / 100.0);
				}
			}
			filter += (":shadowx=" + to_string(shadowX));
			filter += (":shadowy=" + to_string(shadowY));
			if (boxEnable)
			{
				filter += (":box=1");

				if (!boxColor.empty())
				{
					filter += (":boxcolor=" + boxColor);
					if (boxPercentageOpacity != -1)
					{
						/*
						char opacity[64];

						sprintf(opacity, "%.1f", ((float)boxPercentageOpacity) / 100.0);

						filter += ("@" + string(opacity));
						*/
						filter += std::format("@{:.1}", ((float)boxPercentageOpacity) / 100.0);
					}
				}
				if (boxBorderW != -1)
					filter += (":boxborderw=" + to_string(boxBorderW));
			}
		}

		SPDLOG_INFO(
			"getDrawTextVideoFilterDescription"
			", text: {}"
			", textPosition_X_InPixel: {}"
			", textPosition_Y_InPixel: {}"
			", fontType: {}"
			", fontSize: {}"
			", fontColor: {}"
			", textPercentageOpacity: {}"
			", boxEnable: {}"
			", boxColor: {}"
			", boxPercentageOpacity: {}"
			", streamingDurationInSeconds: {}"
			", filter: {}",
			text, textPosition_X_InPixel, textPosition_Y_InPixel, fontType, fontSize, fontColor, textPercentageOpacity, boxEnable, boxColor,
			boxPercentageOpacity, streamingDurationInSeconds, filter
		);
	}
	else if (type == "imageoverlay") // overlay image on video
	{
		string imagePosition_X_InPixel = JSONUtils::asString(filterRoot, "imagePosition_X_InPixel", "0");
		string imagePosition_Y_InPixel = JSONUtils::asString(filterRoot, "imagePosition_Y_InPixel", "0");

		string ffmpegImagePosition_X_InPixel;
		if (imagePosition_X_InPixel == "left")
			ffmpegImagePosition_X_InPixel = "20";
		else if (imagePosition_X_InPixel == "center")
			ffmpegImagePosition_X_InPixel = "(main_w - overlay_w)/2";
		else if (imagePosition_X_InPixel == "right")
			ffmpegImagePosition_X_InPixel = "main_w - (overlay_w + 20)";
		else
		{
			// ffmpegImagePosition_X_InPixel = regex_replace(imagePosition_X_InPixel, regex("video_width"), "main_w");
			ffmpegImagePosition_X_InPixel = StringUtils::replaceAll(imagePosition_X_InPixel, "video_width", "main_w");
			// ffmpegImagePosition_X_InPixel = regex_replace(ffmpegImagePosition_X_InPixel, regex("image_width"), "overlay_w");
			ffmpegImagePosition_X_InPixel = StringUtils::replaceAll(ffmpegImagePosition_X_InPixel, "image_width", "overlay_w");
		}

		string ffmpegImagePosition_Y_InPixel;
		if (imagePosition_Y_InPixel == "below")
			ffmpegImagePosition_Y_InPixel = "main_h - (overlay_h + 20)";
		else if (imagePosition_Y_InPixel == "center")
			ffmpegImagePosition_Y_InPixel = "(main_h - overlay_h)/2";
		else if (imagePosition_Y_InPixel == "high")
			ffmpegImagePosition_Y_InPixel = "20";
		else
		{
			// ffmpegImagePosition_Y_InPixel = regex_replace(imagePosition_Y_InPixel, regex("video_height"), "main_h");
			ffmpegImagePosition_Y_InPixel = StringUtils::replaceAll(imagePosition_Y_InPixel, "video_height", "main_h");
			// ffmpegImagePosition_Y_InPixel = regex_replace(ffmpegImagePosition_Y_InPixel, regex("image_height"), "overlay_h");
			ffmpegImagePosition_Y_InPixel = StringUtils::replaceAll(ffmpegImagePosition_Y_InPixel, "image_height", "overlay_h");
		}

		// overlay=x=main_w-overlay_w-10:y=main_h-overlay_h-10
		filter = std::format("overlay=x={}:y={}", ffmpegImagePosition_X_InPixel, ffmpegImagePosition_Y_InPixel);
	}
	else if (type == "fade")
	{
		int duration = JSONUtils::asInt(filterRoot, "duration", 4);

		if (streamingDurationInSeconds >= duration)
		{
			// fade=type=in:duration=3,fade=type=out:duration=3:start_time=27
			filter = std::format("fade=type=in:duration={},fade=type=out:duration={}:start_time={}",
				duration, duration, streamingDurationInSeconds - duration);
				// ("fade=type=in:duration=" + to_string(duration) + ",fade=type=out:duration=" + to_string(duration) +
				//  ":start_time=" + to_string(streamingDurationInSeconds - duration));
		}
		else
		{
			SPDLOG_WARN(
				"fade filter, streaming duration to small"
				", fadeDuration: {}"
				", streamingDurationInSeconds: {}",
				duration, streamingDurationInSeconds
			);
		}
	}
	else if (type == "fps") // framerate
	{
		int framesNumber = JSONUtils::asInt(filterRoot, "framesNumber", 25);
		int periodInSeconds = JSONUtils::asInt(filterRoot, "periodInSeconds", 1);

		filter = std::format("fps={}/{}", framesNumber, periodInSeconds);
	}
	else if (type == "freezedetect")
	{
		int noiseInDb = JSONUtils::asInt(filterRoot, "noiseInDb", -60);
		int duration = JSONUtils::asInt(filterRoot, "duration", 2);

		filter = std::format("freezedetect=noise={}dB:duration={}", noiseInDb, duration);
	}
	else if (type == "metadata")
	{
		filter = ("metadata=mode=print");
	}
	else if (type == "select") // select frames to pass in output
	{
		string frameType = JSONUtils::asString(filterRoot, "frameType", "i-frame");

		// es: vfr
		string fpsMode = JSONUtils::asString(filterRoot, "fpsMode", "");

		if (frameType == "i-frame")
			filter = "select='eq(pict_type,PICT_TYPE_I)'";
		else if (frameType == "scene")
		{
			// double between 0 and 1. 0.5: 50% of changes
			double changePercentage = JSONUtils::asDouble(filterRoot, "changePercentage", 0.5);

			filter = std::format("select='eq(scene,{})'", changePercentage);
		}
		else
		{
			string errorMessage = "filterRoot->frameType is unknown";
			SPDLOG_ERROR(errorMessage);

			throw runtime_error(errorMessage);
		}

		if (!fpsMode.empty())
			filter += std::format(" -fps_mode {}", fpsMode);
	}
	else if (type == "showinfo")
	{
		filter = ("showinfo");
	}
	else if (type == "silencedetect")
	{
		double noise = JSONUtils::asDouble(filterRoot, "noise", 0.0001);

		filter = ("silencedetect=noise=" + to_string(noise));
	}
	else if (type == "volume")
	{
		double factor = JSONUtils::asDouble(filterRoot, "factor", 5.0);

		filter = std::format("volume={}", factor);
	}
	else
	{
		string errorMessage = std::format(
			"filterRoot->type is unknown"
			", type: {}",
			type
		);
		SPDLOG_ERROR(errorMessage);

		throw runtime_error(errorMessage);
	}

	return filter;
}

json FFMpegFilters::mergeFilters(const json& filters_1Root, const json& filters_2Root)
{

	json mergedFiltersRoot = nullptr;

	if (filters_1Root == nullptr)
		mergedFiltersRoot = filters_2Root;
	else if (filters_2Root == nullptr)
		mergedFiltersRoot = filters_1Root;
	else
	{
		string field = "video";
		{
			if (JSONUtils::isMetadataPresent(filters_1Root, field))
				mergedFiltersRoot[field] = filters_1Root[field];

			if (JSONUtils::isMetadataPresent(filters_2Root, field))
			{
				for (int filterIndex = 0; filterIndex < filters_2Root[field].size(); filterIndex++)
					mergedFiltersRoot[field].push_back(filters_2Root[field][filterIndex]);
			}
		}

		field = "audio";
		{
			if (JSONUtils::isMetadataPresent(filters_1Root, field))
				mergedFiltersRoot[field] = filters_1Root[field];

			if (JSONUtils::isMetadataPresent(filters_2Root, field))
			{
				for (int filterIndex = 0; filterIndex < filters_2Root[field].size(); filterIndex++)
					mergedFiltersRoot[field].push_back(filters_2Root[field][filterIndex]);
			}
		}

		field = "complex";
		{
			if (JSONUtils::isMetadataPresent(filters_1Root, field))
				mergedFiltersRoot[field] = filters_1Root[field];

			if (JSONUtils::isMetadataPresent(filters_2Root, field))
			{
				for (int filterIndex = 0; filterIndex < filters_2Root[field].size(); filterIndex++)
					mergedFiltersRoot[field].push_back(filters_2Root[field][filterIndex]);
			}
		}
	}

	return mergedFiltersRoot;
}

string FFMpegFilters::getDrawTextTemporaryPathName(const string& ffmpegTempDir, int64_t ingestionJobKey, int64_t encodingJobKey, int outputIndex)
{
	if (outputIndex != -1)
		return std::format("{}/{}_{}_{}.overlayText", ffmpegTempDir, ingestionJobKey, encodingJobKey, outputIndex);
	else
		return std::format("{}/{}_{}.overlayText", ffmpegTempDir, ingestionJobKey, encodingJobKey);
}
