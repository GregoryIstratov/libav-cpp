#pragma once

#include <av/Decoder.hpp>
#include <av/Frame.hpp>
#include <av/InputFormat.hpp>
#include <av/Scale.hpp>
#include <av/common.hpp>

namespace av
{

struct VideoCaptureParams
{
	std::string url;
	bool rawMode{false};
	bool useSEITimestamps{false};
	int targetFrameWidth{0};
	int targetFrameHeight{0};
};

class VideoCapture : NoCopyable
{
	VideoCapture() = default;

public:
	static Expected<Ptr<VideoCapture>> create(const VideoCaptureParams& params) noexcept
	{
		Ptr<VideoCapture> sr{new VideoCapture};
		sr->params_ = params;

		auto err            = avformat_open_input(&sr->ic_, sr->params_.url.data(), nullptr, nullptr);
		if (err < 0)
			RETURN_AV_ERROR("Cannot open input '{}': {}", sr->params_.url, avErrorStr(err));

		err = avformat_find_stream_info(sr->ic_, nullptr);
		if (err < 0)
			RETURN_AV_ERROR("Cannot find stream info: {}", avErrorStr(err));

		{
			auto ret = sr->findBestStream();
			if (!ret)
				FORWARD_AV_ERROR(ret);
		}

		av_dump_format(sr->ic_, 0, nullptr, 0);

		return sr;
	}

	~VideoCapture()
	{
		if(ic_)
			avformat_close_input(&ic_);
	}

	Expected<bool> readFrameRaw(Packet& packet) noexcept
	{
		for(;;)
		{
			packet.dataUnref();
			auto r = readNextFramePacket(packet);
			if(!r)
				FORWARD_AV_ERROR(r);

			if(!r.value())
				return false;

			if (packet.native()->stream_index != stream_->index)
				continue;

			return true;
		}
	}

	[[nodiscard]] Expected<bool> readFrame(cv::Mat& mat) noexcept
	{
		if(!params_.rawMode)
		{
			Frame frame;

			{
				auto e = readFrameDecoded(frame);
				if (!e)
					FORWARD_AV_ERROR(e);

				if (!e.value())
					return false;
			}

			scale_->scale(frame, *swsFrame_);

			const auto data = swsFrame_->native()->data[0];
			const auto step = swsFrame_->native()->linesize[0];

			try
			{
				const cv::Mat tmp(targetFrameHeight(), targetFameWidth(), CV_MAKETYPE(CV_8U, 3), data, step);
				tmp.copyTo(mat);
			}
			catch (const std::exception& e)
			{
				RETURN_AV_ERROR("opencv exception: {}", e.what());
			}

			return true;
		}
		else
		{
			Packet pkt;
			auto e = readFrameRaw(pkt);
			if(!e)
				FORWARD_AV_ERROR(e);

			if(!e.value())
				return false;

			try
			{
				const auto data = pkt.native()->data;
				const auto step = pkt.native()->size;
				const cv::Mat tmp(1, pkt.native()->size, CV_MAKETYPE(CV_8U, 1), data, step);
				tmp.copyTo(mat);
			}
			catch (const std::exception& e)
			{
				RETURN_AV_ERROR("opencv exception: {}", e.what());
			}

			return true;
		}
	}

	auto pixFmt() const noexcept
	{
		return (AVPixelFormat)stream_->codecpar->format;
	}

	int nativeFrameWidth() const noexcept
	{
		return stream_->codecpar->width;
	}

	int nativeFrameHeight() const noexcept
	{
		return stream_->codecpar->height;
	}
	int targetFameWidth() const noexcept
	{
		return params_.targetFrameWidth;
	}

	int targetFrameHeight() const noexcept
	{
		return params_.targetFrameHeight;
	}

	AVRational framerate() const noexcept
	{
		return framerate_;
	}

private:
	Expected<bool> readNextFramePacket(Packet& packet) noexcept
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

	[[nodiscard]] Expected<bool> readFrameDecoded(Frame& frame) noexcept
	{
		Packet packet;

		for (;;)
		{
			packet.dataUnref();

			auto successExp = readFrameRaw(packet);
			if (!successExp)
				FORWARD_AV_ERROR(successExp);

			auto resExp = decoder_->decode(packet, frame);

			if (!resExp)
				FORWARD_AV_ERROR(resExp);

			if(resExp.value() == Result::kEOF)
				return false;

			if (resExp.value() != Result::kSuccess)
				continue;

			frame.type(AVMEDIA_TYPE_VIDEO);

			return true;

		}
	}

	Expected<void> findBestStream() noexcept
	{
		AVCodec* dec = nullptr;
		int stream_i = AVERROR_STREAM_NOT_FOUND;

		if(!params_.rawMode)
			stream_i = av_find_best_stream(ic_, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
		else
			stream_i = av_find_best_stream(ic_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

		if (stream_i == AVERROR_STREAM_NOT_FOUND)
			RETURN_AV_ERROR("Failed to find {} stream in '{}'", av_get_media_type_string(AVMEDIA_TYPE_VIDEO), params_.url);


		if (!params_.rawMode && stream_i == AVERROR_DECODER_NOT_FOUND)
			RETURN_AV_ERROR("Failed to find decoder '{}' of '{}'", avcodec_get_name(ic_->streams[stream_i]->codecpar->codec_id), params_.url);


		framerate_ = av_guess_frame_rate(ic_, ic_->streams[stream_i], nullptr);

		stream_ = ic_->streams[stream_i];

		if(!params_.rawMode)
		{
			auto decContext = Decoder::create(dec, ic_->streams[stream_i], framerate_);

			if (!decContext)
				FORWARD_AV_ERROR(decContext);

			decoder_ = decContext.value();

			params_.targetFrameWidth = params_.targetFrameWidth > 0 ? params_.targetFrameWidth : nativeFrameWidth();
			params_.targetFrameHeight = params_.targetFrameHeight > 0 ? params_.targetFrameHeight : nativeFrameHeight();

			auto scaleExp = Scale::create(decoder_->native()->coded_width, decoder_->native()->coded_height,
			                              pixFmt(), params_.targetFrameWidth, params_.targetFrameHeight, AV_PIX_FMT_RGB24);

			if(!scaleExp)
				FORWARD_AV_ERROR(scaleExp);

			scale_ = scaleExp.value();

			auto frameExp = Frame::create(params_.targetFrameWidth, params_.targetFrameHeight, AV_PIX_FMT_RGB24);
			if(!frameExp)
				FORWARD_AV_ERROR(frameExp);

			swsFrame_ = frameExp.value();
		}

		return {};
	}

private:
	VideoCaptureParams params_;
	AVFormatContext* ic_{nullptr};
	AVStream* stream_{nullptr};
	AVRational framerate_{};
	Ptr<Decoder> decoder_;
	Ptr<Scale> scale_;
	Ptr<Frame> swsFrame_;
};

}
