#include <infinit/filesystem/Symlink.hh>
#include <infinit/filesystem/Unknown.hh>

ELLE_LOG_COMPONENT("infinit.filesystem.Symlink");

namespace infinit
{
  namespace filesystem
  {
    Symlink::Symlink(DirectoryPtr parent,
        FileSystem& owner,
        std::string const& name)
      : Node(owner, parent, name)
    {}

    void
      Symlink::stat(struct stat* s)
      {
        ELLE_TRACE_SCOPE("%s: stat", *this);
        Node::stat(s);
      }

    void
      Symlink::unlink()
      {
        _parent->_files.erase(_name);
        _parent->_commit({OperationType::remove, _name},true);
        _remove_from_cache();
      }

    void
      Symlink::rename(boost::filesystem::path const& where)
      {
        Node::rename(where);
      }

    boost::filesystem::path
      Symlink::readlink()
      {
        return *_parent->_files.at(_name).symlink_target;
      }

    void
      Symlink::link(boost::filesystem::path const& where)
      {
        auto p = _owner.filesystem()->path(where.string());
        Unknown* unk = dynamic_cast<Unknown*>(p.get());
        if (unk == nullptr)
          THROW_EXIST;
        unk->symlink(readlink());
      }

    void
    Symlink::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Symlink(\"%s\")", this->_name);
    }

    void
    Symlink::chmod(mode_t mode)
    {
      Node::chmod(mode);
    }

    void
    Symlink::chown(int uid, int gid)
    {
      Node::chown(uid, gid);
    }
  }
}
