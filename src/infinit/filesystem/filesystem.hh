#pragma once

#include <chrono>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <elle/cryptography/rsa/KeyPair.hh>

#include <elle/reactor/filesystem.hh>
#include <elle/reactor/Thread.hh>

#include <infinit/filesystem/FileHeader.hh>
#include <infinit/filesystem/fwd.hh>
#include <infinit/model/Model.hh>

namespace infinit
{
  namespace filesystem
  {
    namespace bfs = boost::filesystem;
    namespace bmi = boost::multi_index;

    using Address = model::Address;
    using Block = model::blocks::Block;
    using ACLBlock = model::blocks::ACLBlock;

    enum class EntryType
    {
      file,
      directory,
      symlink,
      pending
    };

    std::ostream&
    operator <<(std::ostream& out, EntryType entry);

    enum class OperationType
    {
      insert,
      update,
      remove,
      insert_exclusive,
    };

    std::ostream&
    operator <<(std::ostream& out, OperationType operation);

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
      DirectoryData(bfs::path path,
                    Block& block, std::pair<bool, bool> perms);
      DirectoryData(bfs::path path,
                    model::Address address);
      DirectoryData(elle::serialization::Serializer& s, elle::Version const& v);
      void
      update(model::blocks::Block& block, std::pair<bool, bool> perms);
      void
      write(FileSystem& fs,
            Operation op,
            std::unique_ptr<model::blocks::ACLBlock>&block = null_block,
            bool set_mtime = false,
            bool first_write = false);
      void
      _prefetch(FileSystem& fs, std::shared_ptr<DirectoryData> self);
      void
      serialize(elle::serialization::Serializer&, elle::Version const& v);
      using serialization_tag = infinit::serialization_tag;
      ELLE_ATTRIBUTE_R(model::Address, address);
      ELLE_ATTRIBUTE_R(int, block_version);
      using Files = elle::unordered_map<std::string, std::pair<EntryType, model::Address>>;
      ELLE_ATTRIBUTE_R(FileHeader, header);
      ELLE_ATTRIBUTE_R(Files, files);
      ELLE_ATTRIBUTE_R(bool, inherit_auth);
      ELLE_ATTRIBUTE_R(bool, prefetching);
      ELLE_ATTRIBUTE_R(clock::time_point, last_prefetch);
      ELLE_ATTRIBUTE_R(clock::time_point, last_used);
      ELLE_ATTRIBUTE_R(bfs::path, path);
      friend class Unknown;
      friend class Directory;
      friend class File;
      friend class Node;
      friend class FileSystem;
      friend class Symlink;
      friend std::unique_ptr<Block>
      resolve_directory_conflict(Block& b,
                                 Block& current,
                                 model::Model& model,
                                 Operation op,
                                 Address address,
                                 bool deserialized);
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
      using ut = std::underlying_type<WriteTarget>::type;
      return static_cast<ut>(l) & static_cast<ut>(r);
    }

    inline
    WriteTarget
    operator |(WriteTarget const& l, WriteTarget const& r)
    {
      using ut = std::underlying_type<WriteTarget>::type;
      return static_cast<WriteTarget>(
        static_cast<ut>(l) | static_cast<ut>(r));
    }

    class FileData
    {
    public:
      using clock = std::chrono::high_resolution_clock;
      FileData(bfs::path path,
               Block& block, std::pair<bool, bool> perms,
               int block_size);
      FileData(bfs::path path,
               model::Address address, int mode,
               int block_size);
      void
      update(model::blocks::Block& block,
             std::pair<bool, bool> perms,
             int block_size);
      void
      write(FileSystem& fs,
            WriteTarget target = WriteTarget::all,
            std::unique_ptr<ACLBlock>&block = DirectoryData::null_block,
            bool first_write = false);
      void
      merge(const FileData& previous, WriteTarget target);
      ELLE_ATTRIBUTE_R(model::Address, address);
      ELLE_ATTRIBUTE_R(int, block_version);
      ELLE_ATTRIBUTE_R(clock::time_point, last_used);
      ELLE_ATTRIBUTE_R(FileHeader, header);
      using FatEntry = std::pair<Address, std::string>; // (address, key)
      ELLE_ATTRIBUTE_R(std::vector<FatEntry>, fat);
      ELLE_ATTRIBUTE_R(elle::Buffer, data);
      ELLE_ATTRIBUTE_R(bfs::path, path);
      using serialization_tag = infinit::serialization_tag;
      friend class FileSystem;
      friend class File;
      friend class FileHandle;
      friend class FileBuffer;
      friend class FileConflictResolver;
    };

    class Node;
    void
    unchecked_remove(model::Model& model,
                     model::Address address);
    void
    unchecked_remove_chb(model::Model& model,
                         model::Address chb,
                         model::Address owner);
    std::unique_ptr<model::blocks::Block>
    fetch_or_die(model::Model& model,
                 model::Address address,
                 boost::optional<int> local_version = {},
                 bfs::path const& path = {});

