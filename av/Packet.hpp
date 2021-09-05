#pragma once

#include "common.hpp"

namespace av
{
class Packet
{
	Packet(AVPacket* packet) noexcept
	    : packet_(packet)
	{}

public:
	Packet() noexcept
	    : packet_(av_packet_alloc())
	{
	}

	static Expected<Ptr<Packet>> create() noexcept
	{
		auto packet = av_packet_alloc();
		if (!packet)
			RETURN_AV_ERROR("Failed to alloc packet");

		return Ptr<Packet>{new Packet{packet}};
	}

	static Expected<Ptr<Packet>> create(const std::vector<uint8_t>& data) noexcept
	{
		auto packet = av_packet_alloc();
		if (!packet)
			RETURN_AV_ERROR("Failed to alloc packet");

		auto buffer = (uint8_t*) av_malloc(data.size());
		if (!buffer)
		{
			av_packet_free(&packet);
			RETURN_AV_ERROR("Failed to allocate buffer");
		}

		std::copy(data.begin(), data.end(), buffer);

		auto err = av_packet_from_data(packet, buffer, (int) data.size());
		if (err < 0)
		{
			av_free(buffer);
			av_packet_free(&packet);
			RETURN_AV_ERROR("Failed to make packet from data: {}", avErrorStr(err));
		}

		return Ptr<Packet>{new Packet{packet}};
	}

	~Packet()
	{
		if (packet_)
			av_packet_free(&packet_);
	}

	Packet(Packet&& other) noexcept
	{
		packet_       = other.packet_;
		other.packet_ = nullptr;
	}

	Packet(const Packet& other) noexcept
	{
		packet_ = av_packet_alloc();
		av_packet_ref(packet_, *other);
	}

	Packet& operator=(Packet&& other) noexcept
	{
		if (&other == this)
			return *this;

		av_packet_free(&packet_);
		packet_       = other.packet_;
		other.packet_ = nullptr;

		return *this;
	}

	Packet& operator=(const Packet& other) noexcept
	{
		if (&other == this)
			return *this;

		av_packet_unref(packet_);
		av_packet_ref(packet_, *other);

		return *this;
	}

	AVPacket* operator*() noexcept
	{
		return packet_;
	}
	AVPacket* operator*() const noexcept
	{
		return packet_;
	}

	AVPacket* native() noexcept
	{
		return packet_;
	}
	AVPacket* native() const noexcept
	{
		return packet_;
	}

	void dataUnref() noexcept
	{
		av_packet_unref(packet_);
	}

private:
	AVPacket* packet_{nullptr};
};

}// namespace av
