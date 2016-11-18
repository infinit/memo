#ifndef INFINIT_BIN_NETWORKING_HH
# define INFINIT_BIN_NETWORKING_HH

using namespace boost::posix_time;

# include <reactor/Barrier.hh>

# include <protocol/exceptions.hh>

# include <infinit/model/doughnut/protocol.hh>

# include <version.hh>

namespace bytes
{
  // XXX: Move that somewhere to elle/elle/src/elle/bytes/conversion.hh or
  // something like that.
  static std::vector<std::pair<std::string, std::string>> capacities{
    { "B",  "B"   },
    { "kB", "KiB" },
    { "MB", "MiB" },
    { "GB", "GiB" },
    { "TB", "TiB" },
    { "EB", "EiB" },
    { "ZB", "ZiB" },
  };

  std::string
  to_human(uint64_t bytes,
           bool si = true)
  {
    for (uint64_t i = 1; i < capacities.size() + 1; ++i)
    {
      if ((double) bytes / pow((si ? 1024 : 1000), i) < 1 || i == (capacities.size()))
        return elle::sprintf("%.1f %s",
                             bytes / pow((si ? 1024 : 1000), i - 1),
                             (si ? capacities[i - 1].second : capacities[i - 1].first));
    }
    elle::unreachable();
  }
}

namespace infinit
{
  namespace networking
  {
    struct RPCs
      : public infinit::RPCServer
    {
      RPCs(elle::Version const& version = ::version)
        : infinit::RPCServer(version)
        , _version(version)
      {
        this->add("match_versions",
                  std::function<elle::Version (elle::Version)>(
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
                  }));

        this->add("download",
                  std::function<elle::Buffer::Size (elle::Buffer const&)>(
                  [this] (elle::Buffer const& body)
                  {
                    ELLE_TRACE("%s uploaded to server (version: %s)",
                               body.size(), this->_version);
                    return body.size();
                  }));

        this->add("upload",
                  std::function<elle::Buffer (elle::Buffer::Size)>(
                  [this] (elle::Buffer::Size size)
                  {
                    ELLE_TRACE("download %s from server (version: %s)",
                               size, this->_version);
                    return elle::Buffer(size);
                  }));
      }

      ELLE_ATTRIBUTE_R(elle::Version, version);
    };

    static
    std::string
    report(elle::Buffer::Size size,
           time_duration duration)
    {
      return elle::sprintf(
        "%sms for %s (%s/sec)",
        duration.total_milliseconds(),
        bytes::to_human(size, false),
        bytes::to_human((double) 1000 * size / duration.total_milliseconds(), false));
    }

    static
    time_duration
    upload(infinit::protocol::ChanneledStream& channels,
           elle::Buffer::Size packet_size,
           int64_t packets_count,
           bool verbose)
    {
      ELLE_TRACE_SCOPE("upload %s %s times", packet_size, packets_count);
      std::cout << "  Upload:" << std::endl;
      // My download is the server upload.
      infinit::RPC<elle::Buffer::Size (elle::Buffer)> upload(
        "download", channels, ::version);

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
      auto diff = microsec_clock::universal_time() - start;
      std::cout << "    "
                << report(packet_size * packets_count, diff)
                << std::endl;
      return diff;
    }

    static
    time_duration
    download(infinit::protocol::ChanneledStream& channels,
             elle::Buffer::Size packet_size,
             int64_t packets_count,
             bool verbose)
    {
      ELLE_TRACE_SCOPE("download %s %s times", packet_size, packets_count);

      // My upload is the server download.
      infinit::RPC<elle::Buffer (elle::Buffer::Size)> download(
        "upload", channels, ::version);

      std::cout << "  Download:" << std::endl;
      auto start = microsec_clock::universal_time();
      for (int64_t i = 0; i < packets_count; ++i)
      {
        ELLE_DEBUG("download...")
        {
          auto start_partial = microsec_clock::universal_time();
          download(packet_size);
          if (verbose)
          {
            std::cout << "    - [" << i << "] "
                      << report(packet_size,
                                microsec_clock::universal_time() - start_partial)
                      << std::endl;
          }
        }
      }
      auto diff = microsec_clock::universal_time() - start;
      std::cout << "    "
                << report(packet_size * packets_count, diff)
                << std::endl;
      return diff;
    }