    std::pair<bool, bool>
    get_permissions(model::Model& model,
                    model::blocks::Block const& block);
    ELLE_DAS_SYMBOL(allow_root_creation);
    ELLE_DAS_SYMBOL(block_size);
    ELLE_DAS_SYMBOL(model);
    ELLE_DAS_SYMBOL(map_other_permissions);
    ELLE_DAS_SYMBOL(mountpoint);
    ELLE_DAS_SYMBOL(owner);
    ELLE_DAS_SYMBOL(root_block_cache_dir);
    ELLE_DAS_SYMBOL(volume_name);

    /** Filesystem using a Block Storage as backend.
     * Directory: nodes are serialized, and contains name, stat() and block
     *            address of the directory content
     * File    : In direct mode, one block with all the data
     *           In index mode, one block containing headers
     *           and the list of addresses for the content.
     */
    class FileSystem
      : public elle::reactor::filesystem::Operations
    {
    /*------.
    | Types |
    `------*/
    public:
      using clock = std::chrono::high_resolution_clock;

    /*-------------.
    | Construction |
    `-------------*/
    public:
      template <typename ... Args>
      FileSystem(Args&& ... args);
      FileSystem(
        std::string volume_name,
        std::shared_ptr<infinit::model::Model> model,
        boost::optional<elle::cryptography::rsa::PublicKey> owner = {},
        boost::optional<bfs::path> root_block_cache_dir = {},
        boost::optional<bfs::path> mountpoint = {},
        bool allow_root_creation = false,
        bool map_other_permissions = true,
        boost::optional<int> block_size = {});
      ~FileSystem() override;
    private:
      struct Init;
      FileSystem(Init);

    public:
      static
      clock::time_point
      now();
      void
      print_cache_stats();
      std::shared_ptr<elle::reactor::filesystem::Path>
      path(std::string const& path) override;

      void
      unchecked_remove(model::Address address);
      std::unique_ptr<model::blocks::MutableBlock>
      unchecked_fetch(model::Address address);

      std::unique_ptr<model::blocks::Block>
      fetch_or_die(model::Address address,
                   boost::optional<int> local_version = {},
                   bfs::path const& path = {});

      void
      store_or_die(std::unique_ptr<model::blocks::Block> block,
                   bool insert,
                   std::unique_ptr<model::ConflictResolver> resolver = {});
      void
      store_or_die(model::blocks::Block& block,
                   bool insert,
                   std::unique_ptr<model::ConflictResolver> resolver = {});
      // Check permissions and throws on access failure
      void
      ensure_permissions(model::blocks::Block const& block, bool r, bool w);

      boost::signals2::signal<void()> on_root_block_create;
      std::shared_ptr<DirectoryData>
      get(bfs::path path, model::Address address);
      void filesystem(elle::reactor::filesystem::FileSystem* fs) override;
      elle::reactor::filesystem::FileSystem* filesystem();

    private:
      Address
      root_address();

    public:
      elle::cryptography::rsa::PublicKey const&
      owner() const;
      ELLE_ATTRIBUTE_R(std::shared_ptr<infinit::model::Model>, block_store);
      ELLE_ATTRIBUTE_RW(bool, single_mount);
      ELLE_ATTRIBUTE(boost::optional<elle::cryptography::rsa::PublicKey>, owner);
      ELLE_ATTRIBUTE_R(std::string, volume_name);
      ELLE_ATTRIBUTE_R(std::string, network_name);
      ELLE_ATTRIBUTE_R(bool, read_only);
      ELLE_ATTRIBUTE_R(boost::optional<bfs::path>, root_block_cache_dir);
      ELLE_ATTRIBUTE_R(boost::optional<bfs::path>, mountpoint);
      ELLE_ATTRIBUTE_R(model::Address, root_address);
      ELLE_ATTRIBUTE_R(bool, allow_root_creation);
      ELLE_ATTRIBUTE_R(bool, map_other_permissions);

      using DirectoryCache
      = bmi::multi_index_container<
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
              >>;
      ELLE_ATTRIBUTE_R(DirectoryCache, directory_cache);

      using FileCache
      = bmi::multi_index_container<
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
              >>;
      ELLE_ATTRIBUTE_R(FileCache, file_cache);
      ELLE_ATTRIBUTE_RX(std::vector<elle::reactor::Thread::unique_ptr>, running);
      ELLE_ATTRIBUTE_RX(int, prefetching);
      ELLE_ATTRIBUTE_RW(boost::optional<int>, block_size);
      using FileBuffers = std::unordered_map<Address, std::weak_ptr<FileBuffer>>;
      ELLE_ATTRIBUTE_RX(FileBuffers, file_buffers);
      static const int max_cache_size = 10000;
      friend class FileData;
      friend class DirectoryData;
    };
  }
}

#include <infinit/filesystem/filesystem.hxx>
