#pragma once

#include "Encoder.hpp"
#include "Frame.hpp"
#include "OptSetter.hpp"
#include "OutputFormat.hpp"
#include "Resample.hpp"
#include "Scale.hpp"
#include "common.hpp"

namespace av
{
class StreamWriter : NoCopyable
{
	StreamWriter() = default;

public:
	[[nodiscard]] static Expected<Ptr<StreamWriter>> create(std::string_view filename) noexcept
	{
		Ptr<StreamWriter> sw{new StreamWriter};
		sw->filename_ = filename;

		auto fcExp = OutputFormat::create(filename);
		if (!fcExp)
			FORWARD_AV_ERROR(fcExp);

		sw->formatContext_ = fcExp.value();

		return sw;
	}

	~StreamWriter()
	{
		flushAllStreams();
	}

	[[nodiscard]] Expected<void> open() noexcept
	{
		return formatContext_->open(filename_);
	}

	[[nodiscard]] Expected<int> addVideoStream(std::variant<AVCodecID, std::string_view> codecName, int inWidth, int inHeight, AVPixelFormat inPixFmt, AVRational frameRate, int outWidth, int outHeight, OptValueMap&& codecParams = {}) noexcept
	{
		auto stream  = makePtr<Stream>();
		stream->type = AVMEDIA_TYPE_VIDEO;

		auto expc = std::visit([](auto&& v) { return Encoder::create(v); }, codecName);

		if (!expc)
			FORWARD_AV_ERROR(expc);

		Ptr<Encoder> c = expc.value();

		c->setVideoParams(outWidth, outHeight, frameRate, std::move(codecParams));
		auto cOpenEXp = c->open();
		if (!cOpenEXp)
			FORWARD_AV_ERROR(cOpenEXp);

		auto frameExp = c->newWriteableVideoFrame();
		if (!frameExp)
			FORWARD_AV_ERROR(frameExp);

		stream->frame   = frameExp.value();
		stream->encoder = c;

		auto swsExp = Scale::create(inWidth, inHeight, inPixFmt, outWidth, outHeight, c->native()->pix_fmt);
		if (!swsExp)
			FORWARD_AV_ERROR(swsExp);

		stream->sws = swsExp.value();

		auto sIndExp = formatContext_->addStream(c);
		if (!sIndExp)
			FORWARD_AV_ERROR(sIndExp);

		stream->index = sIndExp.value();
		int index     = stream->index;

		streams_.emplace_back(std::move(stream));

		if (streams_.size() - 1 != index)
			RETURN_AV_ERROR("Stream index {} != streams count - 1 {}", index, streams_.size() - 1);

		const AVCodec* codec = c->native()->codec;
		LOG_AV_INFO("Added video stream #{} codec: {} {}x{} {} fps", index, codec->long_name, c->native()->width, c->native()->height, av_q2d(av_inv_q(c->native()->time_base)));

		return index;
	}

	[[nodiscard]] Expected<int> addVideoStream(std::variant<AVCodecID, std::string_view> codecName, int inWidth, int inHeight, AVPixelFormat inPixFmt, AVRational frameRate, OptValueMap&& codecParams = {})
	{
		return addVideoStream(codecName, inWidth, inHeight, inPixFmt, frameRate, inWidth, inHeight, std::move(codecParams));
	}

