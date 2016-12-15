#pragma once

#include <das/cli.hh>

#include <infinit/cli/Entity.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class ACL
      : public Entity<ACL>
    {
    public:
      ACL(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(
                     cli::get_xattr,
                     cli::group,
                     cli::list,
                     cli::register_,
                     cli::set,
                     cli::set_xattr
                     ));

      // get-xattr
      Mode<decltype(binding(modes::mode_get_xattr,
                            cli::path,
                            cli::name))>
      get_xattr;
      void
      mode_get_xattr(std::string const& path,
                     std::string const& name);

      // group
      Mode<decltype(binding(modes::mode_group,
                            cli::path,
                            cli::name,
                            cli::create = false,
                            cli::delete_ = false,
                            cli::show = false,
                            cli::description = boost::none,
                            cli::add_user = std::vector<std::string>(),
                            cli::add_group = std::vector<std::string>(),
                            cli::add_admin = std::vector<std::string>(),
                            cli::add = std::vector<std::string>(),
                            cli::remove_user = std::vector<std::string>(),
                            cli::remove_group = std::vector<std::string>(),
                            cli::remove_admin = std::vector<std::string>(),
                            cli::remove = std::vector<std::string>(),
                            cli::verbose = false,
                            cli::fetch = false,
                            cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                            true
#else
                            false
#endif
                      ))>
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
      Mode<decltype(binding(modes::mode_list,
                            cli::path,
                            cli::recursive = false,
                            cli::verbose = false,
                            cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                            true
#else
                            false
#endif
                      ))>
      list;
      void
      mode_list(std::vector<std::string> const& paths,
                bool recursive,
                bool verbose,
                bool fallback);

      // set
      Mode<decltype(binding(modes::mode_set,
                            cli::path,
                            cli::user,
                            cli::group,
                            cli::mode = boost::none,
                            cli::others_mode = boost::none,
                            // FIXME: change that to just "inherit"
                            cli::enable_inherit = false,
                            cli::disable_inherit = false,
                            cli::recursive = false,
                            cli::traverse = false,
                            cli::verbose = false,
                            cli::fetch = false,
                            cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                            true
#else
                            false
#endif
                      ))>
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
      Mode<decltype(binding(modes::mode_register,
                            cli::path,
                            cli::user,
                            cli::network,
                            cli::fetch = false,
                            cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
                            true
#else
                            false
#endif
                      ))>
      register_;
      void
      mode_register(std::string const& path,
                    std::string const& user,
                    std::string const& network,
                    bool fetch,
                    bool fallback);

      // set-xattr
      Mode<decltype(binding(modes::mode_set_xattr,
                            cli::path,
                            cli::name,
                            cli::value))>
      set_xattr;
      void
      mode_set_xattr(std::string const& path,
                     std::string const& attribute,
                     std::string const& value);
    };
  }
}
