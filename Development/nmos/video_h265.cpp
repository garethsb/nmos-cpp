#include "nmos/video_h265.h"

#include <iomanip>
#include <map>
#include "nmos/capabilities.h"
#include "nmos/format.h"
#include "nmos/json_fields.h"

namespace nmos
{
    // Construct additional "video/H265" parameters from the IS-04 resources
    video_H265_parameters make_video_H265_parameters(const web::json::value& node, const web::json::value& source, const web::json::value& flow, const web::json::value& sender, const utility::string_t& sprop_parameter_sets)
    {
        video_H265_parameters params;

        params.profile_id = nmos::fields::profile_id(flow);
        params.profile_space = nmos::fields::profile_space(flow);
        params.level_id = nmos::fields::level_id(flow);
        params.tier_flag = nmos::fields::tier_flag(flow);

        return params;
    }

    namespace details
    {
        utility::string_t make_sdp_interop_constraints(uint64_t interop_constraints)
        {
            utility::ostringstream_t ss;
            ss << std::hex << std::uppercase << std::setw(12) << std::setfill(U('0')) << interop_constraints;
            return ss.str();
        }

        uint64_t parse_interop_constraints(const utility::string_t& interop_constraints)
        {
            utility::istringstream_t ss(interop_constraints);
            uint64_t result = 0;
            ss >> std::hex >> result;
            return result;
        }
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
            { sdp::fields::profile_id, details::make_sdp_interop_constraints(params.profile_id) },
            { sdp::fields::profile_space, utility::ostringstreamed(params.profile_space) },
            { sdp::fields::level_id, utility::ostringstreamed(params.level_id) },
            { sdp::fields::tier_flag, utility::ostringstreamed(params.tier_flag) },
            { sdp::fields::interop_constraints, utility::ostringstreamed(params.interop_constraints) },
            { sdp::fields::sprop_vps, params.sprop_vps },
            { sdp::fields::sprop_sps, params.sprop_sps },
            { sdp::fields::sprop_pps, params.sprop_pps }
        };

        return{ session_name, sdp::media_types::video, rtpmap, fmtp, {}, {}, {}, {}, media_stream_ids, ts_refclk };
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
        const auto profile_space = details::find_fmtp(sdp_params.fmtp, sdp::fields::profile_space);
        if(sdp_params.fmtp.end() != profile_space ) throw details::sdp_processing_error("missing format parameter: profile-space");
        params.profile_space = utility::istringstreamed<uint32_t>( profile_space->second );

        // optional
        const auto level_id = details::find_fmtp(sdp_params.fmtp, sdp::fields::level_id);
        if (sdp_params.fmtp.end() == level_id) throw details::sdp_processing_error("missing format parameter: level-id");
        params.level_id = utility::istringstreamed<uint32_t>( level_id->second );

        // optional
        const auto tier_flag = details::find_fmtp(sdp_params.fmtp, sdp::fields::tier_flag);
        if (sdp_params.fmtp.end() == tier_flag) throw details::sdp_processing_error("missing format parameter: tier-flag");
        params.tier_flag = utility::istringstreamed<uint32_t>( tier_flag->second );

        // optional
        const auto interop_constraints = details::find_fmtp(sdp_params.fmtp, sdp::fields::interop_constraints);
        if (sdp_params.fmtp.end() == interop_constraints) throw details::sdp_processing_error("missing format parameter: interop-constraints");
        params.interop_constraints = details::parse_interop_constraints( interop_constraints->second );

        // optional
        const auto sprop_vps = details::find_fmtp(sdp_params.fmtp, sdp::fields::sprop_vps);
        if (sdp_params.fmtp.end() == sprop_vps) throw details::sdp_processing_error("missing format parameter: sprop-vps");
        params.sprop_vps = sprop_vps->second;

        // optional
        const auto sprop_sps = details::find_fmtp(sdp_params.fmtp, sdp::fields::sprop_sps);
        if (sdp_params.fmtp.end() == sprop_sps) throw details::sdp_processing_error("missing format parameter: sprop-sps");
        params.sprop_sps = sprop_sps->second;

        // optional
        const auto sprop_pps = details::find_fmtp(sdp_params.fmtp, sdp::fields::sprop_pps);
        if (sdp_params.fmtp.end() == sprop_pps) throw details::sdp_processing_error("missing format parameter: sprop-pps");
        params.sprop_pps = sprop_pps->second;

        return params;
    }

    namespace details
    {
        const video_H265_parameters* get_h265(const format_parameters* format) { return get<video_H265_parameters>(format); }

        // NMOS Parameter Registers - Capabilities register
        // See https://specs.amwa.tv/nmos-parameter-registers/branches/main/capabilities/
#define CAPS_ARGS const sdp_parameters& sdp, const format_parameters& format, const web::json::value& con
        static const std::map<utility::string_t, std::function<bool(CAPS_ARGS)>> h265_constraints
        {
            { nmos::caps::format::media_type, [](CAPS_ARGS) { return nmos::match_string_constraint(get_media_type(sdp).name, con); } },
            { nmos::caps::format::profile_id, [](CAPS_ARGS) { auto h265 = get_h265(&format); return h265 && nmos::match_integer_constraint(h265->profile_id, con); } },
            { nmos::caps::format::profile_space, [](CAPS_ARGS) { auto h265 = get_h265(&format); return h265 && nmos::match_integer_constraint(h265->profile_space, con); } },
            { nmos::caps::format::level_id, [](CAPS_ARGS) { auto h265 = get_h265(&format); return h265 && nmos::match_integer_constraint(h265->level_id, con); } },
            { nmos::caps::format::tier_flag, [](CAPS_ARGS) { auto h265 = get_h265(&format); return h265 && nmos::match_integer_constraint(h265->tier_flag, con); } }
        };
#undef CAPS_ARGS
    }

    // Validate SDP parameters for "video/H265" against IS-04 receiver capabilities
    // cf. nmos::validate_sdp_parameters
    void validate_video_H265_sdp_parameters(const web::json::value& receiver, const nmos::sdp_parameters& sdp_params)
    {
        // this function can only be used to validate SDP data for "video/H265"; logic error otherwise
        const auto media_type = get_media_type(sdp_params);
        if (nmos::media_types::video_H265 != media_type) throw std::invalid_argument("unexpected media type/encoding name");

        nmos::details::validate_sdp_parameters(details::h265_constraints, sdp_params, nmos::formats::video, get_video_H265_parameters(sdp_params), receiver);
    }
}
