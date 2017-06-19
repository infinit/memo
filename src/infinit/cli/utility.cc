#include <infinit/cli/utility.hh>

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/regex.hpp>

#include <elle/log.hh>
#include <elle/network/Interface.hh>
#include <elle/system/unistd.hh> // chdir

#include <elle/reactor/FDStream.hh>
#include <elle/reactor/http/Request.hh>
#include <elle/reactor/network/rdv-socket.hh>
#include <elle/reactor/network/resolve.hh>

#include <infinit/Infinit.hh>
#include <infinit/utility.hh>

#include <infinit/cli/Error.hh>


ELLE_LOG_COMPONENT("cli.utility");

namespace infinit
{
  namespace cli
  {
    InterfacePublisher::InterfacePublisher(
      infinit::Infinit const& infinit,
      infinit::Network const& network,
      infinit::User const& self,
      infinit::model::Address const& node_id,
      int port,
      boost::optional<std::vector<std::string>> advertise,
      bool no_local_endpoints,
      bool no_public_endpoints)
      : _infinit(infinit)
      , _url(elle::sprintf("networks/%s/endpoints/%s/%x",
                           network.name, self.name, node_id))
      , _network(network)
      , _self(self)
    {
      bool v4 = !elle::os::getenv("INFINIT_NO_IPV4", false);
      bool v6 = !elle::os::getenv("INFINIT_NO_IPV6", false)
       && network.dht()->version >= elle::Version(0, 7, 0);
      Endpoints endpoints;
      if (advertise)
      {
        ELLE_TRACE("Adding hosts from advertise list");
        for (auto const& a: *advertise)
        {
          try
          {
            auto host = elle::reactor::network::resolve_tcp(a, port,
              elle::os::inenv("INFINIT_NO_IPV6"))[0];
            endpoints.addresses.push_back(host.address().to_string());
          }
          catch (elle::reactor::network::ResolutionError const& e)
          {
            ELLE_LOG("failed to resolve %s: %s", a, e);
          }
        }
      }
      ELLE_TRACE("Establishing UPNP mapping");
      if (!no_public_endpoints)
      {
        try
        {
          _upnp = elle::reactor::network::UPNP::make();
          _upnp->initialize();
          _port_map_udp = _upnp->setup_redirect(elle::reactor::network::Protocol::utp, port);
          _port_map_tcp = _upnp->setup_redirect(elle::reactor::network::Protocol::tcp, port);
          ELLE_TRACE("got mappings: %s, %s", _port_map_udp, _port_map_tcp);
          if ( (v4 && _port_map_udp.external_host.find_first_of(':') == std::string::npos)
            || (v6 && _port_map_udp.external_host.find_first_of(':') != std::string::npos))
            endpoints.addresses.push_back(_port_map_udp.external_host);
        }
        catch (std::exception const& e)
        {
          ELLE_TRACE("UPNP eror: %s", e.what());
        }
      }
      ELLE_TRACE("Obtaining public address from RDV");
      if (!no_public_endpoints)
      {
        try
        {
          auto host = elle::os::getenv("INFINIT_RDV", "rdv.infinit.sh:7890");
          if (host.empty())
            throw std::runtime_error("RDV disabled");
          int port = 7890;
          auto p = host.find_last_of(':');
          if (p != host.npos)
          {
            port = std::stoi(host.substr(p+1));
            host = host.substr(0, p);
          }
          elle::reactor::network::RDVSocket socket;
          socket.close();
          socket.bind(boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(), 0));
          elle::reactor::Thread poller("poll", [&]
          {
            while (true)
            {
              elle::Buffer buf;
              buf.size(5000);
              boost::asio::ip::udp::endpoint ep;
              socket.receive_from(elle::WeakBuffer(buf), ep);
            }
          });
          elle::SafeFinally spoll([&] {
              poller.terminate_now();
          });
          socket.rdv_connect("ip-fetcher", host, port, 5_sec);
          poller.terminate_now();
          spoll.abort();
          ELLE_TRACE("RDV gave endpoint %s", socket.public_endpoint());
          if (socket.public_endpoint().port())
          {
            auto addr = socket.public_endpoint().address();
            if ( (addr.is_v4() && v4) || (addr.is_v6() && v6))
              endpoints.addresses.push_back(addr.to_string());
          }
        }
        catch (std::exception const& e)
        {
          ELLE_TRACE("RDV error: %s", e.what());
        }
      }
      ELLE_TRACE("Obtaining local endpoints");
      if (!no_local_endpoints)
      {
        using Filter = elle::network::Interface::Filter;
        auto const filter = Filter::only_up | Filter::no_loopback | Filter::no_autoip;
        for (auto const& itf: elle::network::Interface::get_map(filter))
        {
          auto add = [&endpoints](auto const& addrs) {
            endpoints.addresses
            .insert(endpoints.addresses.end(),
                    addrs.cbegin(), addrs.cend());
          };
          if (v4)
            add(itf.second.ipv4_address);
          if (v6)
            add(itf.second.ipv6_address);
        }
      }
      endpoints.port = port;
      ELLE_TRACE("Pushing endpoints");
      this->_infinit.hub_push(this->_url, "endpoints for",
                              network.name, endpoints, self, false);
    }

