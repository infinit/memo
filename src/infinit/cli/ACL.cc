#include <infinit/cli/ACL.hh>

#include <sys/stat.h>

#include <elle/print.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>
#include <infinit/cli/xattrs.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>

ELLE_LOG_COMPONENT("infinit-acl");

namespace infinit
{
  namespace cli
  {
    namespace bfs = boost::filesystem;

    /*--------.
    | Helpers |
    `--------*/

    namespace
    {
      class PermissionDenied
        : public elle::Error
      {
      public:
        PermissionDenied(std::string const& error)
          : elle::Error(error)
        {}
      };

      class InvalidArgument
        : public elle::Error
      {
      public:
        InvalidArgument(std::string const& error)
          : elle::Error(error)
        {}
      };

      template<typename A, typename ... Args>
      void
      recursive_action(A action, std::string const& path, Args const&... args)
      {
        boost::system::error_code erc;
        auto it = bfs::recursive_directory_iterator(path, erc);
        if (erc)
          elle::err("%s : %s", path, erc.message());
        for (; it != bfs::recursive_directory_iterator(); it.increment(erc))
        {
          // Ensure that we have permission on the file.
          bfs::exists(it->path(), erc);
          if (erc == boost::system::errc::permission_denied)
          {
            std::cout << "permission denied, skipping " << it->path().string()
                      << std::endl;
            continue;
          }
          action(it->path().string(), args...);
          elle::reactor::yield();
        }
      }

      template<typename A, typename Res, typename ... Args>
      void
      recursive_action(std::vector<Res>& output,
                       A action,
                       std::string const& path, Args ... args)
      {
        recursive_action(
          [&] (std::string const& path) {
            output.push_back(action(path, args...));
          }, path);
      }

      struct PermissionsResult
      {
        // Factor this class.
        struct Permissions
        {
          Permissions() = default;
          Permissions(Permissions const&) = default;

          Permissions(elle::serialization::Serializer& s)
          {
            this->serialize(s);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("read", this->read);
            s.serialize("write", this->write);
            s.serialize("owner", this->owner);
            s.serialize("admin", this->admin);
            s.serialize("name", this->name);
          }

          bool read;
          bool write;
          bool owner;
          bool admin;
          std::string name;
        };

        struct Directory
        {
          Directory()
            : inherit(boost::none)
          {}

          Directory(Directory const&) = default;

          Directory(elle::serialization::Serializer& s)
          {
            this->serialize(s);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("inherit", this->inherit);
          }

          boost::optional<bool> inherit;
        };

        struct World
        {
          World() = default;
          World(World const&) = default;

          World(elle::serialization::Serializer& s)
          {
            this->serialize(s);
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("read", this->read);
            s.serialize("write", this->write);
          }

          bool read;
          bool write;
        };

        PermissionsResult() = default;
        PermissionsResult(PermissionsResult const&) = default;

        PermissionsResult(elle::serialization::Serializer& s)
        {
          this->serialize(s);
        }

        void
        serialize(elle::serialization::Serializer& s)
        {
          s.serialize("path", this->path);
          s.serialize("error", this->error);
          s.serialize("permissions", this->permissions);
          s.serialize("directory", this->directory);
          s.serialize("world", this->world);
        }

        std::string path;
        boost::optional<std::string> error;
        std::vector<Permissions> permissions;
        boost::optional<Directory> directory;
        boost::optional<World> world;
      };

