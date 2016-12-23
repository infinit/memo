#include <infinit/MountOptions.hh>

namespace infinit
{
  MountOptions::MountOptions()
  {}

  template <typename T>
  void merge(T& a, const T& b)
  {
    if (b)
      a = b.get();
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
  //     merge(a, b.get(), o);
  // }

  template <typename T>
  void merge(boost::optional<std::vector<T>>& a,
             boost::optional<std::vector<T>> const& b)
  {
    if (b)
    {
      if (!a)
        a = std::vector<T>();
      a.get().insert(a.get().end(), b.get().begin(), b.get().end());
    }
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
  }

//   void
//   MountOptions::merge(boost::program_options::variables_map const& args)
//   {
//     infinit::merge(this->fuse_options,
//                    optional<std::vector<std::string>>(args, "fuse-option"));
//     infinit::merge(this->peers,
//                    optional<std::vector<std::string>>(args, "peer"));
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
  MountOptions::to_commandline(
    std::vector<std::string>& arguments,
    std::unordered_map<std::string, std::string>& env) const
  {
    if (rdv)
      env.insert(std::make_pair("INFINIT_RDV", rdv.get()));
    if (hub_url)
      env.insert(std::make_pair("INFINIT_BEYOND", hub_url.get()));
    if (fuse_options)
      for (auto const& fo: fuse_options.get())
      {
        arguments.push_back("--fuse-option");
        arguments.push_back(fo);
      }
    if (peers)
      for (auto const& fo: peers.get())
      {
        arguments.push_back("--peer");
        arguments.push_back(fo);
      }
    if (fetch && *fetch) arguments.push_back("--fetch");
    if (push && *push) arguments.push_back("--push");
    if (cache && *cache) arguments.push_back("--cache");
    if (async && *async) arguments.push_back("--async");
    if (readonly && *readonly) arguments.push_back("--readonly");
    if (cache_ram_size) {arguments.push_back("--cache-ram-size"); arguments.push_back(std::to_string(cache_ram_size.get()));}
    if (cache_ram_ttl) {arguments.push_back("--cache-ram-ttl"); arguments.push_back(std::to_string(cache_ram_ttl.get()));}
    if (cache_ram_invalidation) {arguments.push_back("--cache-ram-invalidation"); arguments.push_back(std::to_string(cache_ram_invalidation.get()));}
    if (cache_disk_size) {arguments.push_back("--cache-disk-size"); arguments.push_back(std::to_string(cache_disk_size.get()));}
    if (poll_beyond && *poll_beyond >0) {arguments.push_back("--poll-hub"); arguments.push_back(std::to_string(poll_beyond.get()));}
#ifndef INFINIT_WINDOWS
    if (enable_monitoring && !*enable_monitoring) {arguments.push_back("--monitoring=false");}
#endif
    if (mountpoint)
    {
      arguments.push_back("--mountpoint");
      arguments.push_back(mountpoint.get());
    }
    if (as)
    {
      arguments.push_back("--as");
      arguments.push_back(as.get());
    }
  }
}
