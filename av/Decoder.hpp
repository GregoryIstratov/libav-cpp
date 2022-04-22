#pragma once

#include <av/Frame.hpp>
#include <av/Packet.hpp>
#include <av/common.hpp>

namespace av
{

class Decoder : NoCopyable
{
	explicit Decoder(AVCodecContext* codecContext) noexcept
	    : codecContext_(codecContext)
	{}

public:
	static Expected<Ptr<Decoder>> create(const AVCodec* codec, AVStream* stream, AVRational framerate = {})
	{
		if (!av_codec_is_decoder(codec))
			RETURN_AV_ERROR("{} is not a decoder", codec->name);

		auto codecContext = avcodec_alloc_context3(codec);
		if (!codecContext)
			RETURN_AV_ERROR("Could not alloc an encoding context");

		auto ret = avcodec_parameters_to_context(codecContext, stream->codecpar);
		if (ret < 0)
		{
			avcodec_free_context(&codecContext);
			RETURN_AV_ERROR("Failed to copy parameters to context: {}", avErrorStr(ret));
		}

		if (codecContext->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			if (!framerate.num && !framerate.den)
			{
				avcodec_free_context(&codecContext);
				RETURN_AV_ERROR("Framerate is not set");
			}

			codecContext->framerate = framerate;
		}

		AVDictionary* opts = nullptr;
		ret                = avcodec_open2(codecContext, codecContext->codec, &opts);
		if (ret < 0)
		{
			avcodec_free_context(&codecContext);
			RETURN_AV_ERROR("Could not open video codec: {}", avErrorStr(ret));
		}

		return Ptr<Decoder>{new Decoder{codecContext}};
	}

	~Decoder()
	{
		if (codecContext_)
		{
			avcodec_close(codecContext_);
			avcodec_free_context(&codecContext_);
		}
	}

	auto* operator*() noexcept
	{
		return codecContext_;
	}
	const auto* operator*() const noexcept
	{
		return codecContext_;
	}

	auto* native() noexcept
	{
		return codecContext_;
	}
	const auto* native() const noexcept
	{
		return codecContext_;
	}

	Expected<Result> decode(Packet& packet, Frame& frame) noexcept
	{
		int err = avcodec_send_packet(codecContext_, *packet);

		if (err == AVERROR(EAGAIN))
			return Result::kEAGAIN;

		if (err == AVERROR_EOF)
			return Result::kEOF;

		if (err < 0)
			RETURN_AV_ERROR("Decoder error: {}", avErrorStr(err));

		err = avcodec_receive_frame(codecContext_, *frame);

		if (err == AVERROR(EAGAIN))
			return Result::kEAGAIN;

		if (err == AVERROR_EOF)
			return Result::kEOF;

		if (err < 0)
			RETURN_AV_ERROR("Decoder error: {}", avErrorStr(err));

		return Result::kSuccess;
	}

private:
	AVCodecContext* codecContext_{nullptr};
};

}// namespace av