      PermissionsResult
      get_acl(std::string const& path, bool fallback_xattrs)
      {
        PermissionsResult res;
        res.path = path;
        char buf[4096];
        int sz = get_xattr(
          path.c_str(), "user.infinit.auth", buf, 4095, fallback_xattrs);
        if (sz < 0)
        {
          auto err = errno;
          res.error = std::string{std::strerror(err)};
        }
        else
        {
          buf[sz] = 0;
          std::stringstream ss;
          ss.str(buf);
#ifndef __clang__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
          auto dir_inherit = boost::optional<bool>{};
#ifndef __clang__
# pragma GCC diagnostic pop
#endif
          bool dir = bfs::is_directory(path);
          if (dir)
          {
            int sz = get_xattr(
              path.c_str(), "user.infinit.auth.inherit",
              buf, 4095, fallback_xattrs);
            if (sz < 0)
              perror(path.c_str());
            else
            {
              buf[sz] = 0;
              if (buf == std::string("true"))
                dir_inherit = true;
              else if (buf == std::string("false"))
                dir_inherit = false;
            }
          }
          try
          {
            elle::json::Json j = elle::json::read(ss);
            auto a = boost::any_cast<elle::json::Array>(j);
            for (auto const& _entry: a)
            {
              auto const& entry = boost::any_cast<elle::json::Object>(_entry);
              PermissionsResult::Permissions perms;
              perms.read = boost::any_cast<bool>(entry.at("read"));
              perms.write = boost::any_cast<bool>(entry.at("write"));
              perms.admin = boost::any_cast<bool>(entry.at("admin"));
              perms.owner = boost::any_cast<bool>(entry.at("owner"));
              perms.name = boost::any_cast<std::string>(entry.at("name"));
              res.permissions.push_back(perms);
            }
            if (dir)
            {
              PermissionsResult::Directory dir;
              if (dir_inherit)
                dir.inherit = *dir_inherit;
              res.directory = dir;
            }
            {
              struct stat st;
              int stat_result = ::stat(path.c_str(),&st);
              if (stat_result != 0)
                perror(path.c_str());
              else
              {
                if (st.st_mode & 06)
                {
                  PermissionsResult::World world;
                  world.read = true;
                  world.write = st.st_mode & 02;
                  res.world = world;
                }
              }
            }
            return res;
          }
          catch (elle::reactor::Terminate const&)
          {
            throw;
          }
          catch (...)
          {
            res.error = elle::exception_string();
          }
        }
        return res;
      }

      void
      list_action(std::string const& path, bool verbose, bool fallback_xattrs)
      {
        if (verbose)
          std::cout << "processing " << path << std::endl;
        auto res = get_acl(path, fallback_xattrs);
        if (res.error)
          std::cerr << path << ": " << *res.error
                    << std::endl;
        else
        {
          std::stringstream output;
          output << path << ":" << std::endl;
          if (res.directory)
          {
            auto dir = *res.directory;
            output << "  inherit: "
                   << (dir.inherit ? dir.inherit.get() ? "true" : "false" : "unknown")
                   << std::endl;
          }
          if (res.world)
          {
            output << "  world: "
                   << ((*res.world).write ? "rw" : "r")
                   << std::endl;
          }
          for (auto& perm: res.permissions)
          {
            const char* mode = perm.write
              ? (perm.read ? "rw" : "w")
              : (perm.read ? "r" : "none");
            output << "    " << perm.name;
            if (perm.admin || perm.owner)
            {
              output << " (";
              if (perm.admin)
                output << "admin";
              if (perm.admin && perm.owner)
                output << ", ";
              if (perm.owner)
                output << "owner";
              output << ")";
            }
            output << ": " << mode << std::endl;
          }
          std::cout << output.str();
        }
      }

      constexpr char admin_prefix = '^';
      constexpr char group_prefix = '@';

      bool
      is_admin(std::string const& obj)
      {
        return !obj.empty() && obj[0] == admin_prefix;
      }

      bool
      is_group(std::string const& obj)
      {
        return !obj.empty() && obj[0] == group_prefix;
      }