    InterfacePublisher::~InterfacePublisher()
    {
      this->_infinit.hub_delete(
        this->_url, "endpoints for", this->_network.name, _self);
    }

    std::unique_ptr<std::istream>
    commands_input(boost::optional<std::string> path)
    {
      if (path && *path != "-")
      {
        auto file = std::make_unique<bfs::ifstream>(*path);
        if (!file->good())
          elle::err("unable to open \"%s\" for reading", *path);
        return std::move(file);
      }
      else
      {
#ifndef INFINIT_WINDOWS
        return std::make_unique<elle::reactor::FDStream>(0);
#else
        // Windows does not support async io on stdin
        auto res = std::make_unique<std::stringstream>();
        while (true)
        {
          char buf[4096];
          std::cin.read(buf, sizeof buf);
          if (int count = std::cin.gcount())
            res->write(buf, count);
          else
            break;
        }
        return res;
#endif
      }
    }

    /// Perform metavariable substitution.
    std::string
    VarMap::expand(std::string const& s) const
    {
      static const auto re = boost::regex("\\{\\w+\\}");
      // Not available in std::.
      return boost::regex_replace(s,
                                  re,
                                  [this] (boost::smatch const& in)
                                  {
                                    auto k = in.str();
                                    return this->vars.at(k.substr(1, k.size() - 2));
                                  });
    }


    /*---------.
    | Daemon.  |
    `---------*/

    bfs::path
    daemon_sock_path()
    {
      return infinit::xdg_runtime_dir() / "daemon.sock";
    }

#ifndef INFINIT_WINDOWS
    DaemonHandle
    daemon_hold(int nochdir, int noclose)
    {
      int pipefd[2]; // reader, writer
      if (pipe(pipefd))
        elle::err("pipe failed: %s", strerror(errno));
      int cpid = fork();
      if (cpid == -1)
        elle::err("fork failed: %s", strerror(errno));
      else if (cpid == 0)
      { // child
        if (setsid()==-1)
          elle::err("setsid failed: %s", strerror(errno));
        if (!nochdir)
          elle::chdir("/");
        if (!noclose)
        {
          int fd = open("/dev/null", O_RDWR);
          dup2(fd, 0);
          dup2(fd, 1);
          dup2(fd, 2);
        }
        close(pipefd[0]);
        return pipefd[1];
      }
      else
      { // parent
        close(pipefd[1]);
        char buf;
        int res = read(pipefd[0], &buf, 1);
        ELLE_LOG("DETACHING %s %s", res, strerror(errno));
        if (res < 1)
          exit(1);
        else
          exit(0);
      }
    }

    void
    daemon_release(DaemonHandle handle)
    {
      char buf = 1;
      if (write(handle, &buf, 1)!=1)
        perror("daemon_release");
    }
#endif

    void
    hook_stats_signals(infinit::model::doughnut::Doughnut& dht)
    {
#ifndef INFINIT_WINDOWS
      elle::reactor::scheduler().signal_handle(SIGUSR1, [&dht] {
          auto& o = dht.overlay();
          try
          {
            auto json = o->query("stats", {});
            std::cerr << elle::json::pretty_print(json);
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE("overlay stats query error: %s", e);
          }
          try
          {
            auto json = o->query("blockcount", {});
            std::cerr << elle::json::pretty_print(json);
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE("overlay blockcount query error: %s", e);
          }
        });
#endif
    }

