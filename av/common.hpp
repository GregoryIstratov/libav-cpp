#pragma once

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace av
{

template<typename T>
using Ptr = std::shared_ptr<T>;

template<typename T, typename... Args>
auto makePtr(Args&&... args)
{
	return std::make_shared<T>(std::forward<Args>(args)...);
}

namespace internal
{
template<typename T>
std::string toString(T&& v) noexcept
{
	if constexpr (std::is_convertible_v<T, std::string>)
		return std::string(v);
	else
		return std::to_string(v);
};

inline
std::string toString(std::string_view v) noexcept
{
	return v.data();
}

template<typename... Args>
inline size_t formatInplaceEx(std::string& fmt, int offset, Args&&... args) noexcept
{
	std::vector<std::string> sargs;
	(sargs.emplace_back(toString(args)), ...);

	size_t pos = offset;
	if (fmt.capacity() < 1024)
		fmt.reserve(1024);

	for (const auto& arg : sargs)
	{
		pos = fmt.find("{}", pos);
		if (pos == std::string::npos)
			return pos;

		fmt.replace(pos, 2, arg);

		pos += arg.size();
		if (pos >= fmt.size())
			return fmt.size();
	}

	return fmt.size();
}

template<typename... Args>
inline size_t formatInplace(std::string& fmt, Args&&... args) noexcept
{
	return formatInplaceEx(fmt, 0, std::forward<Args>(args)...);
}

template<typename... Args>
inline std::string format(std::string_view fmt, Args&&... args) noexcept
{
	std::string result(fmt);
	formatInplace(result, std::forward<Args>(args)...);

	return result;
}

class SourceLocation
{
public:
	SourceLocation(const char* file, int line, const char* fun) noexcept
	    : file_(file), fun_(fun), line_(line)
	{
	}

	[[nodiscard]] std::string toString() const noexcept
	{
		return format("{}:{} [{}]", file_, line_, fun_);
	}

	auto values() const noexcept
	{
		return std::tuple{file_, line_, fun_};
	}

private:
	const char* file_;
	const char* fun_;
	int line_;
};

}// namespace internal

#define MAKE_AV_SOURCE_LOCATION() (av::internal::SourceLocation(__FILE__, __LINE__, static_cast<const char*>(__FUNCTION__)))

enum LogLevel
{
	Trace    = 0,
	Debug    = 1,
	Info     = 2,
	Warn     = 3,
	Err      = 4,
	Critical = 5,
	Off      = 6,
	n_levels
};


void writeLog(LogLevel level, internal::SourceLocation&& loc, std::string msg) noexcept;

#define LOG_AV_ERROR(...) av::writeLog(av::LogLevel::Err, MAKE_AV_SOURCE_LOCATION(), av::internal::format(__VA_ARGS__))
#define LOG_AV_DEBUG(...) av::writeLog(av::LogLevel::Debug, MAKE_AV_SOURCE_LOCATION(), av::internal::format(__VA_ARGS__))
#define LOG_AV_INFO(...) av::writeLog(av::LogLevel::Info, MAKE_AV_SOURCE_LOCATION(), av::internal::format(__VA_ARGS__))

enum class Result
{
	kSuccess = 0,
	kEAGAIN,
	kEOF,
	kFail
};

struct NoCopyable
{
	NoCopyable()                  = default;
	NoCopyable(const NoCopyable&) = delete;
	NoCopyable& operator=(const NoCopyable&) = delete;
};

inline std::string avErrorStr(int av_error_code) noexcept
{
	const auto buf_size = 1024U;
	std::string err_string(buf_size, 0);

	if (0 != av_strerror(av_error_code, err_string.data(), buf_size - 1))
	{
		return internal::format("Unknown error with code: {}", av_error_code);
	}

	const auto len = std::strlen(err_string.data());
	err_string.resize(len);

	return err_string;
}

class ExpectedBase
{
public:
	ExpectedBase(const internal::SourceLocation& loc, std::string_view desc) noexcept
	    : desc_(desc)
	{
		stack_.emplace_back(loc);
	}

	ExpectedBase(const internal::SourceLocation& loc, ExpectedBase&& other) noexcept
	    : stack_(std::move(other.stack_)), desc_(std::move(other.desc_))
	{
		stack_.emplace_back(loc);
	}

	ExpectedBase() = default;

	ExpectedBase(ExpectedBase&& o) noexcept
	    : stack_(std::move(o.stack_)), desc_(std::move(o.desc_))
	{}

	ExpectedBase& operator=(ExpectedBase&& o) noexcept
	{
		if (&o == this)
			return *this;

		stack_ = std::move(o.stack_);
		desc_  = std::move(o.desc_);

		return *this;
	}

	explicit operator bool() const noexcept
	{
		return stack_.empty();
	}

	const auto& stack() const noexcept
	{
		return stack_;
	}

	const auto& errorDescription() const noexcept
	{
		return desc_;
	}

	[[nodiscard]] std::string errorString() const noexcept
	{
		if (stack_.empty())
			return "";

		std::string result;
		result.reserve(4 * 1024);

		int i      = 0;
		size_t pos = 0;
		for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
		{
			result += "#{} {}\n";
			pos = internal::formatInplaceEx(result, pos, i++, it->toString());
		}

		result += "Error: {}";
		internal::formatInplaceEx(result, pos, desc_);

		return result;
	}

private:
	std::vector<internal::SourceLocation> stack_;
	std::string desc_;
};

template<typename T>
class Expected : public ExpectedBase
{
public:
	Expected(T&& value)
	    : value_(std::move(value))
	{
	}

	Expected(const T& value)
	    : value_(value)
	{
	}

	using ExpectedBase::ExpectedBase;

	Expected(Expected&& o) noexcept = default;
	Expected& operator=(Expected&& o) noexcept = default;

	const auto& value() const noexcept
	{
		return value_;
	}

private:
	T value_;
};

template<>
class Expected<void> : public ExpectedBase
{
public:
	using ExpectedBase::ExpectedBase;

	Expected(Expected&& o) noexcept = default;
	Expected& operator=(Expected&& o) noexcept = default;
};

#define RETURN_AV_ERROR(...)                                         \
	return                                                           \
	{                                                                \
		MAKE_AV_SOURCE_LOCATION(), av::internal::format(__VA_ARGS__) \
	}
#define FORWARD_AV_ERROR(err)                     \
	return                                        \
	{                                             \
		MAKE_AV_SOURCE_LOCATION(), std::move(err) \
	}

}// namespace av
