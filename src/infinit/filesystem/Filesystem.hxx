#ifndef INFINIT_FILESYSTEM_FILESYSTEM_HXX
# define INFINIT_FILESYSTEM_FILESYSTEM_HXX

namespace infinit
{
  namespace filesystem
  {
    struct FileSystem::Init
    {
      std::string const& volume_name;
      std::shared_ptr<model::Model> model;
      boost::optional<cryptography::rsa::PublicKey> owner;
      boost::optional<boost::filesystem::path> root_block_cache_dir;
      boost::optional<boost::filesystem::path> mountpoint;
      bool allow_root_creation;

      static
      Init
      init(std::string const& volume_name,
           std::shared_ptr<model::Model> model,
           boost::optional<cryptography::rsa::PublicKey> owner,
           boost::optional<boost::filesystem::path> root_block_cache_dir,
           boost::optional<boost::filesystem::path> mountpoint,
           bool allow_root_creation)
      {
        return Init{
          std::move(volume_name),
          std::move(model),
          std::move(owner),
          std::move(root_block_cache_dir),
          std::move(mountpoint),
          std::move(allow_root_creation),
        };
      }
    };

    template <typename ... Args>
    FileSystem::FileSystem(Args&& ... args)
      : FileSystem(elle::named::prototype(
                     filesystem::volume_name,
                     filesystem::model,
                     filesystem::owner = boost::none,
                     filesystem::root_block_cache_dir = boost::none,
                     filesystem::mountpoint = boost::none,
                     filesystem::allow_root_creation = false)
                   .call(&Init::init, std::forward<Args>(args)...))
    {}

    inline
    FileSystem::FileSystem(Init init)
      : FileSystem(std::move(init.volume_name),
                   std::move(init.model),
                   std::move(init.owner),
                   std::move(init.root_block_cache_dir),
                   std::move(init.mountpoint),
                   std::move(init.allow_root_creation))
    {}
  }
}

#endif
