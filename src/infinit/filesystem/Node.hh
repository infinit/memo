#ifndef INFINIT_FILESYSTEM_NODE_HH
# define INFINIT_FILESYSTEM_NODE_HH

# include <elle/Printable.hh>

# include <infinit/serialization.hh>
# include <infinit/filesystem/filesystem.hh>
# include <infinit/filesystem/FileData.hh>
# include <infinit/model/blocks/Block.hh>

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
      boost::filesystem::path full_path();
      void fetch() {_fetch();}
      void commit() {_commit();}
    protected:
      Node(FileSystem& owner, std::shared_ptr<Directory> parent, std::string const& name)
      : _owner(owner)
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
      void _remove_from_cache(boost::filesystem::path p = boost::filesystem::path());
      virtual void _fetch() = 0;
      virtual void _commit() = 0;
      virtual model::blocks::ACLBlock* _header_block() = 0;
      std::unique_ptr<infinit::model::User> _get_user(std::string const& value);
      FileSystem& _owner;
      std::shared_ptr<Directory> _parent;
      std::string _name;
      ELLE_ATTRIBUTE_R(FileHeader, header, protected);
      friend class FileSystem;
    };
  }
}

#endif
