namespace infinit
{
  namespace filesystem
  {
    struct FileSystem::Init
    {
      std::string volume_name;
      std::shared_ptr<model::Model> model;
      boost::optional<elle::cryptography::rsa::PublicKey> owner;
      boost::optional<bfs::path> root_block_cache_dir;
      boost::optional<bfs::path> mountpoint;
      bool allow_root_creation;
      bool map_other_permissions;
      boost::optional<int> block_size;

      static
      Init
      init(std::string const& volume_name,
           std::shared_ptr<model::Model> model,
           boost::optional<elle::cryptography::rsa::PublicKey> owner,
           boost::optional<bfs::path> root_block_cache_dir,
           boost::optional<bfs::path> mountpoint,
           bool allow_root_creation,
           bool map_other_permissions,
           boost::optional<int> block_size)
      {
        return Init{
          std::move(volume_name),
          std::move(model),
          std::move(owner),
          std::move(root_block_cache_dir),
          std::move(mountpoint),
          std::move(allow_root_creation),
          std::move(map_other_permissions),
          std::move(block_size),
        };
      }
    };

    template <typename ... Args>
    FileSystem::FileSystem(Args&& ... args)
      : FileSystem(elle::das::named::prototype(
                     filesystem::volume_name,
                     filesystem::model,
                     filesystem::owner = boost::none,
                     filesystem::root_block_cache_dir = boost::none,
                     filesystem::mountpoint = boost::none,
                     filesystem::allow_root_creation = false,
                     filesystem::map_other_permissions = true,
                     filesystem::block_size = boost::none)
                   .call(&Init::init, std::forward<Args>(args)...))
    {}

    inline
    FileSystem::FileSystem(Init init)
      : FileSystem(std::move(init.volume_name),
                   std::move(init.model),
                   std::move(init.owner),
                   std::move(init.root_block_cache_dir),
                   std::move(init.mountpoint),
                   std::move(init.allow_root_creation),
                   std::move(init.map_other_permissions),
                   std::move(init.block_size))
    {}
  }
}
