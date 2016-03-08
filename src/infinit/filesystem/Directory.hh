#ifndef INFINIT_FILESYSTEM_DIRECTORY_HH
# define INFINIT_FILESYSTEM_DIRECTORY_HH

#include <reactor/filesystem.hh>
#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/umbrella.hh>
#include <infinit/filesystem/FileData.hh>
#include <elle/unordered_map.hh>

namespace infinit
{
  namespace filesystem
  {
    namespace rfs = reactor::filesystem;
    typedef std::shared_ptr<Directory> DirectoryPtr;
    typedef infinit::model::blocks::ACLBlock ACLBlock;

    static const int DIRECTORY_MASK = 0040000;
    static const int SYMLINK_MASK = 0120000;
    static const boost::posix_time::time_duration directory_cache_time
      = boost::posix_time::seconds(2);

    class Directory
      : public rfs::Path
      , public Node
    {
    public:
      Directory(FileSystem& owner,
                std::shared_ptr<DirectoryData> data,
                std::shared_ptr<DirectoryData> parent,
                std::string const& name);
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) override;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_ISDIR;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override THROW_ISDIR;
      void unlink() override THROW_ISDIR;
      void mkdir(mode_t mode) override THROW_EXIST;
      void rmdir() override;
      void rename(boost::filesystem::path const& where) override;
      boost::filesystem::path readlink() override  THROW_ISDIR;
      void symlink(boost::filesystem::path const& where) override THROW_EXIST;
      void link(boost::filesystem::path const& where) override THROW_EXIST;
      void chmod(mode_t mode) override;
      void chown(int uid, int gid) override;
      void statfs(struct statvfs *) override;
      void utimens(const struct timespec tv[2]) override;
      void truncate(off_t new_size) override THROW_ISDIR;
      std::shared_ptr<rfs::Path> child(std::string const& name) override;

    /*--------------------.
    | Extended attributes |
    `--------------------*/
    public:
      virtual
      std::string
      getxattr(std::string const& key) override;
      virtual
      void
      setxattr(std::string const& name,
               std::string const& value,
               int flags) override;
      virtual
      std::vector<std::string>
      listxattr() override;
      virtual
      void
      removexattr(std::string const& name) override;

    public:
      void serialize(elle::serialization::Serializer&);
      bool allow_cache() override { return false;}

    /*----------.
    | Printable |
    `----------*/
    public:
      virtual
      void
      print(std::ostream& stream) const override;

    private:
      void _fetch() override;
      void _fetch(std::unique_ptr<ACLBlock> block);
      void _commit() override;
      model::blocks::ACLBlock* _header_block() override;
      FileHeader& _header() override;
      void move_recurse(boost::filesystem::path const& current,
          boost::filesystem::path const& where);
      friend class Unknown;
      friend class File;
      friend class Symlink;
      friend class Node;
      friend class FileHandle;
      friend std::unique_ptr<Block>
      resolve_directory_conflict(
        Block& b,
        Block& current,
        model::StoreMode store_mode,
        boost::filesystem::path p,
        FileSystem& owner,
        Operation op,
        std::weak_ptr<Directory> wd);
      void _commit(Operation op, bool set_mtime = false);
      void _push_changes(Operation op, bool first_write = false);
      friend class FileSystem;
      ELLE_ATTRIBUTE_R(std::shared_ptr<DirectoryData>, data);
      ELLE_ATTRIBUTE(std::unique_ptr<ACLBlock>, block);
    };

    std::unique_ptr<Block>
    resolve_directory_conflict(Block& b,
                               Block& current,
                               model::StoreMode store_mode,
                               boost::filesystem::path p,
                               FileSystem& owner,
                               Operation op,
                               std::weak_ptr<Directory> wd);
  }
}

#endif
