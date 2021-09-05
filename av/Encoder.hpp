#pragma once

#include "OptSetter.hpp"
#include "common.hpp"

namespace av
{
class Encoder : NoCopyable
{
	explicit Encoder(AVCodecContext* codecContext) noexcept
	    : codecContext_(codecContext)
	{}

public:
	static Expected<Ptr<Encoder>> create(AVCodecID codecId, bool allowHWAccel = true) noexcept
	{
		/* find the encoder */
		//codec_ = avcodec_find_encoder(codecId);

		void* it             = nullptr;
		const AVCodec* codec = nullptr;
		for (;;)
		{
			codec = av_codec_iterate(&it);

			if (!codec)
				RETURN_AV_ERROR("Could not find encoder for '{}'", avcodec_get_name(codecId));

			if (!av_codec_is_encoder(codec))
				continue;

			if (codec->id != codecId)
				continue;

			if (!allowHWAccel && codec->capabilities & AV_CODEC_CAP_HARDWARE)
				continue;

			break;
		}

		auto codecContext = avcodec_alloc_context3(codec);
		if (!codecContext)
			RETURN_AV_ERROR("Could not alloc an encoding context");

		Ptr<Encoder> c{new Encoder(codecContext)};
		c->setGenericDefaultValues();

		return c;
	}

	static Expected<Ptr<Encoder>> create(std::string_view codecName) noexcept
	{
		/* find the encoder */
		auto codec = avcodec_find_encoder_by_name(codecName.data());

		if (!codec)
			RETURN_AV_ERROR("Could not find encoder '{}'", codecName);

		auto codecContext = avcodec_alloc_context3(codec);
		if (!codecContext)
			RETURN_AV_ERROR("Could not alloc an encoding context");

		Ptr<Encoder> c{new Encoder(codecContext)};
		c->setGenericDefaultValues();

		return c;
	}

