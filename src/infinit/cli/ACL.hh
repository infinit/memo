#pragma once

#include <elle/das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class ACL
      : public Object<ACL>
    {
    public:
      ACL(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(
                     cli::group,
                     cli::list,
                     cli::register_,
                     cli::set
                     ));

      // group
      Mode<ACL,
           void (decltype(cli::path)::Formal<std::string const&>,
                 decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::create = false),
                 decltype(cli::delete_ = false),
                 decltype(cli::show = false),
                 decltype(cli::description = boost::optional<std::string>()),
                 decltype(cli::add_user = std::vector<std::string>()),
                 decltype(cli::add_group = std::vector<std::string>()),
                 decltype(cli::add_admin = std::vector<std::string>()),
                 decltype(cli::add = std::vector<std::string>()),
                 decltype(cli::remove_user = std::vector<std::string>()),
                 decltype(cli::remove_group = std::vector<std::string>()),
                 decltype(cli::remove_admin = std::vector<std::string>()),
                 decltype(cli::remove = std::vector<std::string>()),
                 decltype(cli::verbose = false),
                 decltype(cli::fetch = false),
                 decltype(cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                          true
#else
                          false
#endif
                   )),
           decltype(modes::mode_group)>
      group;
      void
      mode_group(std::string const& path,
                 std::string const& name,
                 bool create,
                 bool delete_,
                 bool show,
                 boost::optional<std::string> description,
                 std::vector<std::string> add_user,
                 std::vector<std::string> add_group,
                 std::vector<std::string> add_admin,
                 std::vector<std::string> add,
                 std::vector<std::string> remove_user,
                 std::vector<std::string> remove_group,
                 std::vector<std::string> remove_dmin,
                 std::vector<std::string> remove,
                 bool verbose,
                 bool fetch,
                 bool fallback);

      // list
      Mode<ACL,
           void (decltype(cli::path)::Formal<std::vector<std::string> const&>,
                 decltype(cli::recursive = false),
                 decltype(cli::verbose = false),
                 decltype(cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                          true
#else
                          false
#endif
                   )),
           decltype(modes::mode_list)>
      list;
      void
      mode_list(std::vector<std::string> const& paths,
                bool recursive,
                bool verbose,
                bool fallback);

      // set
      Mode<ACL,
           void (decltype(cli::path)::Formal<std::vector<std::string> const&>,
                 decltype(cli::user)::Formal<std::vector<std::string> const&>,
                 decltype(cli::group)::Formal<std::vector<std::string> const&>,
                 decltype(cli::mode = boost::optional<std::string>()),
                 decltype(cli::others_mode = boost::optional<std::string>()),
                 // FIXME: change that to just "inherit"
                 decltype(cli::enable_inherit = false),
                 decltype(cli::disable_inherit = false),
                 decltype(cli::recursive = false),
                 decltype(cli::traverse = false),
                 decltype(cli::verbose = false),
                 decltype(cli::fetch = false),
                 decltype(cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                          true
#else
                          false
#endif
                   )),
           decltype(modes::mode_set)>
      set;
      void
      mode_set(std::vector<std::string> const& paths,
               std::vector<std::string> const& users,
               std::vector<std::string> const& groups,
               boost::optional<std::string> mode,
               boost::optional<std::string> others_mode,
               bool enable_inherit,
               bool disable_inherit,
               bool recursive,
               bool traverse,
               bool verbose,
               bool fetch,
               bool fallback);

      // register
      Mode<ACL,
           void (decltype(cli::path)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::string const&>,
                 decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::fetch = false),
                 decltype(cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                          true
#else
                          false
#endif
                   )),
           decltype(modes::mode_register)>
      register_;
      void
      mode_register(std::string const& path,
                    std::string const& user,
                    std::string const& network,
                    bool fetch,
                    bool fallback);
    };
  }
}
