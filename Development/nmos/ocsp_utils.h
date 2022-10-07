#ifndef NMOS_OCSP_UTILS_H
#define NMOS_OCSP_UTILS_H

#if !defined(_WIN32) || defined(CPPREST_FORCE_HTTP_CLIENT_ASIO)
#include <boost/asio/ssl/context.hpp>
#endif
#include <stdexcept>
#include <vector>
#include <openssl/ssl.h>
#include "cpprest/uri.h"

namespace nmos
{
    namespace experimental
    {
        struct ocsp_settings;

        struct ocsp_exception : std::runtime_error
        {
            ocsp_exception(const std::string& message) : std::runtime_error(message) {}
        };

        // get a list of OCSP URIs from certificate
        std::vector<web::uri> get_ocsp_uris(const std::string& cert_data);

        // create OCSP request from list of server certificate chains data
        std::vector<uint8_t> make_ocsp_request(const std::vector<std::string>& cert_chains_data);

        // send OCSP response in TLS handshake
        bool send_ocsp_response(SSL* s, const std::vector<uint8_t>& ocsp_resp);

#if !defined(_WIN32) || defined(CPPREST_FORCE_HTTP_CLIENT_ASIO)
        // setup server certificate status callback when client includes a certificate status request extension in the TLS handshake
        void set_server_certificate_status_handler(boost::asio::ssl::context& ctx, const nmos::experimental::ocsp_settings& settings);
#endif
    }
}

#endif
