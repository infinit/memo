#ifndef INFINIT_FILESYSTEM_FILE_HH
# define INFINIT_FILESYSTEM_FILE_HH

# include <reactor/filesystem.hh>
# include <reactor/Barrier.hh>
# include <reactor/thread.hh>
# include <infinit/filesystem/Node.hh>
# include <infinit/filesystem/umbrella.hh>

namespace infinit
{
  namespace filesystem
  {
    namespace rfs = reactor::filesystem;

    class Directory;
    typedef std::shared_ptr<Directory> DirectoryPtr;
    typedef infinit::model::blocks::MutableBlock MutableBlock;

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
        ~File();
        void stat(struct stat*) override;
        void list_directory(rfs::OnDirectoryEntry cb) override THROW_NOTDIR;
        std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override;
        std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override;
        void unlink() override;
        void mkdir(mode_t mode) override THROW_EXIST;
        void rmdir() override THROW_NOTDIR;
        void rename(boost::filesystem::path const& where) override;
        boost::filesystem::path readlink() override  THROW_NOENT;
        void symlink(boost::filesystem::path const& where) override THROW_EXIST;
        void link(boost::filesystem::path const& where) override;
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
        virtual
          void
          print(std::ostream& output) const override;

        static const unsigned long default_block_size = 1024 * 1024;
      private:
        friend class FileHandle;
        friend class Directory;
        friend class Unknown;
        friend class Node;

        std::unique_ptr<ACLBlock> _first_block;
        ELLE_ATTRIBUTE_R(std::shared_ptr<FileData>, filedata);

        // address -> (size, open_handle_count)
        typedef std::unordered_map<Address, std::pair<uint64_t, int>> SizeMap;
        static SizeMap _size_map;
        void _ensure_first_block();
        void _fetch() override;
        void _commit(WriteTarget target) override;
        FileHeader& _header() override;
        model::blocks::ACLBlock* _header_block() override;

    };

    class FileConflictResolver
      : public model::ConflictResolver
    {
    public:
      FileConflictResolver(elle::serialization::SerializerIn& s);
      FileConflictResolver();
      FileConflictResolver(boost::filesystem::path path, model::Model* model,
                           WriteTarget target);
      std::unique_ptr<Block>
      operator()(Block& b,
                 Block& current,
                 model::StoreMode store_mode) override;
      void
      serialize(elle::serialization::Serializer& s) override;
      boost::filesystem::path _path;
      model::Model* _model;
      WriteTarget _target;
      typedef infinit::serialization_tag serialization_tag;
    };
  }
}

#endif
