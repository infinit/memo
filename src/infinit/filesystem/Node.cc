#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/umbrella.hh>

#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Doughnut.hh>

#include <memory>

ELLE_LOG_COMPONENT("infinit.filesystem.Node");

namespace infinit
{
  namespace filesystem
  {
    void
    Node::rename(boost::filesystem::path const& where)
    {
      boost::filesystem::path current = full_path();
      std::string newname = where.filename().string();
      boost::filesystem::path newpath = where.parent_path();
      if (!_parent)
        throw rfs::Error(EINVAL, "Cannot delete root node");
      auto dir = std::dynamic_pointer_cast<Directory>(
        _owner.filesystem()->path(newpath.string()));
      if (dir->_files.find(newname) != dir->_files.end())
      {
        ELLE_TRACE_SCOPE("%s: remove existing destination", *this);
        // File and empty dir gets removed.
        auto target = _owner.filesystem()->path(where.string());
        struct stat st;
        target->stat(&st);
        if (signed(st.st_mode & S_IFMT) == DIRECTORY_MASK)
        {
          try
          {
            target->rmdir();
          }
          catch(rfs::Error const& e)
          {
            throw rfs::Error(EISDIR, "Target is a directory");
          }
        }
        else
          target->unlink();
        ELLE_DEBUG("removed move target %s", where);
      }
      auto data = _parent->_files.at(_name);
      _parent->_files.erase(_name);
      _parent->_commit({OperationType::remove, _name});
      data.name = newname;
      dir->_files.insert(std::make_pair(newname, data));
      dir->_commit({OperationType::insert, newname});
      _name = newname;
      _parent = dir;
      // Move the node in cache
      ELLE_DEBUG("Extracting %s", current);
      auto p = _owner.filesystem()->extract(current.string());
      if (p)
      {
        std::dynamic_pointer_cast<Node>(p)->_name = newname;
        // This might delete the dummy Unknown on destination which is fine
        ELLE_DEBUG("Setting %s", where);
        _owner.filesystem()->set(where.string(), p);
      }
    }

    void
    Node::_remove_from_cache(boost::filesystem::path full_path)
    {
      if (full_path == boost::filesystem::path())
        full_path = this->full_path();
      ELLE_DEBUG("remove_from_cache: %s (%s) entering", _name, full_path);
      std::shared_ptr<rfs::Path> self = _owner.filesystem()->extract(full_path.string());
      ELLE_DEBUG("remove_from_cache: %s released (%s), this %s", _name, self, this);
      new reactor::Thread("delayed_cleanup", [self] { ELLE_DEBUG("async_clean");}, true);
      ELLE_DEBUG("remove_from_cache: %s exiting with async cleaner", _name);
    }

    boost::filesystem::path
    Node::full_path()
    {
      if (_parent == nullptr)
        return "/";
      return _parent->full_path() / _name;
    }

    void
    Node::chmod(mode_t mode)
    {
      if (!_parent)
        return;
      auto & f = _parent->_files.at(_name);
      f.mode = (f.mode & ~07777) | (mode & 07777);
      f.ctime = time(nullptr);
      _parent->_commit({OperationType::update, _name});
    }

    void
    Node::chown(int uid, int gid)
    {
      if (!_parent)
        return;
      auto & f = _parent->_files.at(_name);
      f.uid = uid;
      f.gid = gid;
      f.ctime = time(nullptr);
      _parent->_commit({OperationType::update, _name});
    }

    void
    Node::removexattr(std::string const& k)
    {
      ELLE_LOG_COMPONENT("infinit.filesystem.Node.xattr");
      ELLE_TRACE_SCOPE("%s: remove attribute \"%s\"", *this, k);
      auto& xattrs = _parent ?
         _parent->_files.at(_name).xattrs
         : static_cast<Directory*>(this)->_files[""].xattrs;
      if (xattrs.erase(k))
      {
        if (_parent)
          _parent->_commit({OperationType::update, _name}, false);
        else
          static_cast<Directory*>(this)->_commit(
            {OperationType::update, ""},  false);
      }
      else
        ELLE_TRACE_SCOPE("no such attribute");
    }

    static auto const overlay_info = "user.infinit.overlay.";

    void
    Node::setxattr(std::string const& k, std::string const& v, int flags)
    {
      ELLE_LOG_COMPONENT("infinit.filesystem.Node.xattr");
      ELLE_TRACE_SCOPE("%s: set attribute \"%s\"", *this, k);
      ELLE_DUMP("value: %s", elle::ConstWeakBuffer(v));
      /* Drop quarantine flags, preventing the files from being opened.
      * https://github.com/osxfuse/osxfuse/issues/162
      */
      if (k == "com.apple.quarantine")
        return;
      if (k.substr(0, strlen(overlay_info)) == overlay_info)
      {
        std::string okey = k.substr(strlen(overlay_info));
        umbrella([&] {
          dynamic_cast<model::doughnut::Doughnut*>(_owner.block_store().get())
            ->overlay()->query(okey, v);
        }, EINVAL);
        return;
      }
      auto& xattrs = _parent ?
         _parent->_files.at(_name).xattrs
         : static_cast<Directory*>(this)->_files[""].xattrs;
      ELLE_DEBUG("got xattrs with %s entries", xattrs.size());
      xattrs[k] = elle::Buffer(v.data(), v.size());
      if (_parent)
        _parent->_commit({OperationType::update, _name}, false);
      else
        static_cast<Directory*>(this)->_commit({OperationType::update, ""},false);
    }

