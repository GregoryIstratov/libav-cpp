#pragma once

#include <av/common.hpp>

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

	static Expected<Ptr<Frame>> create(int width, int height, AVPixelFormat pixFmt, int align = 0) noexcept
	{
		auto frame = av::makePtr<Frame>();
		if(!frame)
			RETURN_AV_ERROR("Failed to alloc frame");

		auto f = frame->native();
		f->width = width;
		f->height = height;
		f->format = pixFmt;

		auto err = av_frame_get_buffer(f, align);
		if(err < 0)
			RETURN_AV_ERROR("Failed to get buffer: {}", avErrorStr(err));

		return frame;
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

	AVFrame* native() noexcept
	{
		return frame_;
	}
	const AVFrame* native() const noexcept
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
