#ifndef INFINIT_FILESYSTEM_FILESYSTEM_HH
# define INFINIT_FILESYSTEM_FILESYSTEM_HH

# include <reactor/filesystem.hh>
# include <infinit/model/Model.hh>

namespace infinit
{
  namespace filesystem
  {
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
      FileSystem(std::unique_ptr<infinit::model::Model> model);
      FileSystem(model::Address root,
                 std::unique_ptr<infinit::model::Model> model);
      void
      print_cache_stats();
      std::unique_ptr<reactor::filesystem::Path>
      path(std::string const& path) override;

    private:
      std::unique_ptr<model::blocks::Block> _root_block();
      ELLE_ATTRIBUTE(model::Address, root_address);
      ELLE_ATTRIBUTE_RW(reactor::filesystem::FileSystem*, fs);
      ELLE_ATTRIBUTE_R(std::unique_ptr<infinit::model::Model>, block_store);
    };
  }
}

#endif
