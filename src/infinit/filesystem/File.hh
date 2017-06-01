#pragma once

#include <elle/reactor/filesystem.hh>
#include <elle/reactor/Barrier.hh>
#include <elle/reactor/Thread.hh>

#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/umbrella.hh>

namespace infinit
{
  namespace filesystem
  {
    namespace bfs = boost::filesystem;
    namespace rfs = elle::reactor::filesystem;

    class Directory;
    using DirectoryPtr = std::shared_ptr<Directory>;
    using MutableBlock = infinit::model::blocks::MutableBlock;
    class FileHandle;

    class File
      : public rfs::Path
      , public Node
    {
    public:
      File(FileSystem& owner,
           Address address,
           std::shared_ptr<FileData> data,
           std::shared_ptr<DirectoryData> parent,
           std::string const& name);
      ~File() override;
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) override { THROW_NOTDIR();}
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override;
      void unlink() override;
      void mkdir(mode_t mode) override { THROW_EXIST(); }
      void rmdir() override {THROW_NOTDIR(); }
      void rename(bfs::path const& where) override;
      bfs::path readlink() override { THROW_NOENT(); }
      void symlink(bfs::path const& where) override { THROW_EXIST(); }
      void link(bfs::path const& where) override;
      void chmod(mode_t mode) override;
      void chown(int uid, int gid) override;
      void statfs(struct statvfs *) override;
      void utimens(const struct timespec tv[2]) override;
      void truncate(off_t new_size) override;
      std::string getxattr(std::string const& name) override;
      std::vector<std::string> listxattr() override;
      void setxattr(std::string const& name, std::string const& value, int flags) override;
      void removexattr(std::string const& name) override;
      std::shared_ptr<Path> child(std::string const& name) override;
      bool allow_cache() override;
      void print(std::ostream& output) const override;

      static const unsigned long default_block_size;
    private:
      friend class FileHandle;
      friend class Directory;
      friend class Unknown;
      friend class Node;

      std::unique_ptr<ACLBlock> _first_block;
      ELLE_ATTRIBUTE_R(std::shared_ptr<FileData>, filedata);

      void _ensure_first_block();
      void _fetch() override;
      void _commit(WriteTarget target) override;
      FileHeader& _header() override;
      model::blocks::ACLBlock* _header_block(bool) override;

    };

    class FileConflictResolver
      : public model::ConflictResolver
    {
    public:
      FileConflictResolver(elle::serialization::SerializerIn& s,
                           elle::Version const& v);
      FileConflictResolver();
      FileConflictResolver(bfs::path path, model::Model* model,
                           WriteTarget target);
      std::unique_ptr<Block>
      operator()(Block& b,
                 Block& current) override;
      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& version) override;
      std::string
      description() const override;
      bfs::path _path;
      model::Model* _model;
      WriteTarget _target;
      using serialization_tag = infinit::serialization_tag;
    };
  }
}
