#ifndef NMOS_VIDEO_H265_H
#define NMOS_VIDEO_H265_H

#include "nmos/media_type.h"
#include "nmos/sdp_utils.h"

namespace sdp
{
    namespace fields
    {
        // See https://www.iana.org/assignments/media-types/video/H265
        // and https://tools.ietf.org/html/rfc7798#section-7
        const web::json::field<uint32_t> profile_id{ U("profile-id") };
        const web::json::field<uint32_t> profile_space{ U("profile-space") };
        const web::json::field<uint32_t> level_id{ U("level-id") };
        const web::json::field<uint32_t> tier_flag{ U("tier-flag") };
        const web::json::field<uint64_t> interop_constraints{ U("interop-constraints") };
        const web::json::field_as_string sprop_vps{ U("sprop-vps") };
        const web::json::field_as_string sprop_sps{ U("sprop-sps") };
        const web::json::field_as_string sprop_pps{ U("sprop-pps") };
    }
}

namespace nmos
{
    namespace media_types
    {
        // H.265 Video
        // See https://www.iana.org/assignments/media-types/video/H265
        // and https://tools.ietf.org/html/rfc7798
        const media_type video_H265{ U("video/H265") };
    }

    namespace fields
    {
        // See [TBC in NMOS Parameter Registers]

        const web::json::field_as_integer profile_id{ U("profile_id") };
        const web::json::field_as_integer profile_space{ U("profile_space") };
        const web::json::field_as_integer level_id{ U("level_id") };
        const web::json::field_as_integer tier_flag{ U("tier_flag") };
    }

    namespace caps
    {
        // See [TBC in NMOS Parameter Registers]

        namespace format
        {
            const web::json::field_as_value_or profile_id{ U("urn:x-nmos:cap:format:profile_id"), {} }; // number
            const web::json::field_as_value_or profile_space{ U("urn:x-nmos:cap:format:profile_space"), {} }; // number
            const web::json::field_as_value_or level_id{ U("urn:x-nmos:cap:format:level_id"), {} }; // number
            const web::json::field_as_value_or tier_flag{ U("urn:x-nmos:cap:format:tier_flag"), {} }; // number
        }
    }

    // Additional "video/H265" parameters
    // See https://www.iana.org/assignments/media-types/video/H265
    // and https://tools.ietf.org/html/rfc7798
    struct video_H265_parameters
    {
        // fmtp indicates format
        uint32_t profile_id;
        uint32_t profile_space;
        uint32_t level_id;
        uint32_t tier_flag;
        uint64_t interop_constraints;
        utility::string_t sprop_vps;
        utility::string_t sprop_sps;
        utility::string_t sprop_pps;

        video_H265_parameters() : profile_id(), profile_space(), level_id(), tier_flag(), interop_constraints() {}

        video_H265_parameters(uint32_t profile_id, uint32_t profile_space, uint32_t level_id, uint32_t tier_flag, uint64_t interop_constraints, utility::string_t sprop_vps, utility::string_t sprop_sps, utility::string_t sprop_pps)
            : profile_id(profile_id)
            , profile_space(profile_space)
            , level_id(level_id)
            , tier_flag(tier_flag)
            , interop_constraints(interop_constraints)
            , sprop_vps(sprop_vps)
            , sprop_sps(sprop_sps)
            , sprop_pps(sprop_pps)
        {}
    };

    // Construct additional "video/H265" parameters from the IS-04 resources
    video_H265_parameters make_video_H265_parameters(const web::json::value& node, const web::json::value& source, const web::json::value& flow, const web::json::value& sender, const utility::string_t& sprop_parameter_sets);
    // Construct SDP parameters for "video/H265"
    sdp_parameters make_video_H265_sdp_parameters(const utility::string_t& session_name, const video_H265_parameters& params, uint64_t payload_type, const std::vector<utility::string_t>& media_stream_ids = {}, const std::vector<sdp_parameters::ts_refclk_t>& ts_refclk = {});
    // Get additional "video/H265" parameters from the SDP parameters
    video_H265_parameters get_video_H265_parameters(const sdp_parameters& sdp_params);

    // Construct SDP parameters for "video/H265"
    inline sdp_parameters make_sdp_parameters(const utility::string_t& session_name, const video_H265_parameters& params, uint64_t payload_type, const std::vector<utility::string_t>& media_stream_ids = {}, const std::vector<sdp_parameters::ts_refclk_t>& ts_refclk = {})
    {
        return make_video_H265_sdp_parameters(session_name, params, payload_type, media_stream_ids, ts_refclk);
    }

    // Validate SDP parameters for "video/H265" against IS-04 receiver capabilities
    void validate_video_H265_sdp_parameters(const web::json::value& receiver, const nmos::sdp_parameters& sdp_params);
}

#endif
