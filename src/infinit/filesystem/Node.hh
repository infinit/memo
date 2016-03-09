#ifndef INFINIT_FILESYSTEM_NODE_HH
# define INFINIT_FILESYSTEM_NODE_HH

# include <elle/Printable.hh>

# include <infinit/serialization.hh>
# include <infinit/filesystem/filesystem.hh>
# include <infinit/filesystem/FileData.hh>
# include <infinit/model/blocks/Block.hh>

#ifdef INFINIT_WINDOWS
  #define S_IFLNK    0120000
  #define S_IFREG    0100000
  #define S_IFDIR    0040000
  #undef  stat
#endif
namespace infinit
{
  namespace filesystem
  {
    typedef infinit::model::blocks::Block Block;
    typedef infinit::model::Address Address;

    class Directory;

    class Node
      : public elle::Printable
    {
    public:
      typedef infinit::serialization_tag serialization_tag;
      void fetch() {_fetch();}
      void commit() {_commit();}
      boost::filesystem::path full_path() { return {};}
    protected:
      Node(FileSystem& owner,
           model::Address address,
           std::shared_ptr<DirectoryData> parent,
           std::string const& name)
      : _owner(owner)
      , _address(address)
      , _parent(parent)
      , _name(name)
      {}
      void rename(boost::filesystem::path const& where);
      void utimens(const struct timespec tv[2]);
      void chmod(mode_t mode);
      void chown(int uid, int gid);
      void stat(struct stat* st);
      std::string getxattr(std::string const& key);
      void setxattr(std::string const& k, std::string const& v, int flags);
      void removexattr(std::string const& k);
      void set_permissions(std::string const& flags,
        std::string const& userkey, Address self_address);
      virtual void _fetch() = 0;
      virtual void _commit(WriteTarget = WriteTarget::all) = 0;
      virtual FileHeader& _header() = 0;
      virtual model::blocks::ACLBlock* _header_block() = 0;
      std::unique_ptr<infinit::model::User> _get_user(std::string const& value);
      FileSystem& _owner;
      model::Address _address;
      std::shared_ptr<DirectoryData> _parent;
      std::string _name;
      //ELLE_ATTRIBUTE_R(FileHeader, header, protected);
      friend class FileSystem;
    };

    boost::optional<std::string>
    xattr_special(std::string const& name);
  }
}

#endif
