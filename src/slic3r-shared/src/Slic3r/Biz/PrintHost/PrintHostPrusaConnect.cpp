#include "Slic3r/Biz/PrintHost/PrintHostPrusaConnect.hpp"

#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Time.hpp"

#include <fmt/format.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include <initializer_list>
#include <optional>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PrintHost {

namespace {

constexpr size_t LOG_BODY_LIMIT = 4096;

void report_unauthorized(const PhysicalPrinter::ConnectUpload& auth, unsigned status)
{
    if (status == 401 && auth.on_unauthorized) {
        auth.on_unauthorized(auth.access_token);
    }
}

std::string truncate_for_log(const std::string& text)
{
    if (text.size() <= LOG_BODY_LIMIT) {
        return text;
    }
    return fmt::format("{}... [{} bytes total]", text.substr(0, LOG_BODY_LIMIT), text.size());
}

std::string redact_url(const std::string& url)
{
    const size_t query = url.find('?');
    return query == std::string::npos ? url : url.substr(0, query) + "?<redacted>";
}

void log_request(const char* step, const char* method, const std::string& url, const std::string& body)
{
    SPDLOG_INFO("PrusaConnect: [{}] {} {}\nBody: {}", step, method, redact_url(url), truncate_for_log(body));
}

void log_response(const char* step, bool ok, unsigned status, const std::string& error, const std::string& body)
{
    if (ok) {
        SPDLOG_INFO("PrusaConnect: [{}] HTTP {}: {}", step, status, truncate_for_log(body));
    } else {
        SPDLOG_ERROR(
            "PrusaConnect: [{}] failed: {}, HTTP {}, body: `{}`",
            step,
            error,
            status,
            truncate_for_log(body)
        );
    }
}

std::string validation_errors_text(const nlohmann::json& detail)
{
    std::string text;
    for (const nlohmann::json& item : detail) {
        if (!item.is_object() || !item.contains("msg") || !item["msg"].is_string()) {
            continue;
        }
        std::string location;
        if (item.contains("loc") && item["loc"].is_array()) {
            for (const nlohmann::json& part : item["loc"]) {
                const std::string part_text = part.is_string() ? part.get<std::string>() :
                    part.is_number_integer()                   ? std::to_string(part.get<long long>()) :
                                                                 std::string{};
                if (!part_text.empty()) {
                    location += (location.empty() ? "" : ".") + part_text;
                }
            }
        }
        text += (text.empty() ? "" : "; ") + (location.empty() ? "" : location + ": ") + item["msg"].get<std::string>();
    }
    return text.empty() ? detail.dump() : text;
}

std::optional<std::string> server_message(const std::string& body)
{
    try {
        const nlohmann::json json = nlohmann::json::parse(body);
        if (!json.is_object()) {
            return std::nullopt;
        }
        for (const char* key : {"message", "detail"}) {
            if (auto it = json.find(key); it != json.end()) {
                if (it->is_string()) {
                    return it->get<std::string>();
                }
                if (it->is_array()) {
                    return validation_errors_text(*it);
                }
                if (it->is_object()) {
                    return it->dump();
                }
            }
        }
    } catch (const nlohmann::json::exception&) {
    }
    return std::nullopt;
}

std::string step_error(const std::string& prefix, const std::string& body, const std::string& fallback)
{
    return prefix + ": " + server_message(body).value_or(fallback);
}

std::string authenticated_step_error(
    const std::string& prefix,
    const std::string& body,
    unsigned status,
    const std::string& fallback
)
{
    std::string text = step_error(prefix, body, fallback);
    if (status == 401) {
        text += " " + _u8L("Please log in to Prusa Connect again.");
    }
    return text;
}

std::string string_field(const nlohmann::json& json, const char* key)
{
    if (auto it = json.find(key); it != json.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return {};
}

std::string redact_upload_url(const std::string& body)
{
    try {
        nlohmann::json json = nlohmann::json::parse(body);
        if (json.is_object()) {
            if (auto it = json.find("upload_url"); it != json.end() && it->is_string()) {
                *it = redact_url(it->get<std::string>());
            }
            return json.dump();
        }
    } catch (const nlohmann::json::exception&) {
    }
    return body;
}

bool stop_requested(const IPrintHost::RetryFn& retry_fn)
{
    if (!retry_fn) {
        return false;
    }
    bool stop = false;
    retry_fn(Network::IHttp::Retry(0, 0, false), stop);
    return stop;
}

void attach_cancel_poll(Network::IHttp& http, const IPrintHost::RetryFn& retry_fn)
{
    http.on_progress([retry_fn](Network::IHttp::Progress, bool& cancel) {
        if (stop_requested(retry_fn)) {
            cancel = true;
        }
    });
}

} // namespace

bool PrintHostPrusaConnect::test(std::string& msg, ProgressFn, RetryFn) const
{
    msg.clear();
    return true;
}

bool PrintHostPrusaConnect::create_file_record(
    const PhysicalPrinter::ConnectUpload& auth,
    RetryFn retry_fn,
    ErrorFn error_fn,
    FileRecord& out
) const
{
    static constexpr const char* STEP = "create file";

    boost::system::error_code ec;
    const boost::uintmax_t size = fs::file_size(m_upload_data.source_path, ec);
    if (ec) {
        SPDLOG_ERROR(
            "PrusaConnect: Failed to read file size of {}: {}",
            m_upload_data.source_path.string(),
            ec.message()
        );
        error_fn(_u8L("Failed to read the size of the file to upload."));
        return false;
    }

    nlohmann::json request;
    request["name"]          = m_upload_data.dest_path.filename().string();
    request["size"]          = size;
    std::string request_body = request.dump();

    std::string url = Network::ServiceConfig::instance().connect_organization_files_url(auth.organization_id);
    log_request(STEP, "POST", url, request_body);

    bool res = false;
    std::string response_body;
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Post,
        std::move(url),
        retry_fn
    );
    http->header("Authorization", "Bearer " + auth.access_token)
        .header("Content-Type", "application/json")
        .set_post_body(std::move(request_body))
        .on_complete([&](std::string body, unsigned status) {
            log_response(STEP, true, status, {}, redact_upload_url(body));
            response_body = std::move(body);
            res           = true;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            log_response(STEP, false, status, error, body);
            report_unauthorized(auth, status);
            error_fn(authenticated_step_error(
                _u8L("Failed to register the file in Prusa Connect"),
                body,
                status,
                format_error(body, error, status)
            ));
        });
    attach_cancel_poll(*http, retry_fn);
    http->perform_sync();
    if (!res) {
        return false;
    }

