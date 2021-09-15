#pragma once

#include <av/common.hpp>

namespace av
{
class Rational
{
public:
	explicit Rational(AVRational&& val) noexcept
	    : val_(val)
	{
	}

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

	Rational inv() const noexcept
	{
		return Rational{av_inv_q(val_)};
	}

	operator AVRational() const { return val_; }

	static Rational fromFPS(double fps) noexcept
	{
		return Rational{fps}.inv();
	}

private:
	AVRational val_;
};
}// namespace av