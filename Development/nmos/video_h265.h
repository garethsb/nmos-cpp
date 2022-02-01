#ifndef NMOS_VIDEO_H265_H
#define NMOS_VIDEO_H265_H

#include "nmos/media_type.h"
#include "nmos/sdp_utils.h"

namespace sdp
{
    namespace fields
    {
        // See https://datatracker.ietf.org/doc/html/rfc7798#page-64
        //H265 payload mapping
        const web::json::field<uint32_t> profile_id{U("profile-id")};
        const web::json::field<uint32_t> level_id{U("level-id")};
        const web::json::field<uint32_t> interop_constraints{U("interop-constraints")};
        const web::json::field_as_string sprop_vps{U("sprop-vps")};
        const web::json::field_as_string sprop_sps{U("sprop-sps")};
        const web::json::field_as_string sprop_pps{U("sprop-pps")};
    }
}

namespace nmos
{
    namespace media_types
    {
        // H.265 Video
        // See https://datatracker.ietf.org/doc/html/rfc7798
        const media_type video_H265{ U("video/H265") };
    }


    // Additional "video/H265" parameters
    // See https://datatracker.ietf.org/doc/html/rfc7798#section-4.4
    struct video_h265_parameters
    {
        // fmtp indicates format
        uint32_t profile_id;
        uint32_t level_id;
        uint32_t interop_constraints;
        utility::string_t sprop_vps;
        utility::string_t sprop_sps;
        utility::string_t sprop_pps;

        video_h265_parameters() : profile_id(), level_id(), interop_constraints(), sprop_vps(), sprop_sps(), sprop_pps() {}

        video_h265_parameters(uint32_t profile_id, uint32_t level_id, uint32_t interop_constraints, utility::string_t sprop_vps,
                              utility::string_t sprop_sps, utility::string_t sprop_pps)
                : profile_id(profile_id), level_id(level_id), interop_constraints(interop_constraints), sprop_vps(sprop_vps),
                  sprop_sps(sprop_sps), sprop_pps(sprop_pps) {}
    };


    // Construct additional "video/H265" parameters from the IS-04 resources
    video_h265_parameters make_video_H265_parameters(const web::json::value& node, const web::json::value& source, const web::json::value& flow, const web::json::value& sender, const utility::string_t& sprop_parameter_sets);
    // Construct SDP parameters for "video/H265"
    sdp_parameters make_video_h265_sdp_parameters(const utility::string_t& session_name, const video_h265_parameters& params, uint64_t payload_type, const std::vector<utility::string_t>& media_stream_ids = {}, const std::vector<sdp_parameters::ts_refclk_t>& ts_refclk = {});
    // Get additional "video/H265" parameters from the SDP parameters
    video_h265_parameters get_video_H265_parameters(const sdp_parameters& sdp_params);

    // Construct SDP parameters for "video/H265"
    inline sdp_parameters make_sdp_parameters(const utility::string_t& session_name, const video_h265_parameters& params, uint64_t payload_type, const std::vector<utility::string_t>& media_stream_ids = {}, const std::vector<sdp_parameters::ts_refclk_t>& ts_refclk = {})
    {
        return make_video_h265_sdp_parameters(session_name, params, payload_type, media_stream_ids, ts_refclk);
    }
}

#endif