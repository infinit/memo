#ifndef INFINIT_FILESYSTEM_UNREACHABLE_HH
# define INFINIT_FILESYSTEM_UNREACHABLE_HH

# include <infinit/filesystem/filesystem.hh>

namespace infinit
{
  namespace filesystem
  {
    class Unreachable
      : public rfs::Path
    {
    public:
      Unreachable(FileSystem& owner, std::shared_ptr<DirectoryData> parent,
                  std::string const& name,
                  Address address,
                  EntryType type);
      void
      stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) override THROW_ACCES;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_ACCES;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override THROW_ACCES;
      void unlink() override THROW_ACCES;
      void mkdir(mode_t mode) override THROW_ACCES;
      void rmdir() override THROW_ACCES;
      void rename(boost::filesystem::path const& where) override THROW_ACCES;
      boost::filesystem::path readlink() override THROW_ACCES;
      void symlink(boost::filesystem::path const& where) override THROW_ACCES;
      void link(boost::filesystem::path const& where) override THROW_ACCES;
      void chmod(mode_t mode) override THROW_ACCES;
      void chown(int uid, int gid) override THROW_ACCES;
      void statfs(struct statvfs *) override THROW_ACCES;
      void utimens(const struct timespec tv[2]) override THROW_ACCES;
      void truncate(off_t new_size) override THROW_ACCES;
      std::shared_ptr<Path> child(std::string const& name) override THROW_ACCES;
      bool allow_cache() override { return false;}
      std::string getxattr(std::string const& k) override {THROW_ACCES;}
      EntryType _type;
    };
  }
}
# endif