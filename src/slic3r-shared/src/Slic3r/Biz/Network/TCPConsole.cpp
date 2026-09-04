#include "Slic3r/Biz/Network/TCPConsole.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind/bind.hpp>
#include <boost/format.hpp>
#include <Slic3r/Log.hpp>
#include <boost/algorithm/string.hpp>

#include <iostream>
#include <string>

namespace Slic3r::Biz::Network {
void TCPConsole::transmit_next_command()
{
    if (m_cmd_queue.empty()) {
        m_io_context.stop();
        return;
    }

    std::string cmd = m_cmd_queue.front();
    m_cmd_queue.pop_front();

    SPDLOG_DEBUG("TCPConsole: transmitting '{}' to {}:{}", cmd, m_host_name, m_port_name);

    m_send_buffer = cmd + m_newline;

    set_deadline_in(m_write_timeout);
    boost::asio::async_write(
        m_socket,
        boost::asio::buffer(m_send_buffer),
        boost::bind(&TCPConsole::handle_write, this, boost::placeholders::_1, boost::placeholders::_2)
    );
}

void TCPConsole::wait_next_line()
{
    set_deadline_in(m_read_timeout);
    boost::asio::async_read_until(
        m_socket,
        m_recv_buffer,
        m_newline,
        boost::bind(&TCPConsole::handle_read, this, boost::placeholders::_1, boost::placeholders::_2)
    );
}

// TODO: Use std::optional here
std::string TCPConsole::extract_next_line()
{
    char linebuf[1024];
    std::istream is(&m_recv_buffer);
    is.getline(linebuf, sizeof(linebuf));
    return is.good() ? linebuf : std::string{};
}

void TCPConsole::handle_read(
    const boost::system::error_code& ec,
    std::size_t bytes_transferred)
{
    m_error_code = ec;

    if (ec) {
        SPDLOG_ERROR("TCPConsole: Can't read from {}:{}: {}", m_host_name, m_port_name, ec.message());

        m_io_context.stop();
    }
    else {
        std::string line = extract_next_line();
        boost::trim(line);

        SPDLOG_DEBUG("TCPConsole: received '{}' from {}:{}", line, m_host_name, m_port_name);

        boost::to_lower(line);

        if (line == m_done_string)
            transmit_next_command();
        else
            wait_next_line();
    }
}

void TCPConsole::handle_write(
    const boost::system::error_code& ec,
    std::size_t)
{
    m_error_code = ec;
    if (ec) {
        SPDLOG_ERROR("TCPConsole: Can't write to {}:{}: {}", m_host_name, m_port_name, ec.message());

        m_io_context.stop();
    }
    else {
        wait_next_line();
    }
}

void TCPConsole::handle_connect(const boost::system::error_code& ec)
{
    m_error_code = ec;

    if (ec) {
        SPDLOG_ERROR("TCPConsole: Can't connect to {}:{}: {}", m_host_name, m_port_name, ec.message());

        m_io_context.stop();
    }
    else {
        m_is_connected = true;
        SPDLOG_INFO("TCPConsole: connected to {}:{}", m_host_name, m_port_name);

        transmit_next_command();
    }
}

void TCPConsole::set_deadline_in(std::chrono::steady_clock::duration d)
{
    m_deadline = std::chrono::steady_clock::now() + d;
}
bool TCPConsole::is_deadline_over() const
{
    return m_deadline < std::chrono::steady_clock::now();
}

bool TCPConsole::run_queue()
{
    try {
        // TODO: Add more resets and initializations after previous run (reset() method?..)
        set_deadline_in(m_connect_timeout);
        m_is_connected = false;
        m_io_context.restart();

        auto endpoints = m_resolver.resolve(m_host_name, m_port_name);

        m_socket.async_connect(endpoints->endpoint(),
            boost::bind(&TCPConsole::handle_connect, this, boost::placeholders::_1)
        );

        // Loop until we get any reasonable result. Negative result is also result.
        // TODO: Rewrite to more graceful way using deadlime_timer
        bool timeout = false;
        while (!(timeout = is_deadline_over()) && !m_io_context.stopped()) {
            if (m_error_code) {
                m_io_context.stop();
            }
            m_io_context.run_for(boost::asio::chrono::milliseconds(100));
        }

        // Override error message if timeout is set
        if (timeout)
            m_error_code = make_error_code(boost::asio::error::timed_out);

        // Socket is not closed automatically by boost
        m_socket.close();

        if (m_error_code) {
            // We expect that message is logged in handler
            return false;
        }

        // It's expected to have empty queue after successful exchange
        if (!m_cmd_queue.empty()) {
            SPDLOG_ERROR("TCPConsole: command queue is not empty after end of exchange");
            return false;
        }
    }
    catch (std::exception& e)
    {
        SPDLOG_ERROR("TCPConsole: Exception while talking with {}:{}: {}", m_host_name, m_port_name, e.what());

        return false;
    }

    return true;
}
} // namespace Slic3r::Biz::Network