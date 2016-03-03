#ifndef INFINIT_FILESYSTEM_XATTRIBUTE_HH
# define INFINIT_FILESYSTEM_XATTRIBUTE_HH

# include <reactor/filesystem.hh>
# include <infinit/filesystem/File.hh>

namespace infinit
{
  namespace filesystem
  {
    namespace rfs = reactor::filesystem;
    class XAttributeFile
      : public rfs::Path
    {
      public:
        XAttributeFile(std::shared_ptr<rfs::Path> file, std::string const& name);
        void stat(struct stat*) override;
        void list_directory(rfs::OnDirectoryEntry cb) override THROW_NOTDIR;
        std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override;
        std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override;
        void unlink() override;
        void mkdir(mode_t mode) override THROW_EXIST;
        void rmdir() override THROW_NOTDIR;
        void rename(boost::filesystem::path const& where) override THROW_NOENT;
        boost::filesystem::path readlink() override  THROW_NOENT;
        void symlink(boost::filesystem::path const& where) override THROW_EXIST;
        void link(boost::filesystem::path const& where) override THROW_NOENT;
        void chmod(mode_t mode) override THROW_NOENT;
        void chown(int uid, int gid) override THROW_NOENT;
        void statfs(struct statvfs *) override THROW_NOENT;
        void utimens(const struct timespec tv[2]) override THROW_NOENT;
        void truncate(off_t new_size) override;
        std::string getxattr(std::string const& name) override THROW_NOENT;
        std::vector<std::string> listxattr() override { return {};};
        void setxattr(std::string const& name, std::string const& value, int flags) override THROW_NOENT;
        void removexattr(std::string const& name) override THROW_NOENT;
        std::shared_ptr<Path> child(std::string const& name) override THROW_NOENT;
        bool allow_cache() override {return false;}
      private:
        std::shared_ptr<rfs::Path> _file;
        std::string _name;
    };

    class XAttributeHandle
      : public rfs::Handle
    {
    public:
      XAttributeHandle(std::shared_ptr<rfs::Path> file, std::string const& name);
      int read(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      int
      write(elle::ConstWeakBuffer buffer, size_t size, off_t offset) override;
      void ftruncate(off_t offset) override {};
      void fsync(int datasync) override {};
      void fsyncdir(int datasync) override {};
      void close() override;
    private:
      std::shared_ptr<rfs::Path> _file;
      std::string _name;
      std::string _value;
      bool _written;
    };
    class XAttributeDirectory
      : public rfs::Path
    {
      public:
        XAttributeDirectory(std::shared_ptr<rfs::Path> file);
        void stat(struct stat*) override;
        void list_directory(rfs::OnDirectoryEntry cb) override;
        std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_ISDIR;
        std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override THROW_ISDIR;
        void unlink() override THROW_ISDIR;
        void mkdir(mode_t mode) override {};
        void rmdir() override THROW_NOENT;
        void rename(boost::filesystem::path const& where) override THROW_NOENT;
        boost::filesystem::path readlink() override  THROW_ISDIR;
        void symlink(boost::filesystem::path const& where) override THROW_EXIST;
        void link(boost::filesystem::path const& where) override THROW_EXIST;
        void chmod(mode_t mode) override THROW_NOENT;
        void chown(int uid, int gid) override THROW_NOENT;
        void statfs(struct statvfs *) override THROW_NOENT ;
        void utimens(const struct timespec tv[2]) override THROW_NOENT;
        void truncate(off_t new_size) override THROW_ISDIR;
        std::shared_ptr<rfs::Path> child(std::string const& name) override;
        std::string getxattr(std::string const& key) override THROW_NOENT;
        std::vector<std::string> listxattr() override  {return {};};
        void setxattr(std::string const& name, std::string const& value, int flags) override THROW_NOENT;
        void removexattr(std::string const& name) override THROW_NOENT;
        bool allow_cache() override { return false;}
      private:
        std::shared_ptr<rfs::Path> _file;
    };
  }
}

#endif
