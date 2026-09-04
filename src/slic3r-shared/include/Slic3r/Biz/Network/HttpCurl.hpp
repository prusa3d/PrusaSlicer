#pragma once

#include "Slic3r/Biz/Network/IHttp.hpp"

#include <curl/curl.h>
#include <deque>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem.hpp>

namespace Slic3r::Biz::Network {

class HttpCurl : public IHttp {
public:
    ~HttpCurl() override;
    HttpCurl(HttpCurl&& other) = delete;
	HttpCurl(const HttpCurl& ) = delete;
	HttpCurl& operator=(const HttpCurl& ) = delete;
	HttpCurl& operator=(HttpCurl&&) = delete;

	/**
	 * @brief This constructor should be called only from IHttp::create.
	 */
    HttpCurl(RequestMethod request_method, std::string&& url, RetryFn fn);

	/**
	 * @brief Sets a maximum connection timeout in seconds.
	 */
	IHttp& timeout_connection(long timeout) override;

	/**
	 * @brief Sets a maximum total request timeout in seconds.
	 */
	IHttp& timeout_total(long timeout) override;

	/**
	 * @brief Sets a maximum size of the data that can be received. A value of zero sets the default m_limit, which is 5MB.
	 */
	IHttp& size_limit(size_t sizeLimit) override;

	/**
	 * @brief Sets the range of downloaded bytes. Example: curl_easy_setopt(curl, CURLOPT_RANGE, "0-199").
	 */
	IHttp& set_range(const std::string& range) override;

	/**
	 * @brief Sets an HTTP header field.
	 */
	IHttp& header(std::string name, const std::string& value) override;

	/**
	 * @brief Removes an HTTP header field.
	 */
	IHttp& remove_header(std::string name) override;

	/**
	 * @brief Authorization by HTTP digest, based on RFC2617.
	 */
	IHttp& auth_digest(const std::string& user, const std::string& password) override;

	/**
	 * @brief Basic HTTP authorization.
	 */
	IHttp& auth_basic(const std::string& user, const std::string& password) override;

	/**
	 * @brief Sets a CA certificate file for usage with HTTPS. This is only supported on some backends, specifically OpenSSL, and NOT supported with Windows and OS X native certificate store. See also ca_file_supported().
	 */
	IHttp& ca_file(const std::string& filename) override;

	/**
	 * @brief Adds a HTTP multipart form field.
	 */
	IHttp& form_add(const std::string& name, const std::string& contents) override;

	/**
	 * @brief Adds a HTTP multipart form file data contents, `name` is the name of the part.
	 */
	IHttp& form_add_file(const std::string& name, const boost::filesystem::path& path) override;

	/**
	 * @brief Same as above except also override the file's filename with a custom one.
	 */
	IHttp& form_add_file(const std::string& name, const boost::filesystem::path& path, const std::string& filename) override;

	/**
	 * @brief Adds file data from memory. Post only.
	 */
	IHttp& form_add_data(const std::string& name, const std::string&& data, const boost::filesystem::path& upload_path) override;

    
	/**
	 * @brief Adds file data from memory. Put only.
	 */
    IHttp& set_put_data(std::string&& body, const boost::filesystem::path& upload_path) override;

#ifdef WIN32
	/**
	 * @brief Tells libcurl to ignore certificate revocation checks in case of missing or offline distribution points for those SSL backends where such behavior is present. 
     * This option is only supported for Schannel (the native Windows SSL library).
	 */
	IHttp& ssl_revoke_best_effort(bool set);
#endif // WIN32

	/**
	 * @brief Sets the file contents as a POST request body.
	 * The data is used verbatim, it is not additionally encoded in any way.
	 * This can be used for hosts which do not support multipart requests.
	 */
	IHttp& set_post_body(const boost::filesystem::path& path) override;

	/**
	 * @brief Sets the POST request body.
	 * The data is used verbatim, it is not additionally encoded in any way.
	 * This can be used for hosts which do not support multipart requests.
	 * Use std::move to avoid copying the string.
	 */
	IHttp& set_post_body(std::string body) override;

