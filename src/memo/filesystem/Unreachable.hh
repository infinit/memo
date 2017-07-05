#pragma once

#include <memo/filesystem/Node.hh>
#include <memo/filesystem/umbrella.hh>

namespace memo
{
  namespace filesystem
  {
    /*------.
    | Types |
    `------*/
    namespace rfs = elle::reactor::filesystem;

    class Unreachable
      : public rfs::Path
      , Node
    {
      /*-------------.
      | Construction |
      `-------------*/
    public:
      Unreachable(FileSystem& owner,
                  std::unique_ptr<model::blocks::Block> data,
                  std::shared_ptr<DirectoryData> parent,
                  std::string const& name,
                  Address address,
                  EntryType type);
      ~Unreachable() override;

    private:
      ELLE_ATTRIBUTE(std::unique_ptr<model::blocks::Block>, data);
      ELLE_ATTRIBUTE(EntryType, type);

      /*-----.
      | Path |
      `-----*/
    public:
      void
      stat(struct stat*) override;
      void
      list_directory(rfs::OnDirectoryEntry cb) override { THROW_ACCES(); }
      std::unique_ptr<rfs::Handle>
      open(int flags, mode_t mode) override { THROW_ACCES(); }
      std::unique_ptr<rfs::Handle>
      create(int flags, mode_t mode) override { THROW_ACCES(); }
      void
      unlink() override { THROW_ACCES(); }
      void
      mkdir(mode_t mode) override { THROW_ACCES(); }
      void
      rmdir() override { THROW_ACCES(); }
      void
      rename(boost::filesystem::path const& where) override { THROW_ACCES(); }
      boost::filesystem::path
      readlink() override { THROW_ACCES(); }
      void
      symlink(boost::filesystem::path const& where) override { THROW_ACCES(); }
      void
      link(boost::filesystem::path const& where) override { THROW_ACCES(); }
      void
      chmod(mode_t mode) override { THROW_ACCES(); }
      void
      chown(int uid, int gid) override { THROW_ACCES(); }
      void
      statfs(struct statvfs *) override { THROW_ACCES(); }
      void
      utimens(const struct timespec tv[2]) override { THROW_ACCES(); }
      void
      truncate(off_t new_size) override { THROW_ACCES(); }
      std::shared_ptr<Path>
      child(std::string const& name) override { THROW_ACCES(); }
      bool
      allow_cache() override;
      std::string
      getxattr(std::string const& key) override;

      /*-----.
      | Node |
      `-----*/
    public:
      void
      _commit(WriteTarget target) override { THROW_ACCES(); }
      void
      _fetch() override { THROW_ACCES(); }
      FileHeader&
      _header() override { THROW_ACCES(); }
      model::blocks::ACLBlock*
      _header_block(bool force) override { THROW_ACCES(); }

      /*----------.
      | Printable |
      `----------*/
    public:
      void
      print(std::ostream& stream) const override;
    };
  }
}

