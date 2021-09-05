#pragma once

#include "Frame.hpp"
#include "common.hpp"

namespace av
{

class Scale : NoCopyable
{
	explicit Scale(SwsContext* sws) noexcept
	    : sws_(sws)
	{}

public:
	static Expected<Ptr<Scale>> create(int inputWidth, int inputHeight, AVPixelFormat inputPixFmt, int outputWidth, int outputHeight, AVPixelFormat outputPixFmt) noexcept
	{
		auto sws = sws_getContext(inputWidth, inputHeight, inputPixFmt,
		                          outputWidth, outputHeight, outputPixFmt,
		                          SWS_BICUBIC, nullptr, nullptr, nullptr);

		if (!sws)
			RETURN_AV_ERROR("Failed to create sws context");

		return Ptr<Scale>{new Scale{sws}};
	}

	~Scale()
	{
		if (sws_)
			sws_freeContext(sws_);
	}

	void scale(const uint8_t* const srcSlice[],
	           const int srcStride[], int srcSliceY, int srcSliceH,
	           uint8_t* const dst[], const int dstStride[])
	{
		sws_scale(sws_, srcSlice, srcStride, srcSliceY, srcSliceH, dst, dstStride);
	}

	void scale(const Frame& src, Frame& dst)
	{
		sws_scale(sws_, src.native()->data, src.native()->linesize, 0, src.native()->height, dst.native()->data, dst.native()->linesize);
	}

private:
	SwsContext* sws_{nullptr};
};

}// namespace av
