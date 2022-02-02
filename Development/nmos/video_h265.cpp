#include "nmos/video_h265.h"
#include "nmos/json_fields.h"

namespace nmos
{

    // Construct additional "video/H265" parameters from the IS-04 resources
    video_H265_parameters make_video_H265_parameters(const web::json::value& node, const web::json::value& source, const web::json::value& flow, const web::json::value& sender, const utility::string_t& sprop_parameter_sets)
    {
        video_H265_parameters params;

        params.profile_id = nmos::fields::profile_id(flow);

        params.level_id = nmos::fields::level_id(flow);

        return params;
    }

    // Construct SDP parameters for "video/H265", with sensible defaults for unspecified fields
    sdp_parameters make_video_H265_sdp_parameters(const utility::string_t& session_name, const video_H265_parameters& params, uint64_t payload_type, const std::vector<utility::string_t>& media_stream_ids, const std::vector<sdp_parameters::ts_refclk_t>& ts_refclk)
    {
        // a=rtpmap:<payload type> <encoding name>/<clock rate>[/<encoding parameters>]
        sdp_parameters::rtpmap_t rtpmap = { payload_type, U("H265"), 90000 };

        // a=fmtp:<format> <format specific parameters>
        // for simplicity, following the order of parameters given in VSF TR-05:2017
        // See https://tools.ietf.org/html/rfc4566#section-6
        sdp_parameters::fmtp_t fmtp = {
            { sdp::fields::profile_id, utility::ostringstreamed(params.profile_id) },
            { sdp::fields::level_id, utility::ostringstreamed(params.level_id) },
            { sdp::fields::interop_constraints, utility::ostringstreamed(params.interop_constraints) },
            { sdp::fields::sprop_vps, params.sprop_vps },
            { sdp::fields::sprop_sps, params.sprop_sps },
            { sdp::fields::sprop_pps, params.sprop_pps }
        };

        return{ session_name, sdp::media_types::video, rtpmap, fmtp, {}, {}, {}, {}, media_stream_ids, ts_refclk };
    }


    namespace details
    {
        // ought to be declared in nmos/sdp_utils.h
        std::runtime_error sdp_processing_error(const std::string& message);

        // ought to be declared in nmos/sdp_utils.h (definition currently has internal linkage)
        sdp_parameters::fmtp_t::const_iterator find_fmtp(const sdp_parameters::fmtp_t& fmtp, const utility::string_t& param_name)
        {
            return std::find_if(fmtp.begin(), fmtp.end(), [&](const sdp_parameters::fmtp_t::value_type& param)
            {
                return param.first == param_name;
            });
        }
    }

    // Get additional "video/h265" parameters from the SDP parameters
    video_H265_parameters get_video_H265_parameters(const sdp_parameters& sdp_params)
    {
        video_H265_parameters params;

        if (sdp_params.fmtp.empty()) return params;

        // optional
        const auto profile_id = details::find_fmtp(sdp_params.fmtp, sdp::fields::profile_id);
        if(sdp_params.fmtp.end() != profile_id ) throw details::sdp_processing_error("missing format parameter: profile-id");
        params.profile_id = utility::istringstreamed<uint32_t>( profile_id->second );

        // optional
        const auto level_id = details::find_fmtp(sdp_params.fmtp, sdp::fields::level_id);
        if (sdp_params.fmtp.end() == level_id) throw details::sdp_processing_error("missing format parameter: level_id");
        params.level_id = utility::istringstreamed<uint32_t>( level_id->second );

        //optional
        const auto interop_constraints = details::find_fmtp(sdp_params.fmtp, sdp::fields::interop_constraints);
        if (sdp_params.fmtp.end() == interop_constraints) throw details::sdp_processing_error("missing format parameter: interop_constraints");
        params.interop_constraints = utility::istringstreamed<uint32_t>( interop_constraints->second );

        //optional
        const auto sprop_vps = details::find_fmtp(sdp_params.fmtp, sdp::fields::sprop_vps);
        if (sdp_params.fmtp.end() == sprop_vps) throw details::sdp_processing_error("missing format parameter: sprop_vps");
        params.sprop_vps = sprop_vps->second;

        //optional
        const auto sprop_sps = details::find_fmtp(sdp_params.fmtp, sdp::fields::sprop_sps);
        if (sdp_params.fmtp.end() == sprop_sps) throw details::sdp_processing_error("missing format parameter: sprop_sps");
        params.sprop_sps = sprop_sps->second;

        //optional
        const auto sprop_pps = details::find_fmtp(sdp_params.fmtp, sdp::fields::sprop_pps);
        if (sdp_params.fmtp.end() == sprop_pps) throw details::sdp_processing_error("missing format parameter: sprop_pps");
        params.sprop_pps = sprop_pps->second;

        return params;
    }
}