#include <infinit/filesystem/Symlink.hh>
#include <infinit/filesystem/Unknown.hh>

#include <elle/serialization/binary.hh>
#include <elle/cast.hh>

#include <fcntl.h>

#include <sys/stat.h> // S_IMFT...

#ifdef INFINIT_WINDOWS
#undef stat
#endif

ELLE_LOG_COMPONENT("infinit.filesystem.Symlink");

namespace infinit
{
  namespace filesystem
  {
    Symlink::Symlink(
      FileSystem& owner,
       Address address,
      std::shared_ptr<DirectoryData> parent,
      std::string const& name)
      : Node(owner, address, parent, name)
    {}

    void
    Symlink::_fetch()
    {
      this->_block = std::dynamic_pointer_cast<MutableBlock>(
        this->_owner.fetch_or_die(_address));
      umbrella([&] {
          this->_h = elle::serialization::binary::deserialize<FileHeader>(
            this->_block->data());
      });
    }

    FileHeader&
    Symlink::_header()
    {
      if (!_block)
        _fetch();
      return _h;
    }

    model::blocks::ACLBlock*
    Symlink::_header_block()
    {
      return dynamic_cast<model::blocks::ACLBlock*>(_block.get());
    }

    void
    Symlink::_commit(WriteTarget)
    {
      auto data = elle::serialization::binary::serialize(_h);
      _block->data(data);
      _owner.store_or_die(std::move(_block), model::STORE_UPDATE);
    }

    void
      Symlink::stat(struct stat* st)
      {
        ELLE_TRACE_SCOPE("%s: stat", *this);
        try
        {
          this->_fetch();
          this->Node::stat(st);
        }
        catch (infinit::model::doughnut::ValidationFailed const& e)
        {
          ELLE_DEBUG("%s: permission exception dropped for stat: %s", *this, e);
        }
        catch (rfs::Error const& e)
        {
          ELLE_DEBUG("%s: filesystem exception: %s", *this, e.what());
          if (e.error_code() != EACCES)
            throw;
        }
        st->st_mode |= S_IFLNK;
        st->st_mode |= 0777; // Set rxwrwxrwx, to mimic Posix behavior.
      }

    void
      Symlink::unlink()
      {
        _parent->_files.erase(_name);
        _parent->write(*_owner.block_store(),
          {OperationType::remove, _name},
          DirectoryData::null_block,
          true);
      }

    void
      Symlink::rename(boost::filesystem::path const& where)
      {
        Node::rename(where);
      }

    boost::filesystem::path
      Symlink::readlink()
      {
        _fetch();
        return *_h.symlink_target;
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

    std::string
    Symlink::getxattr(std::string const& key)
    {
      return Node::getxattr(key);
    }
    std::vector<std::string>
    Symlink::listxattr()
    {
      _fetch();
      std::vector<std::string> res;
      for (auto const& a: _h.xattrs)
        res.push_back(a.first);
      return res;
    }
    void
    Symlink::setxattr(std::string const& name, std::string const& value, int flags)
    {
      Node::setxattr(name, value, flags);
    }
    void
    Symlink::removexattr(std::string const& name)
    {
      Node::removexattr(name);
    }
    std::unique_ptr<rfs::Handle>
    Symlink::open(int flags, mode_t mode)
    {
#ifdef INFINIT_MACOSX
  #define O_PATH O_SYMLINK
#endif
#ifndef INFINIT_WINDOWS
      if (! (flags & O_PATH))
#endif
        THROW_NOSYS;
      return {};
    }
    void Symlink::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }
  }
}