	/**
	 * @brief Sets the file contents as a PUT request body.
	 * The data is used verbatim, it is not additionally encoded in any way.
	 * This can be used for hosts which do not support multipart requests.
	 */
	IHttp& set_put_body(const boost::filesystem::path& path) override;

	IHttp& cookie_file(const std::string& file_path) override;
	IHttp& cookie_jar(const std::string& file_path) override;
	IHttp& set_referer(const std::string& referer) override;

	/**
	 * @brief Starts performing the request on the current thread.
	 */
    void perform_sync(const HttpRetryOpt& retry_opts = HttpRetryOpt::no_retry()) override;

    static std::string extract_host_from_url(const std::string& url_in);
    static std::string get_base_url(const std::string& url_in);
    static std::string substitute_host(const std::string& orig_addr, std::string sub_addr);
    static std::string escape_path_by_element(const boost::filesystem::path& path);
    static std::string escape_string(const std::string& str);

    /**
	 * @brief Converts the given URL-encoded string to plain string.
	 */
    static std::string unescape_string(const std::string& str);

    /*
     * @brief Returns true if 'domain' is subdomain of 'url'
     */
    static bool is_subdomain(const std::string& url, const std::string& domain);

    /*
     * @brief Returns apex domain of url.
     * Note: libcurl extracts the full hostname.
     * Accurately extracting the apex domain across all valid top-level domains 
     * (e.g., distinguishing .com from .co.uk) requires parsing against the Public Suffix List (via libraries like libpsl). 
     * This function uses a naive right-to-left delimiter split 
     * which works for simple TLDs as requested, but is not robust for compound TLDs.
     */
    static std::string get_apex_domain(const std::string& url); 

    /**
     * @brief Tells whether current backend supports setting up a CA file using ca_file()
     */
	static bool ca_file_supported();

    /**
     * Return empty string on success or error message on fail.
     */
    static std::string tls_global_init();
    /**
     * Return empty string on success or error message on fail.
     */    
    static std::string tls_system_cert_store();

private:
    struct CurlDeleter {
        void operator()(CURL* ptr) const { if (ptr) curl_easy_cleanup(ptr); }
    };
    std::unique_ptr<CURL, CurlDeleter> m_curl;

    struct FormDeleter {
        void operator()(curl_httppost* ptr) const { if (ptr) curl_formfree(ptr); }
    };
    std::unique_ptr<curl_httppost, FormDeleter> m_form;
    curl_httppost* m_form_end = nullptr; // form_end does not own the memory

    struct SListDeleter {
        void operator()(curl_slist* ptr) const { if (ptr) curl_slist_free_all(ptr); }
    };
    std::unique_ptr<curl_slist, SListDeleter> m_header_list;

	/**
	 * @brief Used for reading the body.
	 */
    std::string m_buffer;
	/**
	 * @brief Used for storing file streams added as multipart form parts.
	 * Using a deque here because unlike vector it doesn't invalidate pointers on insertion.
	 */
	std::deque<boost::filesystem::ifstream> m_form_files;
	std::string m_post_fields;
	std::string m_error_buffer;    // Used for CURLOPT_ERRORBUFFER
    size_t m_limit {0};
    std::unique_ptr<boost::filesystem::ifstream> m_put_file;
    bool m_cancel {false};

	static bool ca_file_supported_inner(::CURL *curl);
	static size_t writecb(void *data, size_t size, size_t nmemb, void *userp);
    static size_t headercb(void *data, size_t size, size_t nmemb, void *userp);
	static int xfercb(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
	static int xfercb_legacy(void *userp, double dltotal, double dlnow, double ultotal, double ulnow);
	static size_t form_file_read_cb(char *buffer, size_t size, size_t nitems, void *userp);
	/**
	 * @brief Used instead of form_file_read_cb when file data is in memory (m_upload_data).
	 */
    static size_t form_memory_read_cb(char *buffer, size_t size, size_t nitems, void *userp);

	void form_add_file_inner(const char *name, const boost::filesystem::path& path, const char* filename);

	std::string curl_error(CURLcode curlcode);
	std::string body_size_error();

    std::string m_mime_data;
    boost::filesystem::path m_mime_path;

    std::string m_upload_data;
    boost::filesystem::path m_upload_path;
};



} //Slic3r::Biz::Network