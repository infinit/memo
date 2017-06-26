#pragma once

#include <elle/Printable.hh>

#include <memo/serialization.hh>
#include <memo/filesystem/filesystem.hh>
#include <memo/filesystem/FileHeader.hh>
#include <memo/model/blocks/Block.hh>

#ifdef MEMO_WINDOWS
# define S_IFLNK    0120000
# define S_IFREG    0100000
# define S_IFDIR    0040000
# undef  stat
#endif

namespace memo
{
  namespace filesystem
  {
    using Block = memo::model::blocks::Block;
    using Address = memo::model::Address;

    class Directory;

    class Node
      : public elle::Printable
    {
    public:
      using serialization_tag = memo::serialization_tag;
      void
      fetch()
      {
        this->_fetch();
      }
      void
      commit()
      {
        this->_commit();
      }
      bfs::path
      full_path();
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
      std::string
      perms_to_json(ACLBlock& block);

    public:
      void
      rename(bfs::path const& where);
      void
      utimens(const struct timespec tv[2]);
      void
      chmod(mode_t mode);
      void
      chown(int uid, int gid);
      void
      stat(struct stat* st);
      std::string
      getxattr(std::string const& key);
      void
      setxattr(std::string const& k, std::string const& v, int flags);
      void
      removexattr(std::string const& k);
      void
      set_permissions(std::string const& flags,
                      std::string const& userkey,
                      Address self_address);
      virtual
      void
      _fetch() = 0;
      virtual
      void
      _commit(WriteTarget = WriteTarget::all) = 0;
      virtual
      FileHeader&
      _header() = 0;
      virtual
      model::blocks::ACLBlock*
      _header_block(bool force = false) = 0;
      std::unique_ptr<memo::model::User>
      _get_user(std::string const& value);

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
