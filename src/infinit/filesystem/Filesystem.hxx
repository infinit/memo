#ifndef INFINIT_FILESYSTEM_FILESYSTEM_HXX
# define INFINIT_FILESYSTEM_FILESYSTEM_HXX

namespace infinit
{
  namespace filesystem
  {
    inline
    FileSystem
    _init(std::string const& volume_name,
          std::shared_ptr<model::Model> model,
          boost::optional<boost::filesystem::path> root_block_cache_dir,
          boost::optional<boost::filesystem::path> mountpoint,
          bool allow_root_creation)
    {
      return FileSystem(
        volume_name,
        std::move(model),
        std::move(root_block_cache_dir),
        std::move(mountpoint),
        allow_root_creation);
    }

    template <typename ... Args>
    FileSystem::FileSystem(Args&& ... args)
      : FileSystem(elle::named::prototype(
                     filesystem::volume_name,
                     filesystem::model,
                     filesystem::root_block_cache_dir = boost::none,
                     filesystem::mountpoint = boost::none,
                     filesystem::allow_root_creation = false)
                   .call(_init, std::forward<Args>(args)...))
    {}
  }
}

#endif