    try {
        const nlohmann::json json = nlohmann::json::parse(response_body);
        if (json.is_object()) {
            out.upload_url = string_field(json, "upload_url");
            if (auto file_it = json.find("file"); file_it != json.end() && file_it->is_object()) {
                out.file_id = string_field(*file_it, "id");
            }
            if (auto it = json.find("upload_headers"); it != json.end() && it->is_object()) {
                for (auto header = it->begin(); header != it->end(); ++header) {
                    if (header.value().is_string()) {
                        out.upload_headers.emplace_back(header.key(), header.value().get<std::string>());
                    }
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("PrusaConnect: [{}] reply is not valid JSON: {}", STEP, e.what());
    }

    if (out.upload_url.empty() || out.file_id.empty()) {
        SPDLOG_ERROR(
            "PrusaConnect: [{}] reply has {} upload URL and {} file id",
            STEP,
            out.upload_url.empty() ? "no" : "an",
            out.file_id.empty() ? "no" : "a"
        );
        error_fn(_u8L("Prusa Connect did not return an upload URL or file id."));
        return false;
    }
    SPDLOG_INFO("PrusaConnect: [{}] file id {}, upload URL {}", STEP, out.file_id, redact_url(out.upload_url));
    return true;
}

bool PrintHostPrusaConnect::upload_to_bucket(
    const FileRecord& record,
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn
) const
{
    static constexpr const char* STEP = "upload";

    log_request(STEP, "PUT", record.upload_url, fmt::format("<file {}>", m_upload_data.source_path.string()));

    bool res = false;
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Put,
        record.upload_url,
        retry_fn
    );
    bool has_content_type = false;
    for (const auto& [name, value] : record.upload_headers) {
        if (boost::iequals(name, "Content-Type")) {
            if (has_content_type) {
                continue;
            }
            has_content_type = true;
        }
        http->header(name, value);
    }
    if (!has_content_type) {
        http->header("Content-Type", "application/octet-stream");
    }
    http->remove_header("Expect");
    http->set_put_body(m_upload_data.source_path)
        .on_complete([&](std::string body, unsigned status) {
            log_response(STEP, true, status, {}, body);
            res = true;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            log_response(STEP, false, status, error, body);
            error_fn(step_error(
                _u8L("Failed to upload the file to Prusa Connect storage"),
                body,
                format_error(body, error, status)
            ));
        })
        .on_progress([&](Network::IHttp::Progress progress, bool& cancel) {
            if (cancel) {
                return;
            }
            if (progress_fn) {
                progress_fn(std::move(progress), cancel);
            }
            if (!cancel && stop_requested(retry_fn)) {
                cancel = true;
            }
            if (cancel) {
                SPDLOG_INFO("PrusaConnect: Upload canceled");
            }
        });
    http->perform_sync();
    return res;
}

bool PrintHostPrusaConnect::finish_file_record(
    const PhysicalPrinter::ConnectUpload& auth,
    const FileRecord& record,
    RetryFn retry_fn,
    ErrorFn error_fn
) const
{
    static constexpr const char* STEP = "finish file";

    std::string url = Network::ServiceConfig::instance().connect_organization_file_url(
        auth.organization_id,
        record.file_id
    );
    nlohmann::json request;
    request["upload_finished_at"] = Slic3r::Utils::iso_ext_utc_timestamp();
    std::string request_body      = request.dump();
    log_request(STEP, "PATCH", url, request_body);

    bool res = false;
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Patch,
        std::move(url),
        retry_fn
    );
    http->header("Authorization", "Bearer " + auth.access_token)
        .header("Content-Type", "application/json")
        .set_post_body(std::move(request_body))
        .on_complete([&](std::string body, unsigned status) {
            log_response(STEP, true, status, {}, body);
            res = true;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            log_response(STEP, false, status, error, body);
            report_unauthorized(auth, status);
            error_fn(authenticated_step_error(
                _u8L("Failed to finish the file upload in Prusa Connect"),
                body,
                status,
                format_error(body, error, status)
            ));
        });
    attach_cancel_poll(*http, retry_fn);
    http->perform_sync();
    return res;
}

bool PrintHostPrusaConnect::enqueue_on_printer(
    const PhysicalPrinter::ConnectUpload& auth,
    const FileRecord& record,
    RetryFn retry_fn,
    ErrorFn error_fn
) const
{
    static constexpr const char* STEP = "queue";

    std::string request_body;
    try {
        const nlohmann::json data = m_upload_data.request_body_json.empty() ?
            nlohmann::json::object() :
            nlohmann::json::parse(m_upload_data.request_body_json);
        nlohmann::json request;
        request["file_id"]         = record.file_id;
        request["organization_id"] = auth.organization_id;
        if (data.is_object()) {
            using TypeCheck = bool (*)(const nlohmann::json&);
            const std::initializer_list<std::pair<const char*, TypeCheck>> forwarded = {
                {"set_ready", [](const nlohmann::json& j) { return j.is_boolean(); }},
                {"position", [](const nlohmann::json& j) { return j.is_number_integer(); }},
                {"tool_mapping", [](const nlohmann::json& j) { return j.is_object(); }},
                {"destination", [](const nlohmann::json& j) { return j.is_string(); }},
            };
            for (const auto& [key, has_expected_type] : forwarded) {
                if (auto it = data.find(key); it != data.end() && has_expected_type(*it)) {
                    request[key] = *it;
                }
            }
        }
        request_body = request.dump();
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("PrusaConnect: Could not build the queue request: {}", e.what());
        error_fn(_u8L("Invalid print request data."));
        return false;
    }

    std::string url = Network::ServiceConfig::instance().connect_printer_queue_url(auth.printer_uuid);
    log_request(STEP, "POST", url, request_body);

    bool res = false;
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Post,
        std::move(url),
        retry_fn
    );
    http->header("Authorization", "Bearer " + auth.access_token)
        .header("Content-Type", "application/json")
        .set_post_body(std::move(request_body))
        .on_complete([&](std::string body, unsigned status) {
            log_response(STEP, true, status, {}, body);
            res = true;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            log_response(STEP, false, status, error, body);
            report_unauthorized(auth, status);
            error_fn(authenticated_step_error(
                _u8L("Failed to send the file to the printer"),
                body,
                status,
                format_error(body, error, status)
            ));
        });
    attach_cancel_poll(*http, retry_fn);
    http->perform_sync();
    return res;
}

