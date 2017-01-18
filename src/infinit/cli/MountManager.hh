#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <elle/serialization/json.hh>
#include <elle/system/Process.hh>

#include <infinit/Infinit.hh>
#include <infinit/MountOptions.hh>
#include <infinit/Network.hh>
#include <infinit/User.hh>
#include <infinit/cli/Infinit.hh>
#include <infinit/descriptor/BaseDescriptor.hh>
#include <infinit/utility.hh>

namespace infinit
{
  namespace cli
  {
    namespace bfs = boost::filesystem;

    struct Mount
    {
      std::unique_ptr<elle::system::Process> process;
      infinit::MountOptions options;
    };

    struct MountInfo
      : elle::Printable
    {
      MountInfo(std::string n,
                bool l,
                boost::optional<std::string> m)
        : name(std::move(n))
        , live(l)
        , mountpoint(std::move(m))
      {}
      MountInfo() = default;

      void
      serialize(elle::serialization::Serializer& s)
      {
        s.serialize("name", name);
        s.serialize("live", live);
        s.serialize("mountpoint", mountpoint);
      }

      void
      print(std::ostream& out) const override
      {
        out << name << ": "
            << (live && mountpoint ? "mounted"
                : mountpoint ? "crashed" : "not running");
        if (mountpoint)
          out << ": " << *mountpoint;
      }

      std::string name;
      bool live;
      boost::optional<std::string> mountpoint;
    };

    class MountManager
    {
    public:
      MountManager(infinit::Infinit& ifnt,
                   Infinit& cli,
                   bfs::path mount_root = bfs::temp_directory_path(),
                   std::string mount_substitute = "")
        : _mount_root(mount_root)
        , _mount_substitute(mount_substitute)
        , _fetch(false)
        , _push(false)
        , _ifnt(ifnt)
        , _cli(cli)
      {}
      ~MountManager();
      void
      start(std::string const& name, infinit::MountOptions opts = {},
            bool force_mount = false,
            bool wait_for_mount = false,
            std::vector<std::string> const& extra_args = {});
      void
      stop(std::string const& name);
      void
      status(std::string const& name, elle::serialization::SerializerOut& reply);
      bool
      exists(std::string const& name);
      std::string
      mountpoint(std::string const& name, bool ignore_subst=false);
      std::vector<infinit::descriptor::BaseDescriptor::Name>
      list();
      std::vector<MountInfo>
      status();
      void
      create_volume(std::string const& name,
                    elle::json::Object const& args);
      void
      delete_volume(std::string const& name);
      infinit::Network
      create_network(elle::json::Object const& options,
                     infinit::User const& owner);
      void
      update_network(infinit::Network& network,
                     elle::json::Object const& options);
      void
      acquire_volumes();
      ELLE_ATTRIBUTE_RW(bfs::path, mount_root);
      ELLE_ATTRIBUTE_RW(std::string, mount_substitute);
      ELLE_ATTRIBUTE_RW(boost::optional<std::string>, log_level);
      ELLE_ATTRIBUTE_RW(boost::optional<std::string>, log_path);
      ELLE_ATTRIBUTE_RW(std::string, default_user);
      ELLE_ATTRIBUTE_RW(boost::optional<std::string>, default_network);
      ELLE_ATTRIBUTE_RW(std::vector<std::string>, advertise_host);
      ELLE_ATTRIBUTE_RW(bool, fetch);
      ELLE_ATTRIBUTE_RW(bool, push);
    private:
      std::unordered_map<std::string, Mount> _mounts;
      infinit::Infinit& _ifnt;
      infinit::cli::Infinit& _cli;
    };
  }
}
