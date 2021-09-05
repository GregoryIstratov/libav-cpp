# libav-cpp
FFmpeg C++ wrapper

This is a header only, exceptionless simple C++ FFmpeg wrapper.
Native FFmpeg C library has load of clumsy and excessive functions you need to call to be able to do something in it, 
it's very verbosy and you may forget to free some resources and eventually get a memory leak or forget to initialize something and get segfault, etc ...

Internally I use ffmpeg in my work projects, and until now it was done via opencv and half-reworked ffmpeg wrappers ripped out of opencv sources which are light years far from ideal C++ implementation and looks more like C with classes than real c++ code with error handling ready for production.

The goal of this library is to make using ffmpeg in C++ safe, comfy and simplier than it's done in FFmpeg C API.
So I hope this code will be helpful for someone and if you want to contribute you're welcome.

The library doesn't use exceptions at all, it uses custom implementation of Expected<T> for error handling. 
All members and functions are marked as noexcept ( tbh it uses std containers which in theory may throw std::bad_alloc or some terrible things, but have you ever met out of memory and do you really need to handle this? Especially if you run your software on servers with tons of ram and where software typically relauches automatically on fails. )

No more words to say, just take a look at transocding example!

```C++
#include <iostream>

#include <av/StreamReader.hpp>
#include <av/StreamWriter.hpp>

// Since it a header only library there is no specific logging backend, so we must implement our own writeLog function
// and place it in av namespace
namespace av
{
template<typename... Args>
void writeLog(LogLevel level, internal::SourceLocation&& loc, std::string_view fmt, Args&&... args) noexcept
{
    std::cerr << loc.toString() << ": " << av::internal::format(fmt, std::forward<Args>(args)...) << std::endl;
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

    auto width     = reader->frameWidth();
    auto height    = reader->frameHeight();
    auto framerate = reader->framerate();
    framerate      = av_inv_q(framerate);
    auto pixFmt    = reader->pixFmt();

    av::OptValueMap codecOpts = {{"preset", "fast"}, {"crf", 29}};

    assertExpected(writer->addVideoStream(AV_CODEC_ID_H264, width, height, 
                                          pixFmt, framerate, std::move(codecOpts)));

    auto channels = reader->channels();
    auto rate     = reader->sampleRate();
    auto format   = reader->sampleFormat();
    auto bitRate  = 128 * 1024;

    assertExpected(writer->addAudioStream(AV_CODEC_ID_AAC, channels, format, 
                                          rate, channels, rate, bitRate));

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
```
