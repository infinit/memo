#pragma once

#include <elle/reactor/filesystem.hh>

#include <memo/filesystem/Node.hh>
#include <memo/filesystem/Directory.hh>
#include <memo/filesystem/Symlink.hh>
#include <memo/filesystem/umbrella.hh>

namespace memo
{
  namespace filesystem
  {
    using DirectoryPtr = std::shared_ptr<Directory>;

    class Symlink
      : public Node
      , public rfs::Path
    {
    public:
      Symlink(FileSystem& owner,
              Address address,
              std::shared_ptr<DirectoryData> parent,
              std::string const& name);
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) override { THROW_NOTDIR(); }
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override { THROW_NOSYS(); }
      void unlink() override;
      void mkdir(mode_t mode) override { THROW_EXIST(); }
      void rmdir() override { THROW_NOTDIR(); }
      void rename(bfs::path const& where) override;
      bfs::path readlink() override;
      void symlink(bfs::path const& where) override { THROW_EXIST(); }
      void link(bfs::path const& where) override; //copied symlink
      void chmod(mode_t mode) override;
      void chown(int uid, int gid) override;
      void statfs(struct statvfs *) override { THROW_NOSYS(); }
      void utimens(const struct timespec tv[2]) override;
      void truncate(off_t new_size) override { THROW_NOSYS(); }
      std::shared_ptr<Path> child(std::string const& name) override { THROW_NOTDIR(); }
      std::string getxattr(std::string const& key) override;
      std::vector<std::string> listxattr() override;
      void setxattr(std::string const& name, std::string const& value, int flags) override;
      void removexattr(std::string const& name) override;
      bool allow_cache() override { return false;}
      void _fetch() override;
      void _commit(WriteTarget target) override;
      model::blocks::ACLBlock* _header_block(bool) override;
      FileHeader& _header() override;
      void print(std::ostream& stream) const override;
      std::unique_ptr<MutableBlock> _block;
      FileHeader _h;
    };
  }
}
