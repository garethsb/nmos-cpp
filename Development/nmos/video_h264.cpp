#include "nmos/video_h264.h"

#include <iomanip>
#include "nmos/capabilities.h"
#include "nmos/format.h"

namespace nmos
{
    sdp::packetization_mode make_sdp_packetization_mode(const packetization_mode& packetization_mode)
    {
        if (packetization_mode == nmos::packetization_modes::single_nal_unit_mode) return sdp::single_nal_unit_mode;
        if (packetization_mode == nmos::packetization_modes::non_interleaved_mode) return sdp::non_interleaved_mode;
        if (packetization_mode == nmos::packetization_modes::interleaved_mode) return sdp::interleaved_mode;
        return sdp::single_nal_unit_mode;
    }

    packetization_mode parse_sdp_packetization_mode(const sdp::packetization_mode& packetization_mode)
    {
        switch (packetization_mode)
        {
        case sdp::single_nal_unit_mode: return nmos::packetization_modes::single_nal_unit_mode;
        case sdp::non_interleaved_mode: return nmos::packetization_modes::non_interleaved_mode;
        case sdp::interleaved_mode: return nmos::packetization_modes::interleaved_mode;
        default: return nmos::packetization_modes::single_nal_unit_mode;
        };
    }

    // Construct additional "video/H264" parameters from the IS-04 resources
    video_H264_parameters make_video_H264_parameters(const web::json::value& node, const web::json::value& source, const web::json::value& flow, const web::json::value& sender, const utility::string_t& sprop_parameter_sets)
    {
        video_H264_parameters params;

        // "If no profile-level-id is present, the Baseline profile,
        // without additional constraints at Level 1, MUST be inferred."
        // but better to let the caller distinguish that it's been defaulted?
        // See https://tools.ietf.org/html/rfc6184#section-8.1
        params.profile_level_id = nmos::fields::profile_level_id(flow);

        params.packetization_mode = make_sdp_packetization_mode(packetization_mode{ nmos::fields::packetization_mode(sender) });

        params.sprop_parameter_sets = sprop_parameter_sets;

        return params;
    }

    namespace details
    {
        utility::string_t make_sdp_profile_level_id(uint32_t profile_level_id)
        {
            utility::ostringstream_t ss;
            ss << std::hex << std::uppercase << std::setw(6) << std::setfill(U('0')) << profile_level_id;
            return ss.str();
        }

        uint32_t parse_sdp_profile_level_id(const utility::string_t& profile_level_id)
        {
            utility::istringstream_t ss(profile_level_id);
            uint32_t result = 0;
            ss >> std::hex >> result;
            return result;
        }
    }

    // Construct SDP parameters for "video/H264"
    sdp_parameters make_video_H264_sdp_parameters(const utility::string_t& session_name, const video_H264_parameters& params, uint64_t payload_type, const std::vector<utility::string_t>& media_stream_ids, const std::vector<sdp_parameters::ts_refclk_t>& ts_refclk)
    {
        // a=rtpmap:<payload type> <encoding name>/<clock rate>[/<encoding parameters>]
        sdp_parameters::rtpmap_t rtpmap = { payload_type, U("H264"), 90000 };

        // a=fmtp:<format> <format specific parameters>
        sdp_parameters::fmtp_t fmtp = {};
        if (0 != params.profile_level_id) fmtp.push_back({ sdp::fields::profile_level_id, details::make_sdp_profile_level_id(params.profile_level_id) });
        fmtp.push_back({ sdp::fields::packetization_mode, utility::ostringstreamed((uint32_t)params.packetization_mode) });
        if (!params.sprop_parameter_sets.empty()) fmtp.push_back({ sdp::fields::sprop_parameter_sets, params.sprop_parameter_sets });

        return{ session_name, sdp::media_types::video, rtpmap, fmtp, {}, {}, {}, {}, media_stream_ids, ts_refclk };
    }

    // Get additional "video/H264" parameters from the SDP parameters
    video_H264_parameters get_video_H264_parameters(const sdp_parameters& sdp_params)
    {
        video_H264_parameters params;

        // all parameters are optional
        if (sdp_params.fmtp.empty()) return params;

        // optional
        const auto profile_level_id = details::find_fmtp(sdp_params.fmtp, sdp::fields::profile_level_id);
        if (sdp_params.fmtp.end() != profile_level_id)
        {
            params.profile_level_id = details::parse_sdp_profile_level_id(profile_level_id->second);
        }
        // "If no profile-level-id is present, the Baseline profile,
        // without additional constraints at Level 1, MUST be inferred."
        // but better to let the caller distinguish that it's been defaulted?
        // See https://tools.ietf.org/html/rfc6184#section-8.1

        // optional
        const auto packetization_mode = details::find_fmtp(sdp_params.fmtp, sdp::fields::packetization_mode);
        if (sdp_params.fmtp.end() != packetization_mode)
        {
            params.packetization_mode = sdp::packetization_mode(utility::istringstreamed(packetization_mode->second, (uint32_t)sdp::single_nal_unit_mode));
        }

        // optional
        const auto sprop_parameter_sets = details::find_fmtp(sdp_params.fmtp, sdp::fields::sprop_parameter_sets);
        if (sdp_params.fmtp.end() != sprop_parameter_sets)
        {
            params.sprop_parameter_sets = sprop_parameter_sets->second;
        }

        return params;
    }

    namespace details
    {
        const video_H264_parameters* get_h264(const format_parameters* format) { return get<video_H264_parameters>(format); }

        // NMOS Parameter Registers - Capabilities register
        // See https://specs.amwa.tv/nmos-parameter-registers/branches/main/capabilities/
#define CAPS_ARGS const sdp_parameters& sdp, const format_parameters& format, const web::json::value& con
        static const std::map<utility::string_t, std::function<bool(CAPS_ARGS)>> h264_constraints
        {
            { nmos::caps::format::media_type, [](CAPS_ARGS) { return nmos::match_string_constraint(get_media_type(sdp).name, con); } },
            // hmm, there are lots of other format parameter constraints that could be validated against the sprop_parameter_sets
            { nmos::caps::format::profile_level_id, [](CAPS_ARGS) { auto h264 = get_h264(&format); return h264 && nmos::match_integer_constraint(h264->profile_level_id, con); } },
            { nmos::caps::transport::packetization_mode, [](CAPS_ARGS) { auto h264 = get_h264(&format); return h264 && nmos::match_string_constraint(parse_sdp_packetization_mode(h264->packetization_mode).name, con); } }
        };
#undef CAPS_ARGS
    }

    // Validate SDP parameters for "video/H264" against IS-04 receiver capabilities
    // cf. nmos::validate_sdp_parameters
    void validate_video_H264_sdp_parameters(const web::json::value& receiver, const nmos::sdp_parameters& sdp_params)
    {
        // this function can only be used to validate SDP data for "video/H264"; logic error otherwise
        const auto media_type = get_media_type(sdp_params);
        if (nmos::media_types::video_H264 != media_type) throw std::invalid_argument("unexpected media type/encoding name");

        nmos::details::validate_sdp_parameters(details::h264_constraints, sdp_params, nmos::formats::video, get_video_H264_parameters(sdp_params), receiver);
    }
}
