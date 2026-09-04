#pragma once
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "Slic3r/Biz/Network/IHttp.hpp"

namespace Slic3r::Biz::Network {

class MockHttp;

class MockHttpProvider
{
public:
    typedef std::function<
        std::unique_ptr<MockHttp>(IHttp::RequestMethod, const std::string& url, IHttp::RetryFn)>
        CreateMockHttpFn;

    static MockHttpProvider& get()
    {
        static MockHttpProvider instance;
        return instance;
    }

    MockHttpProvider(const MockHttpProvider&)            = delete;
    MockHttpProvider& operator=(const MockHttpProvider&) = delete;
    MockHttpProvider(MockHttpProvider&&)                 = delete;
    MockHttpProvider& operator=(MockHttpProvider&&)      = delete;

    void set_create_fn(CreateMockHttpFn fn)
    {
        m_create_mock_fn = fn;
    }

    CreateMockHttpFn create_fn()
    {
        return m_create_mock_fn;
    }

private:
    CreateMockHttpFn m_create_mock_fn;

    MockHttpProvider()  = default;
    ~MockHttpProvider() = default;
};

class MockHttp : public IHttp
{
public:
    MockHttp(std::string url, IHttp::RetryFn retryfn) :
        IHttp(RequestMethod::Get, std::move(url), retryfn)
    {}

    void invoke_complete(std::string body, unsigned status)
    {
        if (completefn)
            completefn(body, status);
    }

    void invoke_error(std::string body, std::string error, unsigned status)
    {
        if (errorfn)
            errorfn(std::move(body), std::move(error), status);
    }

    MAKE_MOCK1(timeout_connection_mock, void(long));

    IHttp& timeout_connection(long t) override
    {
        timeout_connection_mock(t);
        return *this;
    }

    std::unique_ptr<trompeloeil::expectation> default_allow_timeout;

    MAKE_MOCK1(timeout_total_mock, void(long));

    IHttp& timeout_total(long t) override
    {
        timeout_total_mock(t);
        return *this;
    }

    std::unique_ptr<trompeloeil::expectation> default_allow_size_limit;

    MAKE_MOCK1(size_limit_mock, void(size_t));

    IHttp& size_limit(size_t s) override
    {
        size_limit_mock(s);
        return *this;
    }

    MAKE_MOCK1(set_range_mock, void(const std::string&));

    IHttp& set_range(const std::string& r) override
    {
        set_range_mock(r);
        return *this;
    }

    MAKE_MOCK2(header_mock, void(std::string, const std::string&));

    IHttp& header(std::string k, const std::string& v) override
    {
        header_mock(std::move(k), v);
        return *this;
    }

    MAKE_MOCK1(remove_header_mock, void(std::string));

    IHttp& remove_header(std::string k) override
    {
        remove_header_mock(std::move(k));
        return *this;
    }

    MAKE_MOCK2(auth_digest_mock, void(const std::string&, const std::string&));

    IHttp& auth_digest(const std::string& u, const std::string& p) override
    {
        auth_digest_mock(u, p);
        return *this;
    }

    MAKE_MOCK2(auth_basic_mock, void(const std::string&, const std::string&));

    IHttp& auth_basic(const std::string& u, const std::string& p) override
    {
        auth_basic_mock(u, p);
        return *this;
    }

    MAKE_MOCK1(ca_file_mock, void(const std::string&));

    IHttp& ca_file(const std::string& p) override
    {
        ca_file_mock(p);
        return *this;
    }

    MAKE_MOCK2(form_add_mock, void(const std::string&, const std::string&));

    IHttp& form_add(const std::string& k, const std::string& v) override
    {
        form_add_mock(k, v);
        return *this;
    }

    MAKE_MOCK2(form_add_file_mock, void(const std::string&, const std::string&));

    IHttp& form_add_file(const std::string& name, const boost::filesystem::path& path) override
    {
        form_add_file_mock(name, path.string());
        return *this;
    }

    MAKE_MOCK3(
        form_add_file_with_name_mock,
        void(const std::string&, const std::string&, const std::string&)
    );

    IHttp& form_add_file(
        const std::string& name,
        const boost::filesystem::path& path,
        const std::string& filename
    ) override
    {
        form_add_file_with_name_mock(name, path.string(), filename);
        return *this;
    }

    MAKE_MOCK3(
        form_add_data_mock,
        void(const std::string&, const std::string&, const std::string&)
    );

    IHttp& form_add_data(
        const std::string& name,
        const std::string&& data,
        const boost::filesystem::path& path
    ) override
    {
        form_add_data_mock(
            name,
            data,
            path.string()
        ); // Note: data is passed by value or const ref in mock, careful with moves in mocks
        return *this;
    }

    MAKE_MOCK2(set_put_data_mock, void(std::string, const std::string&));

    IHttp& set_put_data(std::string&& data, const boost::filesystem::path& path) override
    {
        set_put_data_mock(std::move(data), path.string());
        return *this;
    }

#ifdef WIN32
    MAKE_MOCK1(ssl_revoke_best_effort_mock, void(bool));

    IHttp& ssl_revoke_best_effort(bool b) override
    {
        ssl_revoke_best_effort_mock(b);
        return *this;
    }
#endif

    MAKE_MOCK1(set_post_body_path_mock, void(const std::string&));

    IHttp& set_post_body(const boost::filesystem::path& path) override
    {
        set_post_body_path_mock(path.string());
        return *this;
    }

    MAKE_MOCK1(set_post_body_str_mock, void(std::string));

    IHttp& set_post_body(std::string body) override
    {
        set_post_body_str_mock(std::move(body));
        return *this;
    }

    MAKE_MOCK1(set_put_body_mock, void(const std::string&));

    IHttp& set_put_body(const boost::filesystem::path& path) override
    {
        set_put_body_mock(path.string());
        return *this;
    }

    MAKE_MOCK1(cookie_file_mock, void(const std::string&));

    IHttp& cookie_file(const std::string& p) override
    {
        cookie_file_mock(p);
        return *this;
    }

    MAKE_MOCK1(cookie_jar_mock, void(const std::string&));

    IHttp& cookie_jar(const std::string& p) override
    {
        cookie_jar_mock(p);
        return *this;
    }

    MAKE_MOCK1(set_referer_mock, void(const std::string&));

    IHttp& set_referer(const std::string& r) override
    {
        set_referer_mock(r);
        return *this;
    }

    MAKE_MOCK1(perform_sync_mock, void(const HttpRetryOpt&));

    void perform_sync(const HttpRetryOpt& opt) override
    {
        if (m_perform_override_fn) {
            m_perform_override_fn(this, opt);
        } else {
            perform_sync_mock(opt);
        }
    }

    typedef std::function<void(MockHttp*, const HttpRetryOpt&)> PerformOverrideFn;

    void set_perform_override_fn(PerformOverrideFn fn)
    {
        m_perform_override_fn = fn;
    }

private:
    PerformOverrideFn m_perform_override_fn{nullptr};
};

} // namespace Slic3r::Biz::Network
