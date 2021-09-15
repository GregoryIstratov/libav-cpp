#pragma once

#include <av/common.hpp>
#include <unordered_map>
#include <variant>

namespace av
{

using OptValue    = std::variant<std::string, int, double, AVRational>;
using OptValueMap = std::unordered_map<std::string, OptValue>;

class OptSetter
{
public:
	static void set(void* obj, const OptValueMap& opts)
	{
		for (const auto& [k, var] : opts)
		{
			std::string_view sv = k;
			std::visit([obj, sv](auto&& arg) { visit(obj, sv, arg); }, var);
		}
	}

private:
	static void visit(void* obj, std::string_view name, const std::string& s) noexcept
	{
		av_opt_set(obj, name.data(), s.data(), 0);
	};

	static void visit(void* obj, std::string_view name, int i) noexcept
	{
		av_opt_set_int(obj, name.data(), i, 0);
	};

	static void visit(void* obj, std::string_view name, double d) noexcept
	{
		av_opt_set_double(obj, name.data(), d, 0);
	};

	static void visit(void* obj, std::string_view name, AVRational q) noexcept
	{
		av_opt_set_q(obj, name.data(), q, 0);
	};
};

}// namespace av
