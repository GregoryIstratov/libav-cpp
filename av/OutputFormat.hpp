#pragma once

#include <av/Encoder.hpp>
#include <av/common.hpp>

namespace av
{

class OutputFormat : NoCopyable
{
	explicit OutputFormat(AVFormatContext* oc) noexcept
	    : oc_(oc)
	{}

public:
	static Expected<Ptr<OutputFormat>> create(std::string_view filename, std::string_view formatName = {}) noexcept
	{
		AVFormatContext* oc = nullptr;
		int err             = avformat_alloc_output_context2(&oc, nullptr, formatName.empty() ? nullptr : formatName.data(), filename.data());
		if (!oc || err < 0)
			RETURN_AV_ERROR("Failed to create output format context: {}", avErrorStr(err));

		return Ptr<OutputFormat>(new OutputFormat(oc));
	}

	~OutputFormat()
	{
		if (oc_)
		{
			if (oc_->pb)
			{
				auto err = av_write_trailer(oc_);
				if (err < 0)
					LOG_AV_ERROR("Failed to write format trailer: {}", avErrorStr(err));

				avio_close(oc_->pb);
			}

			avformat_free_context(oc_);
		}
	}

	auto* operator*() noexcept
	{
		return oc_;
	}
	const auto* operator*() const noexcept
	{
		return oc_;
	}

	auto* native() noexcept
	{
		return oc_;
	}
	const auto* native() const noexcept
	{
		return oc_;
	}

	// should be called before open()
	[[nodiscard]] Expected<int> addStream(Ptr<Encoder>& codecContext) noexcept
	{
		auto stream = avformat_new_stream(oc_, nullptr);
		if (!stream)
			RETURN_AV_ERROR("Failed to create new stream");

		/* copy the stream parameters to the muxer */
		auto ret = avcodec_parameters_from_context(stream->codecpar, **codecContext);
		if (ret < 0)
			RETURN_AV_ERROR("Could not copy the stream parameters: {}", avErrorStr(ret));

		stream->id        = (int) oc_->nb_streams - 1;
		stream->time_base = codecContext->native()->time_base;

		/* Some formats want stream headers to be separate. */
		if (oc_->oformat->flags & AVFMT_GLOBALHEADER)
			codecContext->native()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		streams_.emplace_back(std::tuple{stream, codecContext});

		int ind = (int) streams_.size() - 1;
		return ind;
	}

	[[nodiscard]] Expected<void> open(std::string_view filename, int ioFlags = AVIO_FLAG_WRITE) noexcept
	{
		if (oc_->oformat->flags & AVFMT_NOFILE)
			RETURN_AV_ERROR("Failed to open avio context. Format context already associated with file.");

		auto err = avio_open(&oc_->pb, filename.data(), ioFlags);
		if (err < 0)
			RETURN_AV_ERROR("Failed to open io context for '{}': {}", filename, err);

		AVDictionary* opts = nullptr;
		err                = avformat_write_header(oc_, &opts);
		if (err < 0)
			RETURN_AV_ERROR("Failed to write header: {}", avErrorStr(err));

		av_dump_format(oc_, 0, nullptr, 1);

		return {};
	}

	[[nodiscard]] Expected<void> writePacket(Packet& packet, int streamIndex) noexcept
	{
		auto expectedStream = getStream(streamIndex);

		if (!expectedStream)
			FORWARD_AV_ERROR(expectedStream);

		auto [stream, codecContext] = expectedStream.value();

		/* rescale output packet timestamp values from codec to stream timebase */
		av_packet_rescale_ts(*packet, codecContext->native()->time_base, stream->time_base);
		packet.native()->stream_index = stream->index;
		packet.native()->pos          = -1;

		auto ret = av_interleaved_write_frame(oc_, *packet);
		if (ret < 0)
			RETURN_AV_ERROR("Error writing output packet: {}", avErrorStr(ret));

		return {};
	}

private:
	[[nodiscard]] Expected<std::tuple<AVStream*, Ptr<Encoder>>> getStream(int index)
	{
		if (index < 0 || index >= streams_.size())
			RETURN_AV_ERROR("Stream index '{}' is out of range [{}-{}]", index, 0, streams_.size());

		return streams_[index];
	}

private:
	AVFormatContext* oc_{nullptr};
	std::vector<std::tuple<AVStream*, Ptr<Encoder>>> streams_;
};

}// namespace av
