#pragma once

#include <av/Packet.hpp>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

namespace av
{

class BSF : NoCopyable
{
	BSF(AVBSFContext* bsfc)
	    : bsfc_(bsfc)
	{}

public:
	static Expected<Ptr<BSF>> create(const char* filters, AVCodecParameters* par)
	{
		AVBSFContext* bsfc  = nullptr;
		const char* bsfName = filters;
		int err             = av_bsf_list_parse_str(bsfName, &bsfc);
		if (err < 0)
			RETURN_AV_ERROR("Error parsing {} bitstream filter: {}", bsfName, avErrorStr(err));

		err = avcodec_parameters_copy(bsfc->par_in, par);
		if (err < 0)
		{
			av_bsf_free(&bsfc);
			RETURN_AV_ERROR("Error bsf '{}' copying codec parameters: {}", bsfName, avErrorStr(err));
		}

		err = av_bsf_init(bsfc);
		if (err < 0)
		{
			av_bsf_free(&bsfc);
			RETURN_AV_ERROR("Error initializing {} bitstream filter: {}", bsfName, avErrorStr(err));
		}

		return Ptr<BSF>{new BSF{bsfc}};
	}

	~BSF()
	{
		if (bsfc_)
			av_bsf_free(&bsfc_);
	}

	std::tuple<Result, int> apply(Packet& inPkt, std::vector<Packet>& outPkts) noexcept
	{
		int err = av_bsf_send_packet(bsfc_, *inPkt);
		if (err < 0)
		{
			LOG_AV_ERROR("BSF packet send error: {}", avErrorStr(err));
			return {Result::kFail, 0};
		}

		for (auto& pkt : outPkts)
			pkt.dataUnref();

		for (int i = 0;; ++i)
		{
			if (i >= outPkts.size())
			{
				outPkts.emplace_back();
			}

			err = av_bsf_receive_packet(bsfc_, *outPkts[i]);
			if (err == AVERROR(EAGAIN))
				return {Result::kSuccess, i};

			if (err == AVERROR(EOF))
				return {Result::kEOF, i};

			if (err < 0)
			{
				LOG_AV_ERROR("BSF packet receive error: {}", avErrorStr(err));
				return {Result::kFail, i};
			}
		}
	}

private:
	AVBSFContext* bsfc_{nullptr};
};

}// namespace av
