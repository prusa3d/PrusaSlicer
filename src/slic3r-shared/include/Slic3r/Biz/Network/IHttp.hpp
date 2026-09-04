#pragma once

#include <memory>
#include <string>
#include <functional>
#include <boost/filesystem/path.hpp>
#include <chrono>


namespace Slic3r::Biz::Network {

struct HttpRetryOpt
{
    // if set to zero, no retries at all
    std::chrono::milliseconds initial_delay;
    std::chrono::milliseconds max_delay;
    // if set to zero, retries forever
    size_t max_retries{0};

	static const HttpRetryOpt& no_retry();
    static const HttpRetryOpt& default_retry();
    static constexpr size_t MAX_RETRY_DELAY_MS = 4*  64000;
    static constexpr size_t MAX_RETRIES = 16;
};

class IHttp
{
public:
    enum class  RequestMethod
    {
        Get,
        Post,
        Put
    };

    struct Progress
	{
		size_t dltotal;   // Total bytes to download
		size_t dlnow;     // Bytes downloaded so far
		size_t ultotal;   // Total bytes to upload
		size_t ulnow;     // Bytes uploaded so far
		const  std::string& buffer; // reference to buffer containing all data

		Progress(size_t dltotal, size_t dlnow, size_t ultotal, size_t ulnow, const std::string& buffer) :
			dltotal(dltotal), dlnow(dlnow), ultotal(ultotal), ulnow(ulnow), buffer(buffer)
		{}

        std::string to_string() const;
	};

    struct Retry
    {
        size_t attempt;
        unsigned ms_to_next_attempt;
        bool just_tried; // true if this is the first call since attempt was incremented

        Retry(size_t attempt, unsigned ms_to_next_attempt, bool just_tried) :
            attempt(attempt), ms_to_next_attempt(ms_to_next_attempt), just_tried(just_tried)
        {}
        std::string to_string() const;
    };

    typedef std::function<void(std::string /* body */, unsigned /* http_status */)> CompleteFn;

	/**
     * @brief A HTTP request may fail at various stages of completeness (URL parsing, DNS lookup, TCP connection, ...).
     * If the HTTP request could not be made or failed before completion, the `error` arg contains a description
     * of the error and `http_status` is zero.
     * If the HTTP request was completed but the response HTTP code is >= 400, `error` is empty and `http_status` contains the response code.
     * In either case there may or may not be a body.
     */
	typedef std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> ErrorFn;

	/**
     * @brief See the Progress struct above.
     * Writing true to the `cancel` reference cancels the request in progress.
     */
	typedef std::function<void(Progress, bool& /* cancel */)> ProgressFn;

    /**
     * @brief Called if the request resolved the just-used IP address.
     */
    typedef std::function<void(std::string/* address */)> IPResolveFn;

    /**
     * @brief See the Retry struct above.
     * Writing true to the `cancel` reference cancels the retrying.
     * This function is fired periodically while waiting for the next retry.
     */
    typedef std::function<void(Retry, bool& /* cancel */)> RetryFn;

    /**
     * @brief Callback when headers are read.
     */
    typedef std::function<void(const std::string&)> HeadersReadFn;

    virtual ~IHttp() = default;
    IHttp(IHttp&& other) = delete;
	IHttp(const IHttp& ) = delete;
	IHttp& operator=(const IHttp& ) = delete;
	IHttp& operator=(IHttp&&) = delete;
    
    /**
     * @brief Creates a new HTTP request object. The request is not started until perform() is called.
     * RetryFn callback is called on each try (not only retry but also first try), and periodically when waiting for the next retry.
     * Retry happens on transient errors.
     */
    static std::unique_ptr<IHttp> create(RequestMethod request_method, std::string url, RetryFn fn);

    /**
     * @brief Sets a maximum connection timeout in seconds.
     */
    virtual IHttp& timeout_connection(long timeout) = 0;
    	/**
	 * @brief Sets a maximum total request timeout in seconds.
	 */
	virtual IHttp& timeout_total(long timeout) = 0;
	
	/**
	 * @brief Sets a maximum size of the data that can be received.
	 * A value of zero sets the default m_limit, which is 5MB.
	 */
	virtual IHttp& size_limit(size_t sizeLimit) = 0;
	
	/**
	 * @brief Sets the range of downloaded bytes. 
	 * Example: curl_easy_setopt(curl, CURLOPT_RANGE, "0-199");
	 */
	virtual IHttp& set_range(const std::string& range) = 0;
	
	/**
	 * @brief Sets an HTTP header field.
	 */
	virtual IHttp& header(std::string name, const std::string& value) = 0;
	
	/**
	 * @brief Removes an HTTP header field.
	 */
	virtual IHttp& remove_header(std::string name) = 0;
	
	/**
	 * @brief Authorization by HTTP digest, based on RFC2617.
	 */
	virtual IHttp& auth_digest(const std::string& user, const std::string& password) = 0;
	
	/**
	 * @brief Basic HTTP authorization.
	 */
	virtual IHttp& auth_basic(const std::string& user, const std::string& password) = 0;
	
	/**
	 * @brief Sets a CA certificate file for usage with HTTPS. 
	 * This is only supported on some backends, specifically OpenSSL, and NOT supported with Windows and OS X native certificate store.
	 * See also ca_file_supported().
	 */
	virtual IHttp& ca_file(const std::string& filename) = 0;
	
