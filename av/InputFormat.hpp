#pragma once

#include "Decoder.hpp"
#include "Packet.hpp"
#include "common.hpp"

namespace av
{

class SimpleInputFormat : NoCopyable
{
	explicit SimpleInputFormat(AVFormatContext* ic) noexcept
	    : ic_(ic)
	{}

public:
	static Expected<Ptr<SimpleInputFormat>> create(std::string_view url, bool enableAudio = false) noexcept
	{
		AVFormatContext* ic = nullptr;
		auto err            = avformat_open_input(&ic, url.data(), nullptr, nullptr);
		if (err < 0)
			RETURN_AV_ERROR("Cannot open input '{}': {}", url, avErrorStr(err));

		err = avformat_find_stream_info(ic, nullptr);
		if (err < 0)
		{
			avformat_close_input(&ic);
			RETURN_AV_ERROR("Cannot find stream info: {}", avErrorStr(err));
		}

		Ptr<SimpleInputFormat> res{new SimpleInputFormat{ic}};
		res->url_ = url;

		{
			auto ret = res->findBestStream(AVMEDIA_TYPE_VIDEO);
			if (!ret)
				FORWARD_AV_ERROR(ret);
		}

		if (enableAudio)
		{
			auto ret = res->findBestStream(AVMEDIA_TYPE_AUDIO);
			if (!ret)
				FORWARD_AV_ERROR(ret);
		}

		av_dump_format(ic, 0, nullptr, 0);

		return res;
	}

	~SimpleInputFormat()
	{
		avformat_close_input(&ic_);
	}

	Expected<bool> readFrame(Packet& packet) noexcept
	{
		int err = 0;
		for (;;)
		{
			err = av_read_frame(ic_, *packet);

			if (err == AVERROR(EAGAIN))
				continue;

			if (err == AVERROR_EOF)
			{
				// flush cached frames from video decoder
				packet.native()->data = nullptr;
				packet.native()->size = 0;

				return false;
			}

			if (err < 0)
				RETURN_AV_ERROR("Failed to read frame: {}", avErrorStr(err));

			return true;
		}
	}

	auto& videoStream() noexcept
	{
		return vStream_;
	}
	auto& audioStream() noexcept
	{
		return aStream_;
	}

private:
	Expected<void> findBestStream(AVMediaType type) noexcept
	{
		AVCodec* dec = nullptr;
		int stream_i = av_find_best_stream(ic_, type, -1, -1, &dec, 0);
		if (stream_i == AVERROR_STREAM_NOT_FOUND)
			RETURN_AV_ERROR("Failed to find {} stream in '{}'", av_get_media_type_string(type), url_);
		if (stream_i == AVERROR_DECODER_NOT_FOUND)
			RETURN_AV_ERROR("Failed to find decoder '{}' of '{}'", avcodec_get_name(ic_->streams[stream_i]->codecpar->codec_id), url_);

		if (type == AVMEDIA_TYPE_VIDEO)
		{
			const auto framerate = av_guess_frame_rate(ic_, ic_->streams[stream_i], nullptr);
			auto decContext      = Decoder::create(dec, ic_->streams[stream_i], framerate);

			if (!decContext)
				FORWARD_AV_ERROR(decContext);

			std::get<0>(vStream_) = ic_->streams[stream_i];
			std::get<1>(vStream_) = decContext.value();
		}
		else if (type == AVMEDIA_TYPE_AUDIO)
		{
			auto decContext = Decoder::create(dec, ic_->streams[stream_i]);

			if (!decContext)
				FORWARD_AV_ERROR(decContext);

			std::get<0>(aStream_) = ic_->streams[stream_i];
			std::get<1>(aStream_) = decContext.value();
		}
		else
			RETURN_AV_ERROR("Not supported stream type '{}'", av_get_media_type_string(type));

		return {};
	}

private:
	std::string url_;
	AVFormatContext* ic_{nullptr};
	std::tuple<AVStream*, Ptr<Decoder>> vStream_;
	std::tuple<AVStream*, Ptr<Decoder>> aStream_;
};

}// namespace av