    model::NodeLocations
    hook_peer_discovery(model::doughnut::Doughnut& model, std::string file)
    {
      ELLE_TRACE("Hooking discovery on %s, to %s", model, file);
      auto nls = std::make_shared<model::NodeLocations>();
      model.overlay()->on_discovery().connect(
        [nls, file] (model::NodeLocation nl, bool observer) {
          if (observer)
            return;
          auto it = std::find_if(nls->begin(), nls->end(),
            [id=nl.id()] (model::NodeLocation n) {
              return n.id() == id;
            });
          if (it == nls->end())
            nls->push_back(nl);
          else
            it->endpoints() = nl.endpoints();
          ELLE_DEBUG("Storing updated endpoint list: %s", *nls);
          std::ofstream ofs(file);
          elle::serialization::json::serialize(*nls, ofs, false);
        });
      model.overlay()->on_disappearance().connect(
        [nls, file] (model::Address id, bool observer) {
          if (observer)
            return;
          auto it = std::find_if(nls->begin(), nls->end(),
            [id] (model::NodeLocation n) {
              return n.id() == id;
            });
          if (it != nls->end())
            nls->erase(it);
          ELLE_DEBUG("Storing updated endpoint list: %s", *nls);
          std::ofstream ofs(file);
          elle::serialization::json::serialize(*nls, ofs, false);
        });
      if (bfs::exists(file) && !bfs::is_empty(file))
      {
        ELLE_DEBUG("Reloading endpoint list file from %s", file);
        std::ifstream ifs(file);
        return elle::serialization::json::deserialize<model::NodeLocations>(ifs, false);
      }
      return model::NodeLocations();
    }

    void
    port_to_file(uint16_t port, bfs::path const& path)
    {
      bfs::ofstream f;
      Infinit::_open_write(f, path, "", "port file", true);
      f << port << std::endl;
    }

    void
    endpoints_to_file(infinit::model::Endpoints endpoints,
                      bfs::path const& path)
    {
      bfs::ofstream f;
      Infinit::_open_write(f, path, "", "endpoint file", true);
      for (auto const& ep: endpoints)
        f << ep << std::endl;
    }

    std::vector<infinit::model::Endpoints>
    parse_peers(std::vector<std::string> const& peers)
    {
      auto res = std::vector<infinit::model::Endpoints>{};
      for (auto const& peer: peers)
      {
        auto const eps
          = bfs::exists(peer)
          ? model::endpoints_from_file(peer)
          : elle::reactor::network::resolve_udp_repr(peer);
        for (auto const& ep: eps)
          res.emplace_back(infinit::model::Endpoints{ep});
      }
      return res;
    }


    /*-----------.
    | Versions.  |
    `-----------*/
    namespace
    {
      bool
      is_version_supported(elle::Version const& version)
      {
        auto const& deps = infinit::serialization_tag::dependencies;
        return boost::algorithm::any_of(
          deps,
          [version] (auto const& kv) -> bool
          {
            return kv.first.major() == version.major() &&
              kv.first.minor() == version.minor();
          });
      }
    }

    void
    ensure_version_is_supported(elle::Version const& version)
    {
      if (!is_version_supported(version))
      {
        auto const supported_versions =
          elle::make_set(infinit::serialization_tag::dependencies,
          [] (auto const& kv)
          {
            return elle::Version{kv.first.major(), kv.first.minor(), 0};
          });
        // Find the max value for the major.
        auto const versions_for_major = make_vector_if
          (supported_versions,
           [&] (elle::Version const& c)
           {
             return c.major() == version.major();
           });
        if (!versions_for_major.empty())
        {
          if (version < versions_for_major.front())
            elle::err("Minimum compatibility version for major version %s is %s",
                      (int) version.major(), *supported_versions.begin());
          else if (version > versions_for_major.back())
            elle::err("Maximum compatibility version for major version %s is %s",
                      (int) version.major(), versions_for_major.back());
        }
        auto const vers
          = elle::make_vector(supported_versions,
                              [](auto const& v) { return elle::sprintf("%s", v); });
        elle::err("Unknown compatibility version, try one of %s",
                  boost::algorithm::join(vers, ", "));
      }
    }

    dnut::Protocol
    protocol_get(boost::optional<std::string> const& proto)
    {
      try
      {
        return elle::serialization::Serialize<dnut::Protocol>::convert
          (proto.value_or("all"));
      }
      catch (elle::serialization::Error const& e)
      {
        elle::err<CLIError>("'protocol' must be 'utp', 'tcp' or 'all': %s",
                            proto);
      }
    }

    std::string
    mode_get(boost::optional<std::string> const& mode)
    {
      static auto const modes = std::map<std::string, std::string>
        {
          {"r", "setr"},
          {"w", "setw"},
          {"rw", "setrw"},
          {"none", "clear"},
          {"", ""},
        };
      auto i = modes.find(mode ? boost::algorithm::to_lower_copy(*mode) : "");
      if (i == modes.end())
        elle::err<CLIError>("invalid mode %s, must be one of: %s",
                            mode, elle::keys(modes));
      else
        return i->second;
    }
  }
}
