#pragma once

using namespace boost::posix_time;

#include <elle/bytes.hh>

#include <elle/reactor/Barrier.hh>
#include <elle/reactor/network/tcp-server.hh>

#include <elle/protocol/exceptions.hh>

#include <memo/cli/utility.hh>

namespace memo
{
  namespace networking
  {
    namespace
    {
      struct RPCs
        : public memo::RPCServer
      {
        RPCs(elle::Version const& version)
          : memo::RPCServer(version)
          , _version(version)
        {
          this->add("match_versions",
                    [this, version] (elle::Version const& client_version)
                    {
                      ELLE_TRACE("version (%s) asked by client (%s)",
                                 version, client_version);
                      if (client_version < this->_version)
                      {
                        ELLE_WARN("Behave as the remote version (%s)", client_version);
                        this->_version = client_version;
                      }
                      return version;
                    });

          this->add("download",
                    [this] (elle::Buffer const& body)
                    {
                      ELLE_TRACE("%s uploaded to server (version: %s)",
                                 body.size(), this->_version);
                      return body.size();
                    });

          this->add("upload",
                    [this] (elle::Buffer::Size size)
                    {
                      ELLE_TRACE("download %s from server (version: %s)",
                                 size, this->_version);
                      return elle::Buffer(size);
                    });
        }

        ELLE_ATTRIBUTE_R(elle::Version, version);
      };

      std::string
      report(elle::Buffer::Size size,
             time_duration duration)
      {
        return elle::sprintf(
          "%sms for %s (%s/sec)",
          duration.total_milliseconds(),
          elle::human_data_size(size, false),
          elle::human_data_size(1000. * size / duration.total_milliseconds(), false));
      }

      time_duration
      upload(elle::protocol::ChanneledStream& channels,
             elle::Buffer::Size packet_size,
             int64_t packets_count,
             bool verbose)
      {
        ELLE_TRACE_SCOPE("upload %s %s times", packet_size, packets_count);
        std::cout << "  Upload:" << std::endl;
        // My download is the server upload.
        memo::RPC<elle::Buffer::Size (elle::Buffer)> upload(
          "download", channels, memo::version());

        elle::Buffer buff(packet_size);
        auto start = microsec_clock::universal_time();
        for (int64_t i = 0; i < packets_count; ++i)
          ELLE_DEBUG("upload...")
          {
            auto start_partial = microsec_clock::universal_time();
            upload(buff);
            if (verbose)
            {
              std::cout
                << "    - [" << i << "] "
                << report(packet_size,
                          microsec_clock::universal_time() - start_partial)
                << std::endl;
            }
          }
        auto res = microsec_clock::universal_time() - start;
        std::cout << "    "
                  << report(packet_size * packets_count, res)
                  << std::endl;
        return res;
      }

      time_duration
      download(elle::protocol::ChanneledStream& channels,
               elle::Buffer::Size packet_size,
               int64_t packets_count,
               bool verbose)
      {
        ELLE_TRACE_SCOPE("download %s %s times", packet_size, packets_count);

        // My upload is the server download.
        memo::RPC<elle::Buffer (elle::Buffer::Size)> download(
          "upload", channels, memo::version());

        std::cout << "  Download:" << std::endl;
        auto start = microsec_clock::universal_time();
        for (int64_t i = 0; i < packets_count; ++i)
        {
          ELLE_DEBUG("download...")
          {
            auto start_partial = microsec_clock::universal_time();
            download(packet_size);
            if (verbose)
              std::cout << "    - [" << i << "] "
                        << report(packet_size,
                                  microsec_clock::universal_time() - start_partial)
                        << std::endl;
          }
        }
        auto res = microsec_clock::universal_time() - start;
        std::cout << "    "
                  << report(packet_size * packets_count, res)
                  << std::endl;
        return res;
      }

      void
      serve(elle::Version const& version,
            elle::IOStream& socket,
            bool verbose)
      {
        try
        {
          if (verbose)
            std::cout << "New connection" << std::endl;
          RPCs rpc_server(version);
          ELLE_TRACE("serve rpcs")
            rpc_server.serve(socket);
        }
        catch (elle::reactor::Terminate const&)
        {
          throw;
        }
        catch (...)
        {
          ELLE_WARN("ended");
        }
      }

      namespace tcp
      {
        void
        serve(uint16_t& port,
              elle::Version const& version,
              elle::reactor::Barrier& listening,
              bool verbose)

