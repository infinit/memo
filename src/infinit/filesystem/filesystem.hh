#ifndef INFINIT_FILESYSTEM_FILESYSTEM_HH
# define INFINIT_FILESYSTEM_FILESYSTEM_HH

# include <chrono>

# include <boost/multi_index_container.hpp>
# include <boost/multi_index/hashed_index.hpp>
# include <boost/multi_index/identity.hpp>
# include <boost/multi_index/ordered_index.hpp>
# include <boost/multi_index/sequenced_index.hpp>

# include <reactor/filesystem.hh>
# include <infinit/model/Model.hh>
# include <infinit/filesystem/FileData.hh>

namespace infinit
{
  namespace filesystem
  {
    namespace bmi = boost::multi_index;
    typedef model::blocks::Block Block;
    typedef model::blocks::ACLBlock ACLBlock;
    class FileSystem;
    enum class EntryType
    {
      file,
      directory,
      symlink
    };
    enum class OperationType
    {
      insert,
      update,
      remove
    };
    struct Operation
    {
      OperationType type;
      std::string target;
      EntryType entry_type;
      Address address;
    };
    class DirectoryData
    {
    public:
      using clock = std::chrono::high_resolution_clock;
      static std::unique_ptr<model::blocks::ACLBlock> null_block;
      DirectoryData(boost::filesystem::path path,
                    Block& block, std::pair<bool, bool> perms);
      DirectoryData(boost::filesystem::path path,
                    model::Address address);
      void
      update(model::blocks::Block& block, std::pair<bool, bool> perms);
      void
      write(model::Model& model,
            Operation op,
            std::unique_ptr<model::blocks::ACLBlock>&block = null_block,
            bool set_mtime = false,
            bool first_write = false);
      void
      _prefetch(model::Model& model, std::shared_ptr<DirectoryData> self);
      void
      serialize(elle::serialization::Serializer&);
      typedef infinit::serialization_tag serialization_tag;
      ELLE_ATTRIBUTE_R(model::Address, address);
      ELLE_ATTRIBUTE_R(int, block_version);
      typedef elle::unordered_map<std::string, std::pair<EntryType, model::Address>> Files;
      ELLE_ATTRIBUTE_R(FileHeader, header);
      ELLE_ATTRIBUTE_R(Files, files);
      ELLE_ATTRIBUTE_R(bool, inherit_auth);
      ELLE_ATTRIBUTE_R(bool, prefetching);
      ELLE_ATTRIBUTE_R(clock::time_point, last_used);
      ELLE_ATTRIBUTE_R(boost::filesystem::path, path);
      friend class Unknown;
      friend class Directory;
      friend class File;
      friend class Node;
      friend class FileSystem;
      friend class Symlink;
      friend std::unique_ptr<Block>
      resolve_directory_conflict(Block& b,
                                 Block& current,
                                 model::StoreMode store_mode,
                                 model::Model& model,
                                 Operation op,
                                 Address address);
    };
    enum class WriteTarget
    {
      none = 0,
      perms = 1,
      links = 2,
      data = 4,
      times = 8,
      xattrs = 16,
      symlink = 32,
      all = 255,
      block = 32768,
    };
    inline
    bool
    operator &(WriteTarget const& l, WriteTarget const& r)
    {
      typedef std::underlying_type<WriteTarget>::type ut;
      return static_cast<ut>(l) & static_cast<ut>(r);
    }
    inline
    WriteTarget
    operator |(WriteTarget const& l, WriteTarget const& r)
    {
      typedef std::underlying_type<WriteTarget>::type ut;
      return static_cast<WriteTarget>(
        static_cast<ut>(l) | static_cast<ut>(r));
    }
    class FileData
    {
    public:
      using clock = std::chrono::high_resolution_clock;
      FileData(boost::filesystem::path path,
               Block& block, std::pair<bool, bool> perms);
      FileData(boost::filesystem::path path,
               model::Address address, int mode);
      void
      update(model::blocks::Block& block, std::pair<bool, bool> perms);
      void
      write(model::Model& model,
            WriteTarget target = WriteTarget::all,
            std::unique_ptr<ACLBlock>&block = DirectoryData::null_block,
            bool first_write = false);
      void
      merge(const FileData& previous, WriteTarget target);
      ELLE_ATTRIBUTE_R(model::Address, address);
      ELLE_ATTRIBUTE_R(int, block_version);
      ELLE_ATTRIBUTE_R(clock::time_point, last_used);
      ELLE_ATTRIBUTE_R(FileHeader, header);
      typedef std::pair<Address, std::string> FatEntry; // (address, key)
      ELLE_ATTRIBUTE_R(std::vector<FatEntry>, fat);
      ELLE_ATTRIBUTE_R(elle::Buffer, data);
      ELLE_ATTRIBUTE_R(boost::filesystem::path, path);
      friend class FileSystem;
      friend class File;
      friend class FileHandle;
      friend class FileConflictResolver;
    };
    class Node;
    void unchecked_remove(model::Model& model,
                          model::Address address);
    std::unique_ptr<model::blocks::Block>
    fetch_or_die(model::Model& model,
                 model::Address address,
                 boost::optional<int> local_version = {},
                 Node* node = nullptr);
    /** Filesystem using a Block Storage as backend.
    * Directory: nodes are serialized, and contains name, stat() and block
    *            address of the directory content
    * File    : In direct mode, one block with all the data
    *           In index mode, one block containing headers
    *           and the list of addresses for the content.
    */
    class FileSystem
      : public reactor::filesystem::Operations
    {
    public:
      using clock = std::chrono::high_resolution_clock;
      static clock::time_point now();
      FileSystem(std::string const& volume_name,
                 std::shared_ptr<infinit::model::Model> model,
                 boost::optional<boost::filesystem::path> state_dir = {});
      std::shared_ptr<reactor::filesystem::Path>
      path(std::string const& path) override;

      void unchecked_remove(model::Address address);
      std::unique_ptr<model::blocks::MutableBlock>
      unchecked_fetch(model::Address address);

      std::unique_ptr<model::blocks::Block>
      fetch_or_die(model::Address address,
                   boost::optional<int> local_version = {},
                   Node* node = nullptr);

      void
      store_or_die(std::unique_ptr<model::blocks::Block> block,
                   model::StoreMode mode = model::STORE_ANY,
                   std::unique_ptr<model::ConflictResolver> resolver = {});
      void
      store_or_die(model::blocks::Block& block,
                   model::StoreMode mode = model::STORE_ANY,
                   std::unique_ptr<model::ConflictResolver> resolver = {});
      // Check permissions and throws on access failure
      void
      ensure_permissions(model::blocks::Block const& block, bool r, bool w);
      std::pair<bool, bool>
      get_permissions(model::blocks::Block const& block);

      boost::signals2::signal<void()> on_root_block_create;
      std::shared_ptr<DirectoryData>
      get(boost::filesystem::path path, model::Address address);
      void filesystem(reactor::filesystem::FileSystem* fs) override;
      reactor::filesystem::FileSystem* filesystem();
    private:
      std::unique_ptr<model::blocks::MutableBlock> _root_block();
      ELLE_ATTRIBUTE_R(std::shared_ptr<infinit::model::Model>, block_store);
      ELLE_ATTRIBUTE_RW(bool, single_mount);
      ELLE_ATTRIBUTE_R(std::string, volume_name);
      ELLE_ATTRIBUTE_R(std::string, network_name);
      ELLE_ATTRIBUTE_R(bool, read_only);
      ELLE_ATTRIBUTE_R(boost::optional<boost::filesystem::path>, state_dir);
      ELLE_ATTRIBUTE_R(model::Address, root_address);

      typedef bmi::multi_index_container<
        std::shared_ptr<DirectoryData>,
        bmi::indexed_by<
          bmi::hashed_unique<
            bmi::const_mem_fun<
              DirectoryData,
              Address const&, &DirectoryData::address>>,
          bmi::ordered_non_unique<
            bmi::const_mem_fun<
              DirectoryData,
              clock::time_point const&, &DirectoryData::last_used>>
              > > DirectoryCache;
      ELLE_ATTRIBUTE_R(DirectoryCache, directory_cache);
      typedef bmi::multi_index_container<
        std::shared_ptr<FileData>,
        bmi::indexed_by<
          bmi::hashed_unique<
            bmi::const_mem_fun<
              FileData,
              Address const&, &FileData::address>>,
          bmi::ordered_non_unique<
            bmi::const_mem_fun<
              FileData,
              clock::time_point const&, &FileData::last_used>>
              > > FileCache;
      ELLE_ATTRIBUTE_R(FileCache, file_cache);
      static const int max_cache_size = 1000;
    };
  }
}

#endif