      std::vector<std::string>
      collate_users(boost::optional<std::vector<std::string>>&& combined,
                    boost::optional<std::vector<std::string>>&& users,
                    boost::optional<std::vector<std::string>>&& admins,
                    boost::optional<std::vector<std::string>>&& groups)
      {
        auto res = std::vector<std::string>{};
        if (combined)
          std::move(combined->begin(), combined->end(), std::back_inserter(res));
        if (users)
          std::move(users->begin(), users->end(), std::back_inserter(res));
        if (admins)
          for (auto a: admins.get())
            if (a[0] == admin_prefix)
              res.emplace_back(std::move(a));
            else
              res.emplace_back(elle::sprintf("%s%s", admin_prefix, a));
        if (groups)
          for (auto g: groups.get())
            if (g[0] == group_prefix)
              res.emplace_back(std::move(g));
            else
              res.emplace_back(elle::sprintf("%s%s", group_prefix, g));
        return res;
      }

      std::string
      public_key_from_username(infinit::Infinit const& ifnt,
                               std::string const& username, bool fetch)
      {
        auto user = ifnt.user_get(username, fetch);
        elle::Buffer buf;
        {
          elle::IOStream ios(buf.ostreambuf());
          elle::serialization::json::SerializerOut so(ios, false);
          so.serialize_forward(user.public_key);
        }
        return buf.string();
      }

      void
      group_add_remove(infinit::Infinit& infinit,
                       bfs::path const& path,
                       std::string const& group,
                       std::string const& object,
                       std::string const& action,
                       bool fallback,
                       bool fetch)
      {
        if (!object.length())
          elle::err<CLIError>("empty user or group name");
        static const std::string base = "user.infinit.group.";
        std::string action_detail = is_admin(object) ? "admin" : "";
        std::string attr = elle::sprintf("%s%s%s", base, action, action_detail);
        auto set_attr = [&] (std::string const& identifier)
          {
            set_xattr(path.string(), attr, group + ":" + identifier, fallback);
          };
        std::string name = is_admin(object) ? object.substr(1) : object;
        try
        {
          set_attr(name);
        }
        catch (elle::Error const& e)
        {
          if (is_group(name))
          {
            elle::err("ensure group \"%s\" exists and path is in a volume",
                      name.substr(1));
          }
          else
          {
            try
            {
              set_attr(public_key_from_username(infinit, name, fetch));
            }
            catch (elle::Error const& e)
            {
              elle::err("ensure user \"%s\" exists and path is in a volume", name);
            }
          }
        }
      }
    }

    /*----.
    | ACL |
    `----*/

    ACL::ACL(Infinit& infinit)
      : Object(infinit)
      , group(*this,
              "Edit groups",
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
        )
      , list(*this,
             "List ACLs",
             cli::path,
             cli::recursive = false,
             cli::verbose = false,
             cli::fallback_xattrs =
#ifdef INFINIT_WINDOWS
             true
#else
             false
#endif
        )
      , set(*this,
            "Set ACLs",
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
        )
      , register_(*this,
                  "Register user's passport to the network",
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
        )
    {}

    /*------------.
    | Mode: group |
    `------------*/

    void
    ACL::mode_group(std::string const& path_str,
                    std::string const& group,
                    bool create,
                    bool delete_,
                    bool show,
                    boost::optional<std::string> description,
                    std::vector<std::string> add_user,
                    std::vector<std::string> add_group,
                    std::vector<std::string> add_admin,
                    std::vector<std::string> add_,
                    std::vector<std::string> rem_user,
                    std::vector<std::string> rem_group,
                    std::vector<std::string> rem_admin,
                    std::vector<std::string> rem_,
                    bool verbose,
                    bool fetch,
                    bool fallback)
    {
      bfs::path path(path_str);
      auto add = collate_users(add_, add_user, add_admin, add_group);
      auto rem = collate_users(rem_, rem_user, rem_admin, rem_group);
      {
        int action_count = (create + delete_ + show + !!description
                            + !add.empty() + !rem.empty());
        if (action_count == 0)
          elle::err<CLIError>("no action specified");
        else if (action_count > 1)
          elle::err<CLIError>("specify only one action at a time");
      }
      enforce_in_mountpoint(path.string(), fallback);
      // Need to perform group actions on a directory in the volume.
      if (!is_directory(path))
        path = path.parent_path();
      if (create)
        set_xattr(path.string(), "user.infinit.group.create", group, fallback);
      else if (delete_)
        set_xattr(path.string(), "user.infinit.group.delete", group, fallback);
      else if (description)
      {
        set_xattr(path.string(),
                 elle::sprintf("infinit.groups.%s.description", group),
                 description.get(), fallback);
      }
      else
      {
        for (auto const& obj: add)
          group_add_remove(this->cli().infinit(),
                           path, group, obj, "add", fallback, fetch);
        for (auto const& obj: rem)
          group_add_remove(this->cli().infinit(),
                           path, group, obj, "remove", fallback, fetch);
      }
      if (show)
      {
        char res[16384];
        int sz = get_xattr(path.string(), "user.infinit.group.list." + group,
                          res, sizeof res, fallback);
        if (sz >= 0)
        {
          res[sz] = 0;
          std::cout << res << std::endl;
        }
        else
          elle::err("unable to list group: %s", group);
      }
    }