	/**
	 * @brief Adds a HTTP multipart form field.
	 */
	virtual IHttp& form_add(const std::string& name, const std::string& contents) = 0;
	
	/**
	 * @brief Adds a HTTP multipart form file data contents, `name` is the name of the part.
	 */
	virtual IHttp& form_add_file(const std::string& name, const boost::filesystem::path& path) = 0;
	
	/**
	 * @brief Same as above except also override the file's filename with a custom one.
	 */
	virtual IHttp& form_add_file(const std::string& name, const boost::filesystem::path& path, const std::string& filename) = 0;
	
	/**
	 * @brief Adds file data from memory. Used for POST.
	 */
	virtual IHttp& form_add_data(const std::string &name, const std::string&& data, const boost::filesystem::path& upload_path) = 0;

    /**
     * @brief Adds file data from memory. Used for PUT.
     */
    virtual IHttp& set_put_data(std::string&& body, const boost::filesystem::path& upload_path) = 0;
#ifdef WIN32
	/**
	 * @brief Tells libcurl to ignore certificate revocation checks in case of missing or offline distribution points for those SSL backends where such behavior is present.
	 * This option is only supported for Schannel (the native Windows SSL library).
	 */
	virtual IHttp& ssl_revoke_best_effort(bool set) = 0;
#endif // WIN32

    /**
	 * @brief Set the file contents as a POST request body.
	 * The data is used verbatim, it is not additionally encoded in any way.
	 * This can be used for hosts which do not support multipart requests.
	 */
	virtual IHttp& set_post_body(const boost::filesystem::path& path) = 0;

	/**
	 * @brief Set the POST request body.
	 * The data is used verbatim, it is not additionally encoded in any way.
	 * This can be used for hosts which do not support multipart requests.
	 */
	virtual IHttp& set_post_body(std::string body) = 0;

	/**
	 * @brief Set the file contents as a PUT request body.
	 * The data is used verbatim, it is not additionally encoded in any way.
	 * This can be used for hosts which do not support multipart requests.
	 */
	virtual IHttp& set_put_body(const boost::filesystem::path& path) = 0;


	virtual IHttp& cookie_file(const std::string& file_path) = 0;
	virtual IHttp& cookie_jar(const std::string& file_path) = 0;
	virtual IHttp& set_referer(const std::string& referer) = 0;

    
	/**
	 * @brief Impl 3.0 - lets keep only blocking sync - we have Jthread that anybody can easily implement with passing callbacks to UI.
	 * Starts performing the request on the current thread.
	 */
    virtual void perform_sync(const HttpRetryOpt& retry_opts = HttpRetryOpt::no_retry()) = 0;


    static std::string extract_host_from_url(const std::string& url_in);

	/**
	 * @brief Reduces the given URL to its origin (scheme, host and explicit port).
	 * A URL without a scheme defaults to http, not https, because this is currently
	 * used only for print host URLs, which are typically plain-http on the LAN.
	 * Users needing TLS type https:// into the host field.
	 */
    static std::string get_base_url(const std::string& url_in);
    static std::string substitute_host(const std::string& orig_addr, std::string sub_addr);
    static std::string escape_path_by_element(const boost::filesystem::path& path);

    /**
	 * @brief Converts the given string to URL-encoded string.
	 */
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
     * @brief Returns apex subdomain of url. 
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

    /**
	 * @brief Callback called on HTTP request completion.
	 */
	IHttp& on_complete(CompleteFn fn);

	/**
	 * @brief Callback called on an error occurring at any stage of the request: URL parsing, DNS lookup,
	 * TCP connection, HTTP transfer, and finally also when the response indicates an error (status >= 400).
	 * Therefore, a response body may or may not be present.
	 */
	IHttp& on_error(ErrorFn fn);

	/**
	 * @brief Callback called on data download/upload progress (called fairly frequently).
	 * See the `Progress` structure for description of the data passed.
	 * Writing a true-ish value into the cancel reference parameter cancels the request.
	 */
	IHttp& on_progress(ProgressFn fn);

	/**
	 * @brief Callback called after successful HTTP request (after on_complete callback).
	 * Called if the request resolved the just-used IP address.
	 */
	IHttp& on_ip_resolve(IPResolveFn fn);

    /**
	 * @brief Callback called after headers are read and written into a string.
	 */
    IHttp& on_headers_read(HeadersReadFn fn);

protected:
	
    /**
     * Constructor is protected, use create() to create a new instance.
     */
    IHttp(RequestMethod request_method, std::string&& url, RetryFn fn) 
        : m_request_method(request_method)
        , m_url(std::move(url))
        , retryfn(fn)
    {}

    RequestMethod   m_request_method;
    std::string     m_url;

    IHttp::CompleteFn   	completefn;
	IHttp::ErrorFn      	errorfn;
	IHttp::ProgressFn   	progressfn;
	IHttp::IPResolveFn  	ipresolvefn;
    IHttp::RetryFn      	retryfn;
    IHttp::HeadersReadFn    headersfn;

};

std::ostream& operator<<(std::ostream& , const IHttp::Progress& );

} // namespace Slic3r::Biz::Network
