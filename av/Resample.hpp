#pragma once

#include "Frame.hpp"
#include "common.hpp"

namespace av
{

class Resample : NoCopyable
{
	explicit Resample(SwrContext* swr) noexcept
	    : swr_(swr)
	{}

public:
	static Expected<Ptr<Resample>> create(int inChannels, AVSampleFormat inSampleFmt, int inSampleRate,
	                                      int outChannels, AVSampleFormat outSampleFmt, int outSampleRate) noexcept
	{
		/*
          * Create a resampler context for the conversion.
          * Set the conversion parameters.
          * Default channel layouts based on the number of channels
          * are assumed for simplicity (they are sometimes not detected
          * properly by the demuxer and/or decoder).
          */

		LOG_AV_DEBUG("Creating swr context: input - channel_layout: {} sample_rate: {} format: {} output - channel_layout: {} sample_rate: {} format: {}",
		             av_get_default_channel_layout(inChannels), inSampleRate, av_get_sample_fmt_name(inSampleFmt),
		             av_get_default_channel_layout(outChannels), outSampleRate, av_get_sample_fmt_name(outSampleFmt));

		auto swr = swr_alloc_set_opts(nullptr,
		                              av_get_default_channel_layout(outChannels),
		                              outSampleFmt,
		                              outSampleRate,
		                              av_get_default_channel_layout(inChannels),
		                              inSampleFmt,
		                              inSampleRate,
		                              0, nullptr);

		if (!swr)
			RETURN_AV_ERROR("Failed to create swr context");

		/* Open the resampler with the specified parameters. */
		int err = 0;
		if ((err = swr_init(swr)) < 0)
		{
			swr_free(&swr);
			RETURN_AV_ERROR("Could not open resample context: {}", avErrorStr(err));
		}

		return Ptr<Resample>{new Resample{swr}};
	}

	~Resample()
	{
		if (swr_)
			swr_free(&swr_);
	}

	Expected<void> convert(const Frame& input, Frame& output) noexcept
	{
		//LOG_AV_DEBUG("input - channel_layout: {} sample_rate: {} format: {}", input->channel_layout, input->sample_rate, av_get_sample_fmt_name((AVSampleFormat)input->format));
		//LOG_AV_DEBUG("output - channel_layout: {} sample_rate: {} format: {}", output->channel_layout, output->sample_rate, av_get_sample_fmt_name((AVSampleFormat)output->format));
		/* Convert the samples using the resampler. */
		auto err = swr_convert_frame(swr_, *output, *input);
		if (err < 0)
			RETURN_AV_ERROR("Could not convert input samples: {}", avErrorStr(err));

		return {};
	}

private:
	SwrContext* swr_{nullptr};
};

}// namespace av