	~Encoder()
	{
		if (codecContext_)
		{
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

	Expected<void> open() noexcept
	{
		AVDictionary* opts = nullptr;
		auto ret           = avcodec_open2(codecContext_, codecContext_->codec, &opts);
		if (ret < 0)
		{
			avcodec_free_context(&codecContext_);
			RETURN_AV_ERROR("Could not open video codec: {}", avErrorStr(ret));
		}

		return {};
	}

	void setVideoParams(int width, int height, double fps, OptValueMap&& valueMap) noexcept
	{
		auto framerate = av_d2q(1.0 / fps, 100000);
		setVideoParams(width, height, framerate, std::move(valueMap));
	}

	void setVideoParams(int width, int height, AVRational framerate, OptValueMap&& valueMap) noexcept
	{
		/* Resolution must be a multiple of two. */
		codecContext_->width  = width;
		codecContext_->height = height;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
                     * of which frame timestamps are represented. For fixed-fps content,
                     * timebase should be 1/framerate and timestamp increments should be
                     * identical to 1. */
		codecContext_->time_base = framerate;

		codecContext_->bit_rate = 0;
		if (codecContext_->priv_data)
		{
			OptSetter::set(codecContext_->priv_data, valueMap);
		}
	}

	void setAudioParams(int channels, int sampleRate, int bitRate, OptValueMap&& valueMap) noexcept
	{
		/* Resolution must be a multiple of two. */
		codecContext_->channels       = channels;
		codecContext_->channel_layout = av_get_default_channel_layout(channels);
		codecContext_->sample_rate    = sampleRate;
		codecContext_->bit_rate       = bitRate;

		/* Allow the use of the experimental encoder. */
		codecContext_->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
                     * of which frame timestamps are represented. For fixed-fps content,
                     * timebase should be 1/framerate and timestamp increments should be
                     * identical to 1. */

		if (codecContext_->priv_data)
		{
			OptSetter::set(codecContext_->priv_data, valueMap);
		}
	}

	[[nodiscard]] Expected<Ptr<Frame>> newWriteableVideoFrame() const noexcept
	{
		Ptr<Frame> f   = makePtr<Frame>();
		AVFrame* frame = f->native();

		frame->width  = codecContext_->width;
		frame->height = codecContext_->height;
		frame->format = codecContext_->pix_fmt;
		frame->pts    = 0;

		/* allocate the buffers for the frame data */
		auto ret = av_frame_get_buffer(frame, 0);
		if (ret < 0)
			RETURN_AV_ERROR("Could not allocate frame data: {}", avErrorStr(ret));

		ret = av_frame_make_writable(frame);
		if (ret < 0)
			RETURN_AV_ERROR("Could not make frame writable: {}", avErrorStr(ret));

		return f;
	}

	[[nodiscard]] Expected<Ptr<Frame>> newWriteableAudioFrame() const noexcept
	{
		auto f     = makePtr<Frame>();
		auto frame = f->native();

		frame->channel_layout = codecContext_->channel_layout;
		//frame->nb_samples = 1024;
		frame->sample_rate = codecContext_->sample_rate;
		frame->format      = codecContext_->sample_fmt;
		frame->pts         = 0;
		frame->pkt_dts     = 0;

		return f;
	}

	std::tuple<Result, int> encodeFrame(Frame& frame, std::vector<Packet>& packets) noexcept
	{
		if (!sendFrame(*frame))
			return {Result::kFail, 0};

		return receivePackets(packets);
	}

	std::tuple<Result, int> flush(std::vector<Packet>& packets) noexcept
	{
		if (!sendFrame(nullptr))
			return {Result::kFail, 0};

		return receivePackets(packets);
	}

private:
	bool sendFrame(AVFrame* frame) noexcept
	{
		// send the frame to the encoder
		auto err = avcodec_send_frame(codecContext_, frame);
		if (err < 0)
		{
			LOG_AV_ERROR("Error sending a frame to the encoder: {}", avErrorStr(err));
			return false;
		}

		return true;
	}

	std::tuple<Result, int> receivePackets(std::vector<Packet>& packets) noexcept
	{
		for (auto& pkt : packets)
			pkt.dataUnref();

		for (int i = 0;; ++i)
		{
			if (i >= packets.size())
			{
				packets.emplace_back();
			}

			auto err = avcodec_receive_packet(codecContext_, *packets[i]);
			if (err == AVERROR(EAGAIN))
				return {Result::kSuccess, i};

			if (err == AVERROR_EOF)
				return {Result::kEOF, i};

			if (err < 0)
			{
				LOG_AV_ERROR("Codec packet receive error: {}", avErrorStr(err));
				return {Result::kFail, i};
			}
		}
	}

	void setGenericDefaultValues() noexcept
	{
		switch (codecContext_->codec->type)
		{
			case AVMEDIA_TYPE_AUDIO:
				codecContext_->sample_fmt  = codecContext_->codec->sample_fmts ? codecContext_->codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
				codecContext_->bit_rate    = 64000;
				codecContext_->sample_rate = 44100;
				if (codecContext_->codec->supported_samplerates)
				{
					codecContext_->sample_rate = codecContext_->codec->supported_samplerates[0];
					for (int i = 0; codecContext_->codec->supported_samplerates[i]; i++)
					{
						if (codecContext_->codec->supported_samplerates[i] == 44100)
							codecContext_->sample_rate = 44100;
					}
				}
				codecContext_->channels       = av_get_channel_layout_nb_channels(codecContext_->channel_layout);
				codecContext_->channel_layout = AV_CH_LAYOUT_STEREO;
				if (codecContext_->codec->channel_layouts)
				{
					codecContext_->channel_layout = codecContext_->codec->channel_layouts[0];
					for (int i = 0; codecContext_->codec->channel_layouts[i]; i++)
					{
						if (codecContext_->codec->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
							codecContext_->channel_layout = AV_CH_LAYOUT_STEREO;
					}
				}
				codecContext_->channels = av_get_channel_layout_nb_channels(codecContext_->channel_layout);
				break;

			case AVMEDIA_TYPE_VIDEO:
				codecContext_->gop_size = 12; /* emit one intra frame every twelve frames at most */
				codecContext_->pix_fmt  = codecContext_->codec->pix_fmts ? codecContext_->codec->pix_fmts[0] : AV_PIX_FMT_YUV420P;
				if (codecContext_->codec_id == AV_CODEC_ID_MPEG2VIDEO)
				{
					/* just for testing, we also add B-frames */
					codecContext_->max_b_frames = 2;
				}
				if (codecContext_->codec_id == AV_CODEC_ID_MPEG1VIDEO)
				{
					/* Needed to avoid using macroblocks in which some coeffs overflow.
                         * This does not happen with normal video, it just happens here as
                         * the motion of the chroma plane does not match the luma plane. */
					codecContext_->mb_decision = 2;
				}
				/* Some settings for libx264 encoding, restore dummy values for gop_size
                   and qmin since they will be set to reasonable defaults by the libx264
                   preset system. Also, use a crf encode with the default quality rating,
                   this seems easier than finding an appropriate default bitrate. */
				if (codecContext_->codec_id == AV_CODEC_ID_H264 || codecContext_->codec_id == AV_CODEC_ID_HEVC)
				{
					codecContext_->gop_size = -1;

					codecContext_->qmin = -1;

					codecContext_->bit_rate = 0;
					if (codecContext_->priv_data) {
						//av_opt_set(codecContext_->priv_data, "crf", "23", 0);
						//                        av_opt_set(codecContext_->priv_data, "preset", "ultrafast", 0);
						//                        av_opt_set(codecContext_->priv_data, "tune", "zerolatency", 0);
					}
				}
				break;

			default:
				break;
		}
	}

private:
	AVCodecContext* codecContext_{nullptr};
};

}// namespace av