    static
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
      catch (reactor::Terminate const&)
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
      static
      void
      serve(uint16_t& port,
            elle::Version const& version,
            reactor::Barrier& listening,
            bool verbose)

      {
        ELLE_TRACE("create tcp server (listening on port: %s)", port);
        auto server = elle::make_unique<reactor::network::TCPServer>();
        server->listen(port);
        port = server->port();
        if (verbose)
          std::cout << "  Listen to tcp connections on port " << port
                    << std::endl;
        listening.open();
        elle::With<reactor::Scope>() <<  [&] (reactor::Scope& s)
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
                    infinit::networking::serve(version, **socket, verbose);
                  });
              }
            });
          reactor::yield();
          s.wait();
        };
      }

      static
      std::unique_ptr<reactor::network::TCPSocket>
      socket(std::string const& host, uint16_t port)
      {
        ELLE_TRACE("open tcp socket to %s:%s", host, port);
        return std::make_unique<reactor::network::TCPSocket>(host, port, 5_sec);
      }
    }

    namespace utp
    {
      static
      void
      serve(uint16_t& port,
            int xorit,
            elle::Version const& version,
            reactor::Barrier& listening,
            bool verbose)
      {
        ELLE_TRACE("create utp server (listening on port: %s) (xor: %s)",
                   port, xorit);
        reactor::network::UTPServer server;
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
        elle::With<reactor::Scope>() <<  [&] (reactor::Scope& s)
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
                    infinit::networking::serve(version, **socket, verbose);
                  });
              }
            });
          reactor::yield();
          s.wait();
        };
      }

      static
      std::unique_ptr<reactor::network::UTPSocket>
      socket(reactor::network::UTPServer& server,
             std::string const& host,
             uint16_t port,
             uint8_t xorit = 0)
      {
        ELLE_TRACE("open utp socket to %s:%s (xor: %s)", host, port, xorit);
        server.listen(0);
        auto s = elle::make_unique<reactor::network::UTPSocket>(server);
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

    inline
    Operations
    get_mode(boost::program_options::variables_map const& args)
    {
      if (args.count("mode") == 0)
        return Operations::all;
      std::string mode = args["mode"].as<std::string>();
      if (mode == "upload")
        return Operations::upload;
      if (mode == "download")
        return Operations::download;
      elle::err("unknown mode '%s'", mode);
    }

    inline
    bool
    tcp_enabled(infinit::model::doughnut::Protocol protocol)
    {
      return protocol == infinit::model::doughnut::Protocol::tcp ||
        protocol == infinit::model::doughnut::Protocol::all;
    }

    inline
    bool
    utp_enabled(infinit::model::doughnut::Protocol protocol)
    {
      return protocol == infinit::model::doughnut::Protocol::utp ||
        protocol == infinit::model::doughnut::Protocol::all;
    }

    inline
    uint16_t
    get_port(boost::program_options::variables_map const& args)
    {
      auto port = optional<uint16_t>(args, "port");
      if (port)
        return *port;
      return 0;
    }

    inline
    uint16_t
    get_port(boost::program_options::variables_map const& args,
             std::string const& key,
             uint16_t offset)
    {
      ELLE_DEBUG("get port for key %s", key);
      if (args.count(key))
        return args[key].as<uint16_t>();
      return get_port(args) + offset;
    }

    inline
    uint16_t
    get_tcp_port(boost::program_options::variables_map const& args)
    {
      auto port = get_port(args, "tcp_port", 0);
      ELLE_DEBUG("utp port: %s", port);
      return port;
    }

    inline
    uint16_t
    get_utp_port(boost::program_options::variables_map const& args)
    {
      auto port = get_port(args, "utp_port", 1);
      if (port == 1)
        return 0;
      ELLE_DEBUG("utp port: %s", port);
      return port;
    }

    inline
    uint16_t
    get_xored_utp_port(boost::program_options::variables_map const& args)
    {
      auto port = get_port(args, "xored_utp_port", 2);
      if (port == 2) // Random port.
      {
        auto uport = get_utp_port(args);
        if (uport == 0)
          ELLE_DEBUG("xored utp port: 0", port)
            return 0;
        port = uport + 1;
      }
      ELLE_DEBUG("xored utp port: %s", port);
      return port;
    }

    inline
    bool
    xored(boost::program_options::variables_map const& args,
          std::string const& value)
    {
      if (args.count("xored") == 0)
        return true;
      auto xored = args["xored"].as<std::string>();
      return xored == "both" || xored == value;
    }

    inline
    bool
    xored_enabled(boost::program_options::variables_map const& args)
    {
      return xored(args, "yes");
    }

    inline
    bool
    non_xored_enabled(boost::program_options::variables_map const& args)
    {
      return xored(args, "no");
    }

    inline
    elle::Buffer::Size
    packet_size_get(boost::program_options::variables_map const& args)
    {
      auto packet_size = args.count("packet_size")
        ? args["packet_size"].as<elle::Buffer::Size>()
        : 1024 * 1024;
      if (packet_size == 0)
        elle::err("--packets_count must be greater than 0");
      return packet_size;
    }

    inline
    int64_t
    packets_count_get(boost::program_options::variables_map const& args)
    {
      auto packets_count = args.count("packets_count")
        ? args["packets_count"].as<int64_t>()
        : 5;
      if (packets_count == 0)
        elle::err("--packets_count must be greater than 0");
      return packets_count;
    }

    struct Servers
    {
      Servers(boost::program_options::variables_map const& args,
              elle::Version const& v = ::version)
        : _version(v)
        , _tcp()
        , _utp()
        , _xored_utp()
      {
        auto protocol = infinit::model::doughnut::Protocol::all;
        if (args.count("protocol"))
          protocol = infinit::protocol_get(args);
        reactor::Barrier listening;
        auto base_port = get_port(args);
        uint16_t tcp_port = 0;
        uint16_t utp_port = 0;
        uint16_t xored_utp_port = 0;
        if (tcp_enabled(protocol))
        {
          tcp_port = get_tcp_port(args);
          this->_tcp.reset(
            new reactor::Thread(
              "tcp",
              [&]
              {
                tcp::serve(tcp_port, this->_version, listening,
                           flag(args, "verbose"));
              }));
          listening.wait();
          listening.close();
        }
        if (utp_enabled(protocol))
        {
          if (non_xored_enabled(args))
          {
            utp_port = get_utp_port(args);
            this->_utp.reset(
              new reactor::Thread(
                "utp",
                [&]
                {
                  utp::serve(utp_port, 0, this->_version, listening,
                             flag(args, "verbose"));
                }));
            listening.wait();
            listening.close();
          }
          if (xored_enabled(args))
          {
            xored_utp_port = get_xored_utp_port(args);
            this->_xored_utp.reset(
              new reactor::Thread(
                "utp", [&]
                {
                  utp::serve(xored_utp_port, 255, this->_version, listening,
                             flag(args, "verbose"));
                }));
            listening.wait();
            listening.close();
          }
        }

        std::cout
          << std::endl
          << "To perform tests, run the following command from another node:"
          << std::endl;
        std::cout << "> infinit-doctor --networking";
        if (protocol != infinit::model::doughnut::Protocol::all)
          std::cout << " --protocol " << protocol;
        if (base_port != 0)
          std::cout << " --port " << get_port(args);
        else
        {
          if (tcp_port)
            std::cout << " --tcp_port " << tcp_port;
          if (utp_port)
            std::cout << " --utp_port " << utp_port;
          if (xored_utp_port)
            std::cout << " --xored_utp_port " << xored_utp_port;
        }
        std::cout << " --host <address_of_this_machine>" <<std::endl;
      }

      Servers(Servers&& servers)
        : _version(servers._version)
        , _tcp(servers._tcp.release())
        , _utp(servers._utp.release())
        , _xored_utp(servers._xored_utp.release())
      {
      }

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
      ELLE_ATTRIBUTE_R(std::unique_ptr<reactor::Thread>, tcp);
      ELLE_ATTRIBUTE_R(std::unique_ptr<reactor::Thread>, utp);
      ELLE_ATTRIBUTE_R(std::unique_ptr<reactor::Thread>, xored_utp);
    };


    static
    void
    perfom(std::string const& host,
           boost::program_options::variables_map const& args,
           elle::Version const& _version = ::version)
    {
      elle::Version v = _version;
      auto protocol = infinit::model::doughnut::Protocol::all;
      if (args.count("protocol"))
        protocol = infinit::protocol_get(args);
      auto mode = get_mode(args);
      int64_t packets_count = packets_count_get(args);
      elle::Buffer::Size packet_size = packet_size_get(args);

      auto action = [&] (infinit::protocol::ChanneledStream& stream) {
        if (mode == Operations::all || mode == Operations::upload)
        {
          try
          {
            upload(stream, packet_size, packets_count, flag(args, "verbose"));
          }
          catch (reactor::network::Exception const&)
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
            download(stream, packet_size, packets_count, flag(args, "verbose"));
          }
          catch (reactor::network::Exception const&)
          {
            std::cerr << "  Something went wrong during download:"
            << elle::exception_string()
            << std::endl;
          }
        }
      };

      auto match_versions = [&] (infinit::protocol::ChanneledStream& stream) {
        infinit::RPC<elle::Version (elle::Version const&)> get_version{
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
        catch (infinit::protocol::Error const& error)
        {
          elle::err("Protocol error establishing connection with the remote.\n"
                    "Make sure it uses the same version or use "
                    "--compatibility-version");
        }
      };

      if (tcp_enabled(protocol))
      {
        auto tcp = [&]
        {
          ELLE_TRACE_SCOPE("TCP: Client");
          std::cout << "TCP:" << std::endl;
          std::unique_ptr<reactor::network::TCPSocket> socket;
          try
          {
            socket.reset(tcp::socket(host, get_tcp_port(args)).release());
          }
          catch (reactor::network::Exception const&)
          {
            std::cerr << "  Unable to establish connection: "
                      << elle::exception_string()
                      << std::endl;
            return;
          }
          infinit::protocol::Serializer serializer{
            *socket, infinit::elle_serialization_version(v), false};
          auto stream =
            infinit::protocol::ChanneledStream{serializer, serializer.version()};
          match_versions(stream);
          action(stream);
        };
        tcp();
      }
      if (utp_enabled(protocol))
      {
        auto utp = [&] (bool xored)
          {
            reactor::network::UTPServer server;
            std::unique_ptr<reactor::network::UTPSocket> socket;
            try
            {
              socket.reset(
                utp::socket(
                  server,
                  host,
                  xored ? get_xored_utp_port(args) : get_utp_port(args),
                  xored ? 0xFF : 0).release());
            }
            catch (reactor::network::Exception const&)
            {
              std::cerr << "  Unable to establish connection: "
                        << elle::exception_string()
                        << std::endl;
              return;
            }
            infinit::protocol::Serializer serializer{
              *socket, infinit::elle_serialization_version(v), false};
            auto stream =
              infinit::protocol::ChanneledStream{serializer, serializer.version()};
            match_versions(stream);
            action(stream);
          };
        if (non_xored_enabled(args))
        {
          std::cout << "UTP:" << std::endl;
          utp(false);
        }
        if (xored_enabled(args))
        {
          std::cout << "xored UTP:" << std::endl;
          utp(true);
        }
      }
    }
  }
}


#endif
