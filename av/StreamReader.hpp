#pragma once

#include <av/Decoder.hpp>
#include <av/Frame.hpp>
#include <av/InputFormat.hpp>
#include <av/common.hpp>

namespace av
{

class StreamReader : NoCopyable
{
	StreamReader() = default;

public:
	static Expected<Ptr<StreamReader>> create(std::string_view url, bool enableAudio = false) noexcept
	{
		Ptr<StreamReader> sr{new StreamReader};

		auto iformExp = SimpleInputFormat::create(url, enableAudio);
		if (!iformExp)
			FORWARD_AV_ERROR(iformExp);

		sr->ic_ = iformExp.value();

		sr->vStream_ = sr->ic_->videoStream();

		if (enableAudio)
			sr->aStream_ = sr->ic_->audioStream();

		return sr;
	}

	~StreamReader()
	{
	}

	[[nodiscard]] Expected<bool> readFrame(Frame& frame) noexcept
	{
		Packet packet;

		for (;;)
		{
			packet.dataUnref();
			auto successExp = ic_->readFrame(packet);
			if (!successExp)
				FORWARD_AV_ERROR(successExp);

			if (!successExp.value())
				return false;

			Ptr<Decoder> dec;
			if (packet.native()->stream_index == std::get<0>(vStream_)->index)
			{
				dec = std::get<1>(vStream_);

				auto resExp = dec->decode(packet, frame);

				if (!resExp)
					FORWARD_AV_ERROR(resExp);

				if (resExp.value() != Result::kSuccess)
					continue;

				frame.type(AVMEDIA_TYPE_VIDEO);

				return true;
			}
			else if (std::get<0>(aStream_) && packet.native()->stream_index == std::get<0>(aStream_)->index)
			{
				dec = std::get<1>(aStream_);

				auto resExp = dec->decode(packet, frame);

				if (!resExp)
					FORWARD_AV_ERROR(resExp);

				if (resExp.value() != Result::kSuccess)
					continue;

				frame.type(AVMEDIA_TYPE_AUDIO);

				return true;
			}
			else
			{
				//LOG_ERROR("Unknown stream index {}", packet->stream_index);
				continue;
			}
		}
	}

	auto pixFmt() const noexcept
	{
		return std::get<1>(vStream_)->native()->pix_fmt;
	}

	auto frameWidth() const noexcept
	{
		return std::get<1>(vStream_)->native()->width;
	}

	auto frameHeight() const noexcept
	{
		return std::get<1>(vStream_)->native()->height;
	}

	auto framerate() const noexcept
	{
		return std::get<1>(vStream_)->native()->framerate;
	}

	auto channels() const noexcept
	{
		return std::get<1>(aStream_)->native()->channels;
	}

	auto sampleRate() const noexcept
	{
		return std::get<1>(aStream_)->native()->sample_rate;
	}

	auto sampleFormat() const noexcept
	{
		return std::get<1>(aStream_)->native()->sample_fmt;
	}

private:
	Ptr<SimpleInputFormat> ic_;
	std::tuple<AVStream*, Ptr<Decoder>> vStream_;
	std::tuple<AVStream*, Ptr<Decoder>> aStream_;
};

}// namespace av