bool PrintHostPrusaConnect::perform(
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn,
    InfoFn info_fn
) const
{
    const PhysicalPrinter::ConnectUpload* auth = std::get_if<PhysicalPrinter::ConnectUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    if (auth->organization_id.empty()) {
        SPDLOG_ERROR("PrusaConnect: No organization id for printer {}", auth->printer_uuid);
        error_fn(_u8L("Prusa Connect did not provide an organization for the selected printer."));
        return false;
    }

    const std::string printer_page_url =
        Network::ServiceConfig::instance().connect_printer_page_url(auth->printer_uuid);
    if (!printer_page_url.empty()) {
        info_fn(PrintHostJobInfoTag::ConnectPrinterAddress, printer_page_url);
    }

    SPDLOG_INFO(
        "PrusaConnect: Uploading {} to printer {} (organization {})",
        m_upload_data.dest_path.filename().string(),
        auth->printer_uuid,
        auth->organization_id
    );

    FileRecord record;
    if (!create_file_record(*auth, retry_fn, error_fn, record)) {
        return false;
    }
    if (!upload_to_bucket(record, progress_fn, retry_fn, error_fn)) {
        return false;
    }
    if (!finish_file_record(*auth, record, retry_fn, error_fn)) {
        return false;
    }
    return enqueue_on_printer(*auth, record, retry_fn, error_fn);
}

} // namespace Slic3r::Biz::PrintHost