    /*-----------.
    | Mode: list |
    `-----------*/

    void
    ACL::mode_list(std::vector<std::string> const& paths,
                   bool recursive,
                   bool verbose,
                   bool fallback)
    {
      if (paths.empty())
        throw elle::das::cli::MissingOption("path");
      for (auto const& path: paths)
      {
        enforce_in_mountpoint(path, fallback);
        if (this->cli().script())
        {
          auto permissions = std::vector<PermissionsResult>();
          permissions.push_back(get_acl(path, fallback));
          if (recursive)
            recursive_action(permissions, get_acl, path, fallback);
          elle::serialization::json::serialize(permissions, std::cout, false);
        }
        else
        {
          list_action(path, verbose, fallback);
          if (recursive)
            recursive_action(list_action, path, verbose, fallback);
        }
      }
    }

    /*---------------.
    | Mode: register |
    `---------------*/

    void
    ACL::mode_register(std::string const& path,
                       std::string const& user_name,
                       std::string const& network_name,
                       bool fetch,
                       bool fallback)
    {
      auto& ifnt = this->cli().infinit();
      auto self = this->cli().as_user();
      auto network = ifnt.network_get(network_name, self);
      enforce_in_mountpoint(path, fallback);
      auto user = ifnt.user_get(user_name, fetch);
      auto passport = ifnt.passport_get(network.name, user_name);
      std::stringstream output;
      elle::serialization::json::serialize(passport, output, false);
      set_xattr(path, "user.infinit.register." + user_name, output.str(),
               fallback);
    }

    /*----------.
    | Mode: set |
    `----------*/