        {
          ELLE_TRACE("create tcp server (listening on port: %s)", port);
          auto server = std::make_unique<elle::reactor::network::TCPServer>();
          server->listen(port);
          port = server->port();
          if (verbose)
            std::cout << "  Listen to tcp connections on port " << port
                      << std::endl;
          listening.open();
          elle::With<elle::reactor::Scope>() <<  [&] (elle::reactor::Scope& s)
          {
            s.run_background(
              "acceptor", [&]
              {
                while (true)
                {
                  ELLE_DEBUG("waiting for a new connection");
                  auto socket = elle::utility::move_on_copy(server->accept());
                  s.run_background(
                    elle::sprintf("socket %s", socket),
                    [version, socket, verbose]
                    {
                      memo::networking::serve(version, **socket, verbose);
                    });
                }
              });
            elle::reactor::yield();
            s.wait();
          };
        }

        std::unique_ptr<elle::reactor::network::TCPSocket>
        socket(std::string const& host, uint16_t port)
        {
          ELLE_TRACE("open tcp socket to %s:%s", host, port);
          return std::make_unique<elle::reactor::network::TCPSocket>(host, port, 5_sec);
        }
      }

      namespace utp
      {
        void
        serve(uint16_t& port,
              int xorit,
              elle::Version const& version,
              elle::reactor::Barrier& listening,
              bool verbose)
        {
          ELLE_TRACE("create utp server (listening on port: %s) (xor: %s)",
                     port, xorit);
          elle::reactor::network::UTPServer server;
          server.xorify(xorit);
          server.listen(port);
          port = server.local_endpoint().port();
          if (verbose)
          {
            std::cout << "  Listen to ";
            if (xorit)
              std::cout << "xored ";
            std::cout << "utp connections on port " << port << std::endl;
          }
          server.rdv_connect("connectivity-server:" + std::to_string(port),
                             "rdv.infinit.sh:7890");
          listening.open();
          elle::With<elle::reactor::Scope>() <<  [&] (elle::reactor::Scope& s)
          {
            s.run_background(
              "acceptor", [&]
              {
                while (true)
                {
                  ELLE_DEBUG("waiting for a new connection");
                  auto socket = elle::utility::move_on_copy(server.accept());
                  s.run_background(
                    elle::sprintf("socket %s", socket),
                    [version, socket, verbose]
                    {
                      memo::networking::serve(version, **socket, verbose);
                    });
                }
              });
            elle::reactor::yield();
            s.wait();
          };
        }

        std::unique_ptr<elle::reactor::network::UTPSocket>
        socket(elle::reactor::network::UTPServer& server,
               std::string const& host,
               uint16_t port,
               uint8_t xorit = 0)
        {
          ELLE_TRACE("open utp socket to %s:%s (xor: %s)", host, port, xorit);
          server.listen(0);
          auto s = std::make_unique<elle::reactor::network::UTPSocket>(server);
          server.xorify(xorit);
          s->connect(host, port);
          return s;
        }
      }

      enum class Operations
      {
        all = 1,
        upload = 2,
        download = 3
      };

      Operations
      get_mode(boost::optional<std::string> const& mode)
      {
        if (!mode)
          return Operations::all;
        else if (*mode == "upload")
          return Operations::upload;
        else if (*mode == "download")
          return Operations::download;
        else
          elle::err("unknown mode '%s'", *mode);
      }

      /// Variable "port".
      uint16_t
      get_port(boost::optional<uint16_t> port)
      {
        return port.value_or(0);
      }

      uint16_t
      get_tcp_port(boost::optional<uint16_t> tcp_port,
                   boost::optional<uint16_t> port)
      {
        auto res = tcp_port.value_or(port.value_or(0));
        ELLE_DEBUG("tcp port: %s", res);
        return res;
      }

      uint16_t
      get_utp_port(boost::optional<uint16_t> utp_port,
                   boost::optional<uint16_t> port)
      {
        auto res = utp_port.value_or(port.value_or(0) + 1);
        if (res == 1)
          res = 0;
        ELLE_DEBUG("utp port: %s", res);
        return res;
      }