	[[nodiscard]] Expected<int> addAudioStream(std::variant<AVCodecID, std::string_view> codecName, int inChannels, AVSampleFormat inSampleFmt, int inSampleRate,
	                                           int outChannels, int outSampleRate, int outBitRate, OptValueMap&& codecParams = {}) noexcept
	{
		auto stream  = makePtr<Stream>();
		stream->type = AVMEDIA_TYPE_AUDIO;

		auto expc = std::visit([](auto&& v) { return Encoder::create(v); }, codecName);

		if (!expc)
			FORWARD_AV_ERROR(expc);

		Ptr<Encoder> c = expc.value();

#if 0
		Ptr<Encoder> c;
		if(std::holds_alternative<AVCodecID>(codecName))
			c = makePtr<Encoder>(std::get<AVCodecID>(codecName));
		else
			c = makePtr<Encoder>(std::get<std::string_view>(codecName));
#endif
		c->setAudioParams(outChannels, outSampleRate, outBitRate, std::move(codecParams));
		auto cOpenExp = c->open();
		if (!cOpenExp)
			FORWARD_AV_ERROR(cOpenExp);

		auto frameExp = c->newWriteableAudioFrame();
		if (!frameExp)
			FORWARD_AV_ERROR(frameExp);

		stream->frame   = frameExp.value();
		stream->encoder = c;

		auto swrExp = Resample::create(inChannels, inSampleFmt, inSampleRate, outChannels, c->native()->sample_fmt, outSampleRate);
		if (!swrExp)
			FORWARD_AV_ERROR(swrExp);

		stream->swr = swrExp.value();

		auto sIndExp = formatContext_->addStream(c);
		if (!sIndExp)
			FORWARD_AV_ERROR(sIndExp);

		stream->index = sIndExp.value();
		int index     = stream->index;

		streams_.emplace_back(std::move(stream));

		if (streams_.size() - 1 != index)
			RETURN_AV_ERROR("Stream index {} != streams count - 1 {}", index, streams_.size() - 1);

		const AVCodec* codec = c->native()->codec;
		LOG_AV_INFO("Added audio stream #{} codec: {} {} Hz {} channels", index, codec->long_name, c->native()->sample_rate, c->native()->channels);

		return index;
	}

	[[nodiscard]] Expected<void> write(Frame& frame, int streamIndex) noexcept
	{
		auto& stream = streams_[streamIndex];

		if (stream->type == AVMEDIA_TYPE_VIDEO)
		{
			stream->sws->scale(frame, *stream->frame);
			stream->frame->native()->pts = stream->nextPts++;
		}
		else if (stream->type == AVMEDIA_TYPE_AUDIO)
		{
			stream->swr->convert(frame, *stream->frame);
			stream->frame->native()->pts = stream->nextPts;
			stream->nextPts += stream->frame->native()->nb_samples;
		}
		else
			RETURN_AV_ERROR("Unsupported/unknown stream type: {}", av_get_media_type_string(stream->type));

		auto [res, sz] = stream->encoder->encodeFrame(*stream->frame, stream->packets);

		if (res == Result::kFail)
			RETURN_AV_ERROR("Encoder returned failure");

		for (int i = 0; i < sz; ++i)
		{
			auto expected = formatContext_->writePacket(stream->packets[i], stream->index);
			if (!expected)
				LOG_AV_ERROR(expected.errorString());
		}

		return {};
	}

	void flushStream(int streamIndex) noexcept
	{
		auto& stream = streams_[streamIndex];

		if (stream->flushed)
			return;

		auto [res, sz]  = stream->encoder->flush(stream->packets);
		stream->flushed = true;

		if (res == Result::kFail)
			return;

		for (int i = 0; i < sz; ++i)
		{
			auto expected = formatContext_->writePacket(stream->packets[i], stream->index);
			if (!expected)
				LOG_AV_ERROR(expected.errorString());
		}
	}

	void flushAllStreams() noexcept
	{
		for (auto& stream : streams_)
		{
			flushStream(stream->index);
		}
	}

private:
	struct Stream
	{
		AVMediaType type{AVMEDIA_TYPE_UNKNOWN};
		int index{-1};
		Ptr<Encoder> encoder;
		Ptr<Scale> sws;
		Ptr<Resample> swr;
		Ptr<Frame> frame;
		std::vector<Packet> packets;
		int nextPts{0};
		int sampleCount{0};
		bool flushed{false};
	};

private:
	std::string filename_;
	std::vector<Ptr<Stream>> streams_;
	Ptr<OutputFormat> formatContext_;
};

}// namespace av
