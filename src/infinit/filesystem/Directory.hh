#pragma once

#include <elle/reactor/filesystem.hh>
#include <elle/unordered_map.hh>

#include <infinit/filesystem/FileHeader.hh>
#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/umbrella.hh>

namespace infinit
{
  namespace filesystem
  {
    namespace bfs = boost::filesystem;
    namespace rfs = elle::reactor::filesystem;

    using DirectoryPtr = std::shared_ptr<Directory>;
    using ACLBlock = infinit::model::blocks::ACLBlock;

    constexpr int DIRECTORY_MASK = 0040000;
    constexpr int SYMLINK_MASK = 0120000;
    static auto const directory_cache_time = boost::posix_time::seconds(2);

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
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override { THROW_ISDIR(); }
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override { THROW_ISDIR(); }
      void unlink() override { THROW_ISDIR(); }
      void mkdir(mode_t mode) override { THROW_EXIST(); }
      void rmdir() override;
      void rename(bfs::path const& where) override;
      bfs::path readlink() override  { THROW_ISDIR(); }
      void symlink(bfs::path const& where) override { THROW_EXIST(); }
      void link(bfs::path const& where) override { THROW_EXIST(); }
      void chmod(mode_t mode) override;
      void chown(int uid, int gid) override;
      void statfs(struct statvfs *) override;
      void utimens(const struct timespec tv[2]) override;
      void truncate(off_t new_size) override { THROW_ISDIR(); }
      std::shared_ptr<rfs::Path> child(std::string const& name) override;

    /*--------------------.
    | Extended attributes |
    `--------------------*/
    public:
      std::string
      getxattr(std::string const& key) override;
      void
      setxattr(std::string const& name,
               std::string const& value,
               int flags) override;
      std::vector<std::string>
      listxattr() override;
      void
      removexattr(std::string const& name) override;

    public:
      void serialize(elle::serialization::Serializer&);
      bool allow_cache() override { return false;}

    /*----------.
    | Printable |
    `----------*/
    public:
      void
      print(std::ostream& stream) const override;

    private:
      void _fetch() override;
      void _fetch(std::unique_ptr<ACLBlock> block);
      void _commit(WriteTarget target) override;
      model::blocks::ACLBlock* _header_block(bool) override;
      FileHeader& _header() override;
      void move_recurse(bfs::path const& current,
          bfs::path const& where);
      friend class Unknown;
      friend class File;
      friend class Symlink;
      friend class Node;
      friend class FileHandle;

      void _commit(Operation op, bool set_mtime = false);
      void _push_changes(Operation op, bool first_write = false);
      friend class FileSystem;
      ELLE_ATTRIBUTE_R(std::shared_ptr<DirectoryData>, data);
      ELLE_ATTRIBUTE(std::unique_ptr<ACLBlock>, block);
    };

    class DirectoryConflictResolver
      : public model::ConflictResolver
    {
    public:
      DirectoryConflictResolver(elle::serialization::SerializerIn& s,
                                elle::Version const& v);
      DirectoryConflictResolver(DirectoryConflictResolver&& b);
      DirectoryConflictResolver();
      DirectoryConflictResolver(model::Model& model,
                                Operation op,
                                Address address);
      ~DirectoryConflictResolver() override;
      std::unique_ptr<Block>
      operator() (Block& block,
                  Block& current) override;
      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& v) override;
      std::string
      description() const override;
      model::SquashOperation
      squashable(SquashStack const& others) override;

      model::Model* _model;
      Operation _op;
      Address _address;
      bool _deserialized;
      using serialization_tag = infinit::serialization_tag;
    };

    std::unique_ptr<Block>
    resolve_directory_conflict(Block& b,
                               Block& current,
                               bfs::path p,
                               FileSystem& owner,
                               Operation op,
                               std::weak_ptr<Directory> wd,
                               bool deserialized);
  }
}
