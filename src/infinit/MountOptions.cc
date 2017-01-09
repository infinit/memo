#include <infinit/MountOptions.hh>

namespace infinit
{
  MountOptions::MountOptions()
  {}

  template <typename T>
  void merge(T& a, const T& b)
  {
    if (b)
      a = *b;
  }

  // template<typename T>
  // void merge(boost::optional<T>& a,
  //            T const& b,
  //            boost::program_options::option_description const& o)
  // {
  //   boost::any def;
  //   o.semantic()->apply_default(def);
  //   if (b != boost::any_cast<T>(def))
  //     a = b;
  // }

  // template<typename T>
  // void merge(boost::optional<T>& a,
  //            boost::optional<T> const& b,
  //            boost::program_options::option_description const& o)
  // {
  //   if (b)
  //     merge(a, *b, o);
  // }

  template <typename T>
  void merge(std::vector<T>& a,
             std::vector<T> const& b)
  {
    a.insert(a.end(), b.begin(), b.end());
  }

  void
  MountOptions::merge(MountOptions const& b)
  {
    infinit::merge(hub_url, b.hub_url);
    infinit::merge(rdv, b.rdv);
    infinit::merge(fuse_options, b.fuse_options);
    infinit::merge(as, b.as);
    infinit::merge(fetch, b.fetch);
    infinit::merge(push, b.push);
    infinit::merge(cache, b.cache);
    infinit::merge(async, b.async);
    infinit::merge(readonly, b.readonly);
    infinit::merge(cache_ram_size, b.cache_ram_size);
    infinit::merge(cache_ram_ttl, b.cache_ram_ttl);
    infinit::merge(cache_ram_invalidation, b.cache_ram_invalidation);
    infinit::merge(cache_disk_size, b.cache_disk_size);
    infinit::merge(mountpoint, b.mountpoint);
    infinit::merge(peers, b.peers);
    infinit::merge(poll_beyond, b.poll_beyond);
#ifndef INFINIT_WINDOWS
    infinit::merge(enable_monitoring, b.enable_monitoring);
#endif
    infinit::merge(listen_address, b.listen_address);
  }

//   void
//   MountOptions::merge(boost::program_options::variables_map const& args)
//   {
//     infinit::merge(this->fuse_options,
//                    optional<Strings>(args, "fuse-option"));
//     infinit::merge(this->peers,
//                    optional<Strings>(args, "peer"));
//     infinit::merge(this->mountpoint, optional(args, "mountpoint"));
//     // FIXME: Why user and as?
//     infinit::merge(this->as, optional(args, "as"));
//     infinit::merge(this->as, optional(args, "user"));
//     infinit::merge(this->readonly, optional<bool>(args, "readonly"));
//     if (option_fetch(args, {"fetch-endpoints"}))
//       this->fetch = true;
//     if (option_push(args, {"push-endpoints"}))
//       this->push = true;
//     infinit::merge(this->cache, optional<bool>(args, option_cache));
//     infinit::merge(this->async, optional<bool>(args, "async"));
//     infinit::merge(this->cache_ram_size,
//                    optional<int>(args, option_cache_ram_size));
//     infinit::merge(this->cache_ram_ttl,
//                    optional<int>(args, option_cache_ram_ttl));
//     infinit::merge(this->cache_ram_invalidation,
//                    optional<int>(args, option_cache_ram_invalidation));
//     infinit::merge(this->cache_disk_size,
//                    optional<uint64_t>(args, option_cache_disk_size));
//     infinit::merge(this->poll_beyond,
//                    optional<int>(args, option_poll_beyond),
//                    option_poll_beyond);
//     if (auto str = optional<std::string>(args, option_listen_interface))
//       infinit::merge(this->listen_address,
//                      boost::asio::ip::address::from_string(*str),
//                      option_listen_interface);
// #ifndef INFINIT_WINDOWS
//     infinit::merge(this->enable_monitoring,
//                    optional<bool>(args, option_monitoring),
//                    option_monitoring);
// #endif
//   }

  void
  MountOptions::to_commandline(Strings& arguments, Environ& env) const
  {
    if (rdv)
      env.emplace("INFINIT_RDV", *rdv);
    if (hub_url)
      env.emplace("INFINIT_BEYOND", *hub_url);

    // Append to `arguments`.
    auto args = [&arguments](auto&&... as)
      {
        using swallow = int[];
        (void) swallow{(arguments.emplace_back(as), 0)...};
      };

    if (fuse_options)
      for (auto const& fo: *fuse_options)
        args("--fuse-option", fo);
    if (peers)
      for (auto const& fo: *peers)
        args("--peer", fo);
    if (fetch && *fetch) args("--fetch");
    if (push && *push) args("--push");
    if (cache && *cache) args("--cache");
    if (async && *async) args("--async");
    if (readonly && *readonly) args("--readonly");
    if (cache_ram_size) args("--cache-ram-size", std::to_string(*cache_ram_size));
    if (cache_ram_ttl) args("--cache-ram-ttl", std::to_string(*cache_ram_ttl));
    if (cache_ram_invalidation) args("--cache-ram-invalidation", std::to_string(*cache_ram_invalidation));
    if (cache_disk_size) args("--cache-disk-size", std::to_string(*cache_disk_size));
    if (poll_beyond && *poll_beyond >0) args("--poll-hub", std::to_string(*poll_beyond));
#ifndef INFINIT_WINDOWS
    if (enable_monitoring && !*enable_monitoring) args("--monitoring=false");
#endif
    if (mountpoint)
      args("--mountpoint", *mountpoint);
    if (as)
      args("--as", *as);
  }
}
