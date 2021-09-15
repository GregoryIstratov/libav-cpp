#include <iostream>

#include <av/StreamReader.hpp>
#include <av/StreamWriter.hpp>

// Since it a header only library there is no specific logging backend, so we must implement our own writeLog function
// and place it in av namespace
namespace av
{
void writeLog(LogLevel level, internal::SourceLocation&& loc, std::string msg) noexcept
{
	std::cerr << loc.toString() << ": " << msg << std::endl;
}
}// namespace av

template<typename... Args>
void println(std::string_view fmt, Args&&... args) noexcept
{
	std::cout << av::internal::format(fmt, std::forward<Args>(args)...) << std::endl;
}

template<typename Return>
Return assertExpected(av::Expected<Return>&& expected) noexcept
{
	if (!expected)
	{
		std::cerr << " === Expected failure == \n"
		          << expected.errorString() << std::endl;
		exit(EXIT_FAILURE);
	}

	if constexpr (std::is_same_v<Return, void>)
		return;
	else
		return expected.value();
}

int main(int argc, const char* argv[])
{
	if (argc < 3)
	{
		std::cout << "Usage: transcode <input> <output>" << std::endl;
		return 0;
	}

	std::string_view input(argv[1]);
	std::string_view output(argv[2]);

	av_log_set_level(AV_LOG_VERBOSE);

	auto reader = assertExpected(av::StreamReader::create(input, true));

	av::Frame frame;

	auto writer = assertExpected(av::StreamWriter::create(output));

	{
		auto width     = reader->frameWidth();
		auto height    = reader->frameHeight();
		auto framerate = reader->framerate();
		framerate      = av_inv_q(framerate);
		auto pixFmt    = reader->pixFmt();

		av::OptValueMap codecOpts = {{"preset", "fast"}, {"crf", 29}};

		assertExpected(writer->addVideoStream(AV_CODEC_ID_H264, width, height, pixFmt, framerate, std::move(codecOpts)));
	}

	{
		auto channels = reader->channels();
		auto rate     = reader->sampleRate();
		auto format   = reader->sampleFormat();
		auto bitRate  = 128 * 1024;

		assertExpected(writer->addAudioStream(AV_CODEC_ID_AAC, channels, format, rate, channels, rate, bitRate));
	}

	assertExpected(writer->open());

	int video_n = 0;
	int audio_n = 0;
	for (;;)
	{
		if (!assertExpected(reader->readFrame(frame)))
			break;

		if (frame.type() == AVMEDIA_TYPE_VIDEO)
		{
			if (video_n % 100 == 0)
				println("Writing video {} frame", video_n);

			assertExpected(writer->write(frame, 0));

			video_n++;
		}
		else if (frame.type() == AVMEDIA_TYPE_AUDIO)
		{
			if (audio_n % 100 == 0)
				println("Writing audio {} frame", audio_n);

			assertExpected(writer->write(frame, 1));

			audio_n++;
		}
	}

	println("Encoded {} video {} audio frames", video_n, audio_n);

	return 0;
}
