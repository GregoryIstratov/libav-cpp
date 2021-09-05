#pragma once

#include "common.hpp"

namespace av
{
class Frame
{
	explicit Frame(AVFrame* frame) noexcept
	    : frame_(frame)
	{}

public:
	Frame() noexcept
	    : frame_(av_frame_alloc())
	{
	}

	~Frame()
	{
		if (frame_)
			av_frame_free(&frame_);
	}

	auto* operator*() noexcept
	{
		return frame_;
	}
	const auto* operator*() const noexcept
	{
		return frame_;
	}

	auto* native() noexcept
	{
		return frame_;
	}
	const auto* native() const noexcept
	{
		return frame_;
	}

	Frame(Frame&& other) noexcept
	{
		frame_       = other.frame_;
		other.frame_ = nullptr;
	}

	Frame(const Frame& other) noexcept
	{
		frame_ = av_frame_alloc();
		av_frame_ref(frame_, *other);
	}

	Frame& operator=(Frame&& other) noexcept
	{
		if (&other == this)
			return *this;

		av_frame_free(&frame_);
		frame_       = other.frame_;
		other.frame_ = nullptr;

		return *this;
	}

	Frame& operator=(const Frame& other) noexcept
	{
		if (&other == this)
			return *this;

		av_frame_unref(frame_);
		av_frame_ref(frame_, *other);

		return *this;
	}

	AVMediaType type() const noexcept
	{
		return type_;
	}
	void type(AVMediaType type) noexcept
	{
		type_ = type;
	}

private:
	AVFrame* frame_{nullptr};
	AVMediaType type_{AVMEDIA_TYPE_UNKNOWN};
};

}// namespace av
