#pragma once

#include "common.hpp"

namespace av
{
class Rational
{
public:
	explicit Rational(const AVRational& val) noexcept
	    : val_(val)
	{
	}

	explicit Rational(double val, int max = 1000000) noexcept
	    : val_(av_d2q(val, max))
	{
	}

	const AVRational& operator*() const noexcept
	{
		return val_;
	}
	AVRational& operator*() noexcept
	{
		return val_;
	}

	[[nodiscard]] double toDouble() const noexcept
	{
		return av_q2d(val_);
	}

private:
	AVRational val_;
};
}// namespace av