      uint16_t
      get_xored_utp_port(boost::optional<uint16_t> xored_utp_port,
                         boost::optional<uint16_t> utp_port,
                         boost::optional<uint16_t> port)
      {
        auto res = xored_utp_port.value_or(port.value_or(0) + 2);
        if (res == 2) // Random port.
        {
          res = get_utp_port(utp_port, port);
          if (res)
            ++res;
        }
        ELLE_DEBUG("xored utp port: %s", res);
        return res;
      }

      bool
      xored_equal(boost::optional<std::string> const& xored,
                  std::string const& value)
      {
        return !xored
          || *xored == "both"
          || *xored == value;
      }

      bool
      xored_enabled(boost::optional<std::string> const& xored)
      {
        return xored_equal(xored, "yes");
      }

      bool
      non_xored_enabled(boost::optional<std::string> const& xored)
      {
        return xored_equal(xored, "no");
      }

      void
      packet_size_resolve(boost::optional<elle::Buffer::Size>& packet_size)
      {
        if (!packet_size)
          packet_size = 1024 * 1024;
        if (!*packet_size)
          elle::err("--packets_count must be greater than 0");
      }

      void
      packets_count_resolve(boost::optional<int64_t>& packets_count)
      {
        if (!packets_count)
          packets_count = 5;
        if (!*packets_count)
          elle::err("--packets_count must be greater than 0");
      }

      struct Servers
      {
        Servers(boost::optional<std::string> const& protocol_name,
                boost::optional<uint16_t> port,
                boost::optional<uint16_t> tcp_port,
                boost::optional<uint16_t> utp_port,
                boost::optional<uint16_t> xored_utp_port,
                boost::optional<std::string> const& xored,
                bool verbose,
                elle::Version const& v)
          : _version(v)
          , _tcp()
          , _utp()
          , _xored_utp()
        {
          auto protocol = cli::protocol_get(protocol_name);
          elle::reactor::Barrier listening;
          auto base_port = get_port(port);
          uint16_t tcp_port_ = 0;
          uint16_t utp_port_ = 0;
          uint16_t xored_utp_port_ = 0;
          if (protocol.with_tcp())
          {
            tcp_port_ = get_tcp_port(tcp_port, port);
            this->_tcp.reset(
              new elle::reactor::Thread(
                "tcp",
                [&]
                {
                  tcp::serve(tcp_port_, this->_version, listening, verbose);
                }));
            listening.wait();
            listening.close();
          }
          if (protocol.with_utp())
          {
            if (non_xored_enabled(xored))
            {
              utp_port_ = get_utp_port(utp_port, port);
              this->_utp.reset(
                new elle::reactor::Thread(
                  "utp",
                  [&]
                  {
                    utp::serve(utp_port_, 0, this->_version, listening,
                               verbose);
                  }));
              listening.wait();
              listening.close();
            }
            if (xored_enabled(xored))
            {
              xored_utp_port_ = get_xored_utp_port(xored_utp_port, utp_port, port);
              this->_xored_utp.reset(
                new elle::reactor::Thread(
                  "utp", [&]
                  {
                    utp::serve(xored_utp_port_, 255, this->_version, listening,
                               verbose);
                  }));
              listening.wait();
              listening.close();
            }
          }

          std::cout
            << std::endl
            << "To perform tests, run the following command from another node:"
            << std::endl;
          std::cout << "> memo doctor networking";
          if (!protocol.with_all())
            std::cout << " --protocol " << protocol;
          if (base_port)
            std::cout << " --port " << get_port(port);
          else
          {
            if (tcp_port_)
              std::cout << " --tcp-port " << tcp_port_;
            if (utp_port_)
              std::cout << " --utp-port " << utp_port_;
            if (xored_utp_port_)
              std::cout << " --xored-utp-port " << xored_utp_port_;
          }
          std::cout << " --host <address_of_this_machine>" <<std::endl;
        }

        Servers(Servers&& servers)
          : _version(servers._version)
          , _tcp(servers._tcp.release())
          , _utp(servers._utp.release())
          , _xored_utp(servers._xored_utp.release())
        {}

        ~Servers()
        {
          if (this->_xored_utp)
            this->_xored_utp->terminate_now();
          if (this->_utp)
            this->_utp->terminate_now();
          if (this->_tcp)
            this->_tcp->terminate_now();
        }

        ELLE_ATTRIBUTE_R(elle::Version, version);
        ELLE_ATTRIBUTE_R(std::unique_ptr<elle::reactor::Thread>, tcp);
        ELLE_ATTRIBUTE_R(std::unique_ptr<elle::reactor::Thread>, utp);
        ELLE_ATTRIBUTE_R(std::unique_ptr<elle::reactor::Thread>, xored_utp);
      };

      void
      perform(boost::optional<std::string> const& mode_name,
              boost::optional<std::string> const& protocol_name,
              boost::optional<elle::Buffer::Size> packet_size,
              boost::optional<int64_t> packets_count,
              std::string const& host,
              boost::optional<uint16_t> port,
              boost::optional<uint16_t> tcp_port,
              boost::optional<uint16_t> utp_port,
              boost::optional<uint16_t> xored_utp_port,
              boost::optional<std::string> const& xored,
              bool verbose,
              elle::Version const& _version)
      {
        elle::Version v = _version;
        auto protocol = cli::protocol_get(protocol_name);
        auto mode = get_mode(mode_name);
        packets_count_resolve(packets_count);
        packet_size_resolve(packet_size);

        auto action = [&] (elle::protocol::ChanneledStream& stream) {
          if (mode == Operations::all || mode == Operations::upload)
          {
            try
            {
              upload(stream, *packet_size, *packets_count, verbose);
            }
            catch (elle::reactor::network::Error const&)
            {
              std::cerr << "  Something went wrong during upload:"
                        << elle::exception_string()
                        << std::endl;
            }
          }
          if (mode == Operations::all || mode == Operations::download)
          {
            try
            {
              download(stream, *packet_size, *packets_count, verbose);
            }
            catch (elle::reactor::network::Error const&)
            {
              std::cerr << "  Something went wrong during download:"
                        << elle::exception_string()
                        << std::endl;
            }
          }
        };

        auto match_versions = [&] (elle::protocol::ChanneledStream& stream) {
          memo::RPC<elle::Version (elle::Version const&)> get_version{
            "match_versions", stream, v
          };
          try
          {
            auto remote_version = get_version(_version);
            if (v > remote_version)
            {
              ELLE_WARN("Behave as the remote version (%s)", remote_version);
              v = remote_version;
            }
          }
          // If protocol aren't compatible, it might just stall...
          // XXX: Add a timeout.
          catch (elle::protocol::Error const& error)
          {
            elle::err("Protocol error establishing connection with the remote.\n"
                      "Make sure it uses the same version or use "
                      "--compatibility-version");
          }
        };

        if (protocol.with_tcp())
        {
          auto tcp = [&]
          {
            ELLE_TRACE_SCOPE("TCP: Client");
            std::cout << "TCP:" << std::endl;
            std::unique_ptr<elle::reactor::network::TCPSocket> socket;
            try
            {
              socket.reset(tcp::socket(host, get_tcp_port(tcp_port, port)).release());
            }
            catch (elle::reactor::network::Error const&)
            {
              std::cerr << "  Unable to establish connection: "
                        << elle::exception_string()
                        << std::endl;
              return;
            }
            elle::protocol::Serializer serializer{
              *socket, memo::elle_serialization_version(v), false};
            auto&& stream = elle::protocol::ChanneledStream{serializer};
            match_versions(stream);
            action(stream);
          };
          tcp();
        }
        if (protocol.with_utp())
        {
          auto utp = [&] (bool xored)
            {
              elle::reactor::network::UTPServer server;
              auto socket = [&]() -> std::unique_ptr<elle::reactor::network::UTPSocket> {
                try
                {
                  return utp::socket(
                      server,
                      host,
                      xored
                      ? get_xored_utp_port(xored_utp_port, utp_port, port)
                      : get_utp_port(utp_port, port),
                      xored ? 0xFF : 0);
                }
                catch (elle::reactor::network::Error const&)
                {
                  std::cerr << "  Unable to establish connection: "
                            << elle::exception_string()
                            << std::endl;
                  return nullptr;
                }
              }();
              elle::protocol::Serializer serializer{
                *socket, memo::elle_serialization_version(v), false};
              auto&& stream = elle::protocol::ChanneledStream{serializer};
              match_versions(stream);
              action(stream);
            };
          if (non_xored_enabled(xored))
          {
            std::cout << "UTP:" << std::endl;
            utp(false);
          }
          if (xored_enabled(xored))
          {
            std::cout << "xored UTP:" << std::endl;
            utp(true);
          }
        }
      }
    }
  }
}