    namespace
    {
      /// \param mode   one of ["setr", "setw", "setrw", "clear", ""].
      /// \param omode  likewise.
      void
      set_action(std::string const& path,
                 infinit::Infinit const& infinit,
                 std::vector<std::string> const& users,
                 std::string const& mode,
                 std::string const& omode,
                 bool inherit,
                 bool disinherit,
                 bool verbose,
                 bool fallback_xattrs,
                 bool fetch,
                 bool multi = false)
      {
        if (verbose)
          elle::print(std::cout, "processing {}\n", path);
        bool dir = bfs::is_directory(path);
        if (inherit || disinherit)
        {
          if (dir)
          {
            try
            {
              std::string value = inherit ? "true" : "false";
              set_xattr(path, "user.infinit.auth.inherit", value, fallback_xattrs);
            }
            catch (PermissionDenied const&)
            {
              if (multi)
                std::cout << "permission denied, skipping " << path << std::endl;
              else
                std::cout << "permission denied " << path << std::endl;
            }
            catch (elle::Error const& error)
            {
              ELLE_ERR("setattr (inherit) on %s failed: %s", path,
                       elle::exception_string());
            }
          }
        }
        if (!omode.empty())
        {
          try
          {
            set_xattr(path, "user.infinit.auth_others",
                     omode, fallback_xattrs);
          }
          catch (PermissionDenied const&)
          {
            if (multi)
              std::cout << "permission denied, skipping " << path << std::endl;
            else
              std::cout << "permission denied " << path << std::endl;
          }
          catch (InvalidArgument const&)
          {
            ELLE_ERR("setattr (omode: %s) on %s failed: %s", omode, path,
                     elle::exception_string());
            throw;
          }
        }
        if (!mode.empty())
        {
          for (auto& username: users)
          {
            auto set_attribute =
              [path, mode, fallback_xattrs, multi] (std::string const& value)
              {
                try
                {
                  set_xattr(path, "user.infinit.auth." + mode,
                           value, fallback_xattrs);
                }
                catch (PermissionDenied const&)
                {
                  if (multi)
                    elle::print(std::cout,
                                "permission denied, skipping {}\n", path);
                  else
                    elle::print(std::cout,
                                "permission denied on {}\n", path);
                }
              };
            try
            {
              set_attribute(username);
            }
            // XXX: Invalid argument could be something else... Find a way to
            // distinguish the different errors.
            catch (InvalidArgument const&)
            {
              try
              {
                set_attribute(public_key_from_username(
                                infinit, username, fetch));
              }
              catch (InvalidArgument const&)
              {
                ELLE_ERR("setattr (mode: %s) on %s failed: %s", mode, path,
                         elle::exception_string());
                throw;
              }
            }
          }
        }
      }
    }

    void
    ACL::mode_set(std::vector<std::string> const& paths,
                  std::vector<std::string> const& users,
                  std::vector<std::string> const& groups,
                  boost::optional<std::string> mode_name,
                  boost::optional<std::string> others_mode_name,
                  bool inherit,
                  bool disinherit,
                  bool recursive,
                  bool traverse,
                  bool verbose,
                  bool fetch,
                  bool fallback)
    {
      // Validate arguments.
      auto mode = mode_get(mode_name);
      auto omode = mode_get(others_mode_name);
      auto combined = collate_users(users, boost::none, boost::none, groups);
      {
        if (paths.empty())
          throw elle::das::cli::MissingOption("path");
        // auto users = combined ? combined.get() : std::vector<std::string>();
        if (mode_name && combined.empty())
          elle::err<CLIError>("must specify user when setting mode");
        if (!mode_name && !combined.empty())
          throw elle::das::cli::MissingOption("mode");
        if (inherit && disinherit)
          elle::err<CLIError>("inherit and disable-inherit are exclusive");
        if (!inherit && !disinherit && !mode_name && !others_mode_name)
          elle::err<CLIError>("no operation specified");
        if (traverse && mode_name && mode_name->find("r") == std::string::npos)
          elle::err<CLIError>(
            "--traverse can only be used with mode 'r' or 'rw'");
      }
      // Don't do any operations before checking paths.
      for (auto const& path: paths)
      {
        enforce_in_mountpoint(path, fallback);
        if ((inherit || disinherit)
            && !recursive
            && !bfs::is_directory(path))
        {
          elle::err("%s is not a directory, cannot %s inheritance",
                    path, inherit ? "enable" : "disable");
        }
      }
      bool multi = paths.size() > 1 || recursive;
      for (auto const& path: paths)
      {
        set_action(path, this->cli().infinit(), combined, mode, omode,
                   inherit, disinherit, verbose, fallback, fetch, multi);
        if (traverse)
        {
          bfs::path working_path = bfs::absolute(path);
          while (!path_is_root(working_path.string(), fallback))
          {
            working_path = working_path.parent_path();
            set_action(working_path.string(), this->cli().infinit(), combined,
                       "setr", "", false, false,
                       verbose, fallback, fetch, multi);
          }
        }
        if (recursive)
        {
          recursive_action(
            set_action, path, this->cli().infinit(), combined, mode, omode,
            inherit, disinherit, verbose, fallback, fetch, multi);
        }
      }
    }

  }
}
