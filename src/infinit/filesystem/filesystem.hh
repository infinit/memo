#ifndef INFINIT_FILESYSTEM_FILESYSTEM_HH
# define INFINIT_FILESYSTEM_FILESYSTEM_HH

# include <reactor/filesystem.hh>
# include <infinit/model/Model.hh>

namespace infinit
{
  namespace filesystem
  {
    class Node;
    /** Filesystem using a Block Storage as backend.
    * Directory: nodes are serialized, and contains name, stat() and block
    *            address of the directory content
    * File    : In direct mode, one block with all the data
    *           In index mode, one block containing headers
    *           and the list of addresses for the content.
    */
    class FileSystem: public reactor::filesystem::Operations
    {
    public:
      FileSystem(std::string const& volume_name, std::shared_ptr<infinit::model::Model> model);
      void
      print_cache_stats();
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
    private:
      std::unique_ptr<model::blocks::MutableBlock> _root_block();
      ELLE_ATTRIBUTE_R(std::shared_ptr<infinit::model::Model>, block_store);
      ELLE_ATTRIBUTE_RW(bool, single_mount);
      ELLE_ATTRIBUTE_R(std::string, volume_name);
    };
  }
}

#endif
