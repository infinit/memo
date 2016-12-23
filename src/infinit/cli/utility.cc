#include <infinit/cli/utility.hh>

#include <boost/regex.hpp>

#include <elle/log.hh>
#include <elle/system/unistd.hh> // chdir

#include <infinit/utility.hh>

ELLE_LOG_COMPONENT("ifnt.cli.utility");

namespace infinit
{
  namespace cli
  {
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
      reactor::scheduler().signal_handle(SIGUSR1, [&dht] {
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
      namespace bfs = boost::filesystem;
      ELLE_TRACE("Hooking discovery on %s, to %s", model, file);
      auto nls = std::make_shared<model::NodeLocations>();
      model.overlay()->on_discover().connect(
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
      model.overlay()->on_disappear().connect(
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
  }
}