    std::string
    Node::getxattr(std::string const& k)
    {
      ELLE_LOG_COMPONENT("infinit.filesystem.Node.xattr");
      ELLE_TRACE_SCOPE("%s: get attribute \"%s\"", *this, k);
      if (k.substr(0, strlen(overlay_info)) == overlay_info)
      {
        std::string okey = k.substr(strlen(overlay_info));
        elle::json::Json v = umbrella([&] {
          return dynamic_cast<model::doughnut::Doughnut*>(_owner.block_store().get())
            ->overlay()->query(okey, {});
        }, EINVAL);
        if (v.empty())
          return "{}";
        else
          return elle::json::pretty_print(v);
      }
      auto& xattrs = this->_parent
        ? this->_parent->_files.at(this->_name).xattrs
        : static_cast<Directory*>(this)->_files[""].xattrs;
      auto it = xattrs.find(k);
      if (it == xattrs.end())
      {
        ELLE_DEBUG("no such attribute");
        THROW_NOATTR;
      }
      std::string value = it->second.string();
      ELLE_DUMP("value: %s", elle::ConstWeakBuffer(value));
      return value;
    }

    void
    Node::stat(struct stat* st)
    {
      memset(st, 0, sizeof(struct stat));
      if (_parent)
      {
        auto fd = _parent->_files.at(_name);
        st->st_blksize = 16384;
        st->st_mode = fd.mode;
        st->st_size = fd.size;
        st->st_atime = fd.atime;
        st->st_mtime = fd.mtime;
        st->st_ctime = fd.ctime;
        st->st_dev = 1;
        st->st_ino = (long)this;
        st->st_nlink = 1;
      }
      else
      { // Root directory permissions
        st->st_mode = DIRECTORY_MASK | 0777;
        st->st_size = 0;
      }
      st->st_uid = ::getuid();
      st->st_gid = ::getgid();
    }

    void
    Node::utimens(const struct timespec tv[2])
    {
      ELLE_TRACE_SCOPE("%s: utimens: %s", *this, tv);
      if (!_parent)
        return;
      auto & f = _parent->_files.at(_name);
      f.atime = tv[0].tv_sec;
      f.mtime = tv[1].tv_sec;
      _parent->_commit({OperationType::update, _name});
    }

    std::unique_ptr<infinit::model::User>
    Node::_get_user(std::string const& value)
    {
      if (value.empty())
        THROW_INVAL;
      ELLE_TRACE("setxattr raw key");
      elle::Buffer userkey = elle::Buffer(value.data(), value.size());
      auto user = _owner.block_store()->make_user(userkey);
      return std::move(user);
    }

    static std::pair<bool, bool> parse_flags(std::string const& flags)
    {
      bool r = false;
      bool w = false;
      if (flags == "clear")
        ;
      else if (flags == "setr")
        r = true;
      else if (flags == "setw")
        w = true;
      else if (flags == "setrw")
      {
        r = true; w = true;
      }
      else
        THROW_NODATA;
      return std::make_pair(r, w);
    }

    void
    Node::set_permissions(std::string const& flags,
                          std::string const& userkey,
                          Address self_address)
    {
      std::pair<bool, bool> perms = parse_flags(flags);
      std::unique_ptr<infinit::model::User> user =
        umbrella([&] {return _get_user(userkey);}, EINVAL);
      if (!user)
      {
        ELLE_WARN("user %s does not exist", userkey);
        THROW_INVAL;
      }
      auto block = _owner.fetch_or_die(self_address);
      std::unique_ptr<model::blocks::ACLBlock> acl
        = std::static_pointer_cast<model::blocks::ACLBlock>(std::move(block));
      if (!acl)
        throw rfs::Error(EIO, "Block is not an ACL block");
      // permission check
      auto acb = dynamic_cast<model::doughnut::ACB*>(acl.get());
      if (!acb)
        throw rfs::Error(EIO,
          elle::sprintf("Block is not an ACB block: %s", typeid(*acl).name()));
      auto dn =
        std::dynamic_pointer_cast<model::doughnut::Doughnut>(_owner.block_store());
      auto keys = dn->keys();
      if (keys.K() != acb->owner_key())
        THROW_ACCES;
      ELLE_TRACE("Setting permission at %s for %s", acl->address(), user->name());
      umbrella([&] {acl->set_permissions(*user, perms.first, perms.second);},
        EACCES);
      _owner.store_or_die(std::move(acl));
    }
  }
}
