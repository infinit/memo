#ifndef INFINIT_FILESYSTEM_UNKNOWN_HH
# define INFINIT_FILESYSTEM_UNKNOWN_HH

# include <infinit/filesystem/umbrella.hh>
# include <infinit/filesystem/Node.hh>
# include <infinit/filesystem/Directory.hh>

namespace infinit
{
  namespace filesystem
  {
    class Unknown
      : public Node
      , public rfs::Path
    {
    public:
      Unknown(DirectoryPtr parent, FileSystem& owner, std::string const& name);
      void
      stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) override THROW_NOENT;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_NOENT;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override;
      void unlink() override THROW_NOENT;
      void mkdir(mode_t mode) override;
      void rmdir() override THROW_NOENT;
      void rename(boost::filesystem::path const& where) override THROW_NOENT;
      boost::filesystem::path readlink() override THROW_NOENT;
      void symlink(boost::filesystem::path const& where) override;
      void link(boost::filesystem::path const& where) override;
      void chmod(mode_t mode) override THROW_NOENT;
      void chown(int uid, int gid) override THROW_NOENT;
      void statfs(struct statvfs *) override THROW_NOENT;
      void utimens(const struct timespec tv[2]) override THROW_NOENT;
      void truncate(off_t new_size) override THROW_NOENT;
      std::shared_ptr<Path> child(std::string const& name) override THROW_NOENT;
      bool allow_cache() override { return false;}
      std::string getxattr(std::string const& k) override {THROW_NODATA;}
      void _fetch() override {}
      void _commit() override {}
      model::blocks::ACLBlock* _header_block() override { return nullptr;}
      virtual
      void
      print(std::ostream& stream) const override;
    private:
    };
  }
}

#endif
