#include <unordered_map>
#include <utility>
#include <map>

#include <elle/log.hh>
#include <elle/string/algorithm.hh>
#include <elle/log/TextLogger.hh>
#include <elle/string/algorithm.hh>
#include <elle/system/Process.hh>
#include <elle/os/environ.hh>

#include <infinit/storage/Dropbox.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/GCS.hh>
#include <infinit/storage/GoogleDrive.hh>
#include <infinit/storage/Strip.hh>
#ifndef INFINIT_WINDOWS
# include <infinit/storage/sftp.hh>
#endif
#include <infinit/storage/S3.hh>

#include <reactor/connectivity/connectivity.hh>
#include <reactor/network/upnp.hh>
#include <reactor/scheduler.hh>

ELLE_LOG_COMPONENT("infinit-doctor");

#include <main.hh>

infinit::Infinit ifnt;
typedef std::unordered_map<std::string, std::string> Environment;
namespace reporting
{
  static
  std::string
  result(bool value)
  {
    return value ? "OK" : "Bad";
  }

  static
  void
  section(std::ostream& out,
          std::string name)
  {
    out << " = " << name << " = " << std::endl;
    out << std::endl;
  }

  template <typename C, typename ... Args>
  void
  store(C& container,
        std::string const& key,
        Args&&... args)
  {
    container.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(key),
      std::forward_as_tuple(std::forward<Args>(args)...));
  }

  template <typename C>
  bool
  sane(C const& c, bool all = true)
  {
    auto filter = [&] (typename C::value_type const& x)
      {
        return x.second.sane();
      };
    if (all)
      return std::all_of(c.begin(), c.end(), filter);
    else
      return std::any_of(c.begin(), c.end(), filter);
  }

  template <typename C>
  bool
  warning(C const& c)
  {
    return std::any_of(c.begin(), c.end(),
                       [&] (typename C::value_type const& x)
                       {
                         return x.second.warning();
                       });
  }

  static
  std::ostream&
  faulty(std::ostream& out,
         std::string const& name)
  {
    return out << "[33;01;31m[" << name << "][0m";
  }

  static
  std::ostream&
  warn(std::ostream& out,
       std::string const& name)
  {
    return out << "[33;01;33m[" << name << "][0m";
  }

  template <typename C>
  void
  print(std::ostream& out,
        std::string const& name,
        C const& container,
        bool verbose)
  {
    bool broken = std::find_if(
      container.begin(), container.end(),
      [&] (typename C::value_type const& r)
      {
        return !r.second.sane();
      }) != container.end();
    if (verbose || broken)
      out << name << ":" << std::endl;
    for (auto const& item: container)
      if (verbose || !item.second.sane())
      {
        out << "  ";
        if (item.second.sane())
          out << item.first;
        else
          faulty(out, item.first);
        item.second.print(out << " ", verbose);
        // out << std::endl;
      }
  }

  struct Result
  {
    typedef boost::optional<std::string> Reason;

    Result()
      : _sane(false)
      , reason()
      , _warning(false)
    {}

    Result(bool sane,
           Reason const& reason = Reason{},
           boost::optional<bool> warning = boost::none)
      : _sane(sane)
      , reason(reason)
      , _warning(warning)
    {}

    Result(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    virtual
    ~Result()
    {
    }

    ELLE_ATTRIBUTE_RW(bool, sane);
    Reason reason;
    ELLE_ATTRIBUTE_RW(bool, warning);


    virtual
    void
    _print(std::ostream& out, bool verbose) const{};

  protected:
    bool
    show(bool verbose) const
    {
      return !this->sane() || verbose || this->warning();
    }

  public:
    std::ostream&
    print(std::ostream& out, bool verbose) const
    {
      if (this->show(verbose))
        this->_print(out, verbose);
      if (this->show(verbose) && this->reason)
        out << " (" << *this->reason << ")";
      if (this->show(verbose))
        out << std::endl;
      return out;
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("sane", this->_sane);
      s.serialize("reason", this->reason);
      s.serialize("warning", this->_warning);
    }
  };

  struct IntegrityResults
    : public Result
  {
    using Result::Result;

    struct Result:
      public reporting::Result
    {
      using reporting::Result::Result;

      std::ostream&
      print(std::ostream& out, bool verbose) const
      {
        if (!this->sane())
          out << "is faulty because ";
        this->_print(out, verbose);
        if (!this->sane() && this->reason)
          out << " (" << *this->reason << ")";
        return out << std::endl;
      }
    };

    // Use inheritance maybe ?
    struct StorageResoucesResult
      : public Result
    {
      StorageResoucesResult(
        bool sane,
        std::string const& type,
        reporting::Result::Reason const& reason = reporting::Result::Reason{})
        : Result(sane, reason)
        , type(type)
      {
      }

      StorageResoucesResult(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      StorageResoucesResult() = default;

      void
      _print(std::ostream& out, bool) const override
      {
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("type", this->type);
      }

      std::string type;
    };

    struct NetworkResult
      : public Result
    {
      typedef
      boost::optional<std::vector<std::string>>
      FaultyStorageResources;
      NetworkResult(
        bool sane,
        FaultyStorageResources storage_resources = FaultyStorageResources{},
        Result::Reason extra_reason = Result::Reason{})
        : Result(sane, extra_reason)
        , storage_resources(storage_resources)
      {
      }

      NetworkResult(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      NetworkResult() = default;

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("storage_resources", this->storage_resources);
      }

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->storage_resources)
        {
          if (this->storage_resources->size() > 0)
            faulty(out, elle::join(storage_resources->begin(), storage_resources->end(), "], ["));
          if (this->storage_resources->size() == 1)
            out << " storage resource is faulty";
          else if (this->storage_resources->size() > 1)
            out << " storage resources are faulty";
        }
      }

      FaultyStorageResources storage_resources;
    };

    struct VolumeResult
      : public Result
    {
      typedef boost::optional<std::string> FaultyNetwork;

      VolumeResult(bool sane,
                   FaultyNetwork faulty_network = FaultyNetwork{},
                   Result::Reason extra_reason = Result::Reason{})
        : Result(sane, extra_reason)
        , faulty_network(faulty_network)
      {
      }

      VolumeResult(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      VolumeResult() = default;

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("network", this->faulty_network);
      }

      void
      _print(std::ostream& out, bool) const override
      {
        if (this->faulty_network)
        {
          faulty(out << "network ", *this->faulty_network) << " is faulty";
        }
      }

      FaultyNetwork faulty_network;
    };

    struct DriveResult
      : public Result
    {
      typedef boost::optional<std::string> FaultyVolume
      ;
      DriveResult(bool sane,
                  FaultyVolume faulty_volume = FaultyVolume{},
                  Result::Reason extra_reason = Result::Reason{})
        : Result(sane, extra_reason)
        , faulty_volume(faulty_volume)
      {
      }

      DriveResult(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      DriveResult() = default;

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("volume", this->faulty_volume);
      }

      void
      _print(std::ostream& out, bool) const override
      {
        if (this->faulty_volume)
        {
          faulty(out << "volume ", *this->faulty_volume) << " is faulty";
        }
      }

      FaultyVolume faulty_volume;
    };

    IntegrityResults()
    {
    }

    IntegrityResults(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    bool
    sane() const
    {
      return reporting::sane(this->storage_resources)
        && reporting::sane(this->networks)
        && reporting::sane(this->volumes)
        && reporting::sane(this->drives);
    }

    bool
    warning() const
    {
      return reporting::warning(this->storage_resources)
        || reporting::warning(this->networks)
        || reporting::warning(this->volumes)
        || reporting::warning(this->drives);
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("storage resources", this->storage_resources);
      s.serialize("networks", this->networks);
      s.serialize("volumes", this->volumes);
      s.serialize("drives", this->drives);
      if (s.out())
      {
        bool sane = this->sane();
        s.serialize("sane", sane);
      }
    }

    void
    print(std::ostream& out, bool verbose) const
    {
      if (!this->sane() || verbose)
        section(out, "Integrity");
      reporting::print(out, "Storage resources", storage_resources, verbose);
      reporting::print(out, "Networks", networks, verbose);
      reporting::print(out, "Volumes", volumes, verbose);
      reporting::print(out, "Drives", drives, verbose);
    }

    std::unordered_map<std::string, StorageResoucesResult> storage_resources;
    std::unordered_map<std::string, NetworkResult> networks;
    std::unordered_map<std::string, VolumeResult> volumes;
    std::unordered_map<std::string, DriveResult> drives;
  };

  struct SanityResults
    : public reporting::Result
  {
    using Result::Result;

    struct UserResult
      : public reporting::Result
    {
      UserResult() = default;

      UserResult(std::tuple<bool, Result::Reason> const& validity,
                 std::string const &name)
        : Result(std::get<0>(validity), std::get<1>(validity))
        , name(name)
      {
      }

      UserResult(std::string const& name)
        : UserResult(valid(name), name)
      {
      }

      std::tuple<bool, Result::Reason>
      valid(std::string const& name) const
      {
        try
        {
          infinit::check_name(name);
          return std::make_tuple(true, Result::Reason{});
        }
        catch (...)
        {
          return std::make_tuple(false, Result::Reason{elle::exception_string()});
        }
      }

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->show(verbose))
        {
          out << "User name:";
          if (this->sane())
            out << " " << this->name;
          else
            faulty(out << std::endl << "  ", this->name) << " is invalid";
        }
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("name", this->name);
      }

      std::string name;
    };

    struct SpaceLeft
      : public reporting::Result
    {
      SpaceLeft(size_t minimum,
                double minimum_ratio,
                size_t available,
                size_t capacity,
                Result::Reason const& reason = Result::Reason {})
        : Result(capacity != 0,
                 reason,
                 ((available / (double) capacity) < minimum_ratio) && available < minimum)
        , minimum(minimum)
        , minimum_ratio(minimum_ratio)
        , available(available)
        , capacity(capacity)
      {
      }

      SpaceLeft(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      SpaceLeft() = default;

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->show(verbose))
        {
          out << "Disk space left:";
          if (!this->sane() || this->warning())
            (this->warning() ? warn : faulty)(out << std::endl << "  ", "low");
          elle::fprintf(out, " %s available (%s%%)",
                        this->available,
                        100 * this->available / (double) this->capacity);
        }
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("minimum", this->minimum);
        s.serialize("minimum_ratio", this->minimum_ratio);
        s.serialize("available", this->available);
        s.serialize("capacity", this->capacity);
      }

      uint64_t minimum;
      double minimum_ratio;
      uint64_t available;
      uint64_t capacity;
    };

    struct EnvironmentResult
      : public reporting::Result
    {
      EnvironmentResult(Environment const& environment)
        : Result(true, reporting::Result::Reason{}, environment.size() != 0)
        , environment(environment)
      {
      }

      EnvironmentResult(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      EnvironmentResult() = default;

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->show(verbose))
        {
          out << "Environment:" << std::endl;
          for (auto const& entry: this->environment)
            warn(out << "  ", entry.first) << ": " << entry.second << std::endl;
        }
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("entries", this->environment);
      }

      Environment environment;
    };

    struct PermissionResult
      : public reporting::Result
    {
      PermissionResult(bool exists, bool read, bool write)
        : Result(exists && read && write)
        , exists(exists)
        , read(read)
        , write(write)
      {
      }

      PermissionResult() = default;

      PermissionResult(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->show(verbose))
        {
          out
            << "exists: " << result(this->exists)
            << ", readable: " << result(this->read)
            << ", writable: " << result(this->write);
        }
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("exists", this->exists);
        s.serialize("read", this->read);
        s.serialize("write", this->write);
      }

      bool exists;
      bool read;
      bool write;
    };
    typedef std::unordered_map<std::string, PermissionResult> PermissionResults;


    SanityResults() = default;

    SanityResults(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    void
    print(std::ostream& out, bool verbose) const
    {
      if (!this->sane() || this->warning() || verbose)
        section(out, "Sanity");
      user.print(out, verbose);
      space_left.print(out, verbose);
      environment.print(out, verbose);
      reporting::print(out, "Permissions", permissions, verbose);
    }

    bool
    sane() const
    {
      return this->user.sane()
        && this->space_left.sane()
        && this->environment.sane()
        && reporting::sane(this->permissions);
    }

    bool
    warning() const
    {
      return this->user.warning()
        || this->space_left.warning()
        || this->environment.warning()
        || reporting::warning(this->permissions);
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("user name", this->user);
      s.serialize("space left", this->space_left);
      s.serialize("environment", this->environment);
      s.serialize("permissions", this->permissions);
      if (s.out())
      {
        bool sane = this->sane();
        s.serialize("sane", sane);
      }
    }

    UserResult user;
    SpaceLeft space_left;
    EnvironmentResult environment;
    PermissionResults permissions;
  };

  struct NetworkingResults
    : public reporting::Result
  {
    struct BeyondResult
      : public reporting::Result
    {
      using Result::Result;

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->show(verbose))
        {
          out << "Connection to " << ::beyond() << ":";
          if (!this->sane())
            faulty(out << std::endl << "  ", result(this->sane()));
          else
            out << result(this->sane());
        }
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
      }
    };

    struct InterfaceResults
      : public reporting::Result
    {
      typedef std::vector<std::string> IPs;
      InterfaceResults(IPs const& ips)
        : Result(ips.size() > 0)
        , entries(ips)
      {
      }

      InterfaceResults(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      InterfaceResults() = default;

      using Result::Result;

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->show(verbose))
        {
          out << "Interfaces:" << std::endl;
          for (auto const& entry: this->entries)
          {
            out << "  " << entry << std::endl;
          }
        }
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("entries", this->entries);
      }

      IPs entries;
    };

    struct ProtocolResult
      : public reporting::Result
    {
      using Result::Result;

      ProtocolResult(std::string const& address,
                     uint16_t local_port,
                     uint16_t remote_port,
                     bool internal)
        : Result(true)
        , address(address)
        , local_port(local_port)
        , remote_port(remote_port)
        , internal(internal)
      {
      }

      ProtocolResult(std::string const& error)
        : Result(false, error)
      {
      }

      ProtocolResult() = default;

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("address", this->address);
        s.serialize("local_port", this->local_port);
        s.serialize("remote_port", this->remote_port);
        s.serialize("internal", this->internal);
      }

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->show(verbose))
        {
          if (this->address)
            out << std::endl << "    Address: " << *this->address;
          if (this->local_port)
            out << std::endl << "    Local port: " << *this->local_port;
          if (this->remote_port)
            out << std::endl << "    Remote port: " << *this->remote_port;
          if (this->internal)
            out << std::endl << "    Internal: " << *this->internal;
        }
      }

      boost::optional<std::string> address;
      boost::optional<uint16_t> local_port;
      boost::optional<uint16_t> remote_port;
      boost::optional<bool> internal;
    };
    typedef std::unordered_map<std::string, ProtocolResult> ProtocolResults;

    struct NatResult
      : public reporting::Result
    {
      NatResult(bool cone)
        : Result(true)
        , cone(cone)
      {
      }

      NatResult(std::string const& error)
        : Result(false, error)
      {
      }

      NatResult() = default;

      NatResult(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->show(verbose))
        {
          out << "NAT: ";
          if (this->sane())
            out << "OK (" << (this->cone ? "CONE" : "NOT CONE") << ")";
          else
            faulty(out << std::endl << "  ", elle::sprintf("%s", result(false)));
        }
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("cone", cone);
      }

      bool cone;
    };

    struct UPNPResult
      : public reporting::Result
    {
      struct RedirectionResult
        : public reporting::Result
      {
        // This probably exist somewhere...
        struct Address
          : elle::Printable
        {
          Address(std::string host,
                  uint16_t port)
            : host(host)
            , port(port)
          {
          }

          Address(elle::serialization::SerializerIn& s)
          {
            this->serialize(s);
          }

          void
          print(std::ostream& out) const override
          {
            out << this->host << ":" << this->port;
          }

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("host", this->host);
            s.serialize("port", this->port);
          }

          std::string host;
          uint16_t port;
        };

        RedirectionResult(bool sane = false,
                          Result::Reason const& reason = Result::Reason {})
          : Result(sane, reason)
        {
        }

        RedirectionResult(elle::serialization::SerializerIn& s)
        {
          this->serialize(s);
        }

        void
        serialize(elle::serialization::Serializer& s)
        {
          Result::serialize(s);
          s.serialize("internal", this->internal);
          s.serialize("external", this->external);
        }

        void
        _print(std::ostream& out, bool verbose) const override
        {
          if (this->show(verbose))
          {
            out << result(this->sane());
            if (internal)
              out << std::endl << "    internal: " << this->internal;
            if (internal)
              out << std::endl << "    external: " << this->external;
            if (!this->sane() && this->reason)
              out << std::endl << "   >" << *this->reason;
          }
        }

        boost::optional<Address> internal;
        boost::optional<Address> external;
      };

      UPNPResult(bool available = false)
        : Result(available)
        , available(available)
      {
      }

      UPNPResult(elle::serialization::SerializerIn& s)
      {
        this->serialize(s);
      }

      void
      _print(std::ostream& out, bool verbose) const override
      {
        if (this->show(verbose))
        {
          out << "UPNP:" << std::endl;
          out << "  available: " << this->available << std::endl;
          if (this->external)
            out << "  external IP address: " << this->external;
          else
            out << "  no external IP address";
          out << std::endl;
          for (auto const& redirection: redirections)
          {
            out << "  ";
            if (redirection.second.sane())
              out << redirection.first;
            else
              warn(out, redirection.first);
            redirection.second.print(out << ": ", verbose);
            out << std::endl;
          }
        }
      }

      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("available", this->available);
        s.serialize("external", this->external);
        s.serialize("redirections", this->redirections);
      }

      bool available;
      boost::optional<std::string> external;
      std::unordered_map<std::string, RedirectionResult> redirections;
    };

    NetworkingResults()
      : Result(false)
    {
    }

    NetworkingResults(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    void
    print(std::ostream& out, bool verbose) const
    {
      if (!this->sane() | verbose)
        section(out, "Networking");
      this->beyond.print(out, verbose);
      this->interfaces.print(out, verbose);
      this->nat.print(out, verbose);
      this->upnp.print(out, verbose);
      reporting::print(out, "Protocols", this->protocols, verbose);
    }

    bool
    sane() const
    {
      return this->beyond.sane()
        && this->interfaces.sane()
        && this->nat.sane()
        && this->upnp.sane()
        && reporting::sane(this->protocols, false);
    }

    bool
    warning() const
    {
      return this->beyond.warning()
        || this->interfaces.warning()
        || this->nat.warning()
        || this->upnp.warning()
        || reporting::warning(this->protocols);
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("beyond", this->beyond);
      s.serialize("interfaces", this->interfaces);
      s.serialize("protocols", this->protocols);
      s.serialize("nat", this->nat);
      s.serialize("upnp", this->upnp);
      if (s.out())
      {
        bool sane = this->sane();
        s.serialize("sane", sane);
      }
    }

    BeyondResult beyond;
    InterfaceResults interfaces;
    ProtocolResults protocols;
    NatResult nat;
    UPNPResult upnp;
  };

  struct All
  {
    All()
    {
    }

    All(elle::serialization::SerializerIn& s)
      : integrity(s.deserialize<IntegrityResults>("integrity"))
      , sanity(s.deserialize<SanityResults>("sanity"))
      , networking(s.deserialize<NetworkingResults>("networking"))
    {
    }

    IntegrityResults integrity;
    SanityResults sanity;
    NetworkingResults networking;

    void
    print(std::ostream& out, bool verbose) const
    {
      integrity.print(out, verbose);
      sanity.print(out, verbose);
      networking.print(out, verbose);
    }

    bool
    sane() const
    {
      return this->integrity.sane()
        && this->sanity.sane()
        && this->networking.sane();
    }

    bool
    warning() const
    {
      return this->integrity.warning()
        || this->sanity.warning()
        || this->networking.warning();
    }

    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("integrity", this->integrity);
      s.serialize("sanity", this->sanity);
      s.serialize("networking", this->networking);
      if (s.out())
      {
        bool sane = this->sane();
        s.serialize("sane", sane);
      }
    }
  };
};

// Return the infinit related environment.
static
Environment
infinit_related_environment()
{
  auto environ = elle::os::environ();
  // Remove non INFINIT_ or ELLE_ prefixed entries.
  for (auto it = environ.begin(); it != environ.end();)
  {
    if (it->first.find("INFINIT_") != 0 &&
        it->first.find("ELLE_") != 0)
      environ.erase(it++);
    else
      ++it;
  }
  return environ;
}

static
void
_networking(boost::program_options::variables_map const& args,
            reporting::NetworkingResults& results)
{
  // Contact beyond.
  {
    try
    {
      reactor::http::Request r(beyond(), reactor::http::Method::GET);
      reactor::wait(r);
      auto status = (r.status() == reactor::http::StatusCode::OK);
      if (status)
        results.beyond = {status};
      else
        results.beyond = {status, elle::sprintf("%s", r.status())};
    }
    catch (elle::Error const&)
    {
      results.beyond = {false, elle::exception_string()};
    }
  }
  // Interfaces.
  auto interfaces = elle::network::Interface::get_map(
    elle::network::Interface::Filter::no_loopback);
  std::vector<std::string> public_ips;
  for (auto i: interfaces)
  {
    if (i.second.ipv4_address.empty())
      continue;
    public_ips.push_back(i.second.ipv4_address);
  }
  results.interfaces = {public_ips};
  // XXX: This should be nat.infinit.sh or something.
  std::string host = "192.241.139.66";
  uint16_t port = 5456;
  auto run = [&] (std::string const& name,
                  std::function<reactor::connectivity::Result (
                    std::string const& host,
                    uint16_t port)> const& function,
                  int deltaport = 0)
    {
      std::string result = elle::sprintf("  %s: ", name);
      try
      {
        auto address = function(host, port + deltaport);
        bool external = std::find(
          public_ips.begin(), public_ips.end(), address.host
          ) == public_ips.end();
        reporting::store(results.protocols, name, host, address.local_port,
                         address.remote_port, !external);
      }
      catch (reactor::Terminate const&)
      {
        throw;
      }
      catch (...)
      {
        reporting::store(results.protocols, name, elle::exception_string());
      }
    };
  run("TCP", reactor::connectivity::tcp);
  run("UDP", reactor::connectivity::udp);
  run("UTP",
      std::bind(reactor::connectivity::utp,
                std::placeholders::_1,
                std::placeholders::_2,
                0),
      1);
  run("UTP (XOR)",
      std::bind(reactor::connectivity::utp,
                std::placeholders::_1,
                std::placeholders::_2,
                0xFF),
      2);
  run("RDV UTP",
      std::bind(reactor::connectivity::rdv_utp,
                std::placeholders::_1,
                std::placeholders::_2,
                0),
      1);
  run("RDV UTP (XOR)",
      std::bind(reactor::connectivity::rdv_utp,
                std::placeholders::_1,
                std::placeholders::_2,
                0xFF),
      2);

  {
    try
    {
      auto nat = reactor::connectivity::nat(host, port);
      // Super uglY.
      auto cone = nat.find("NOT_CONE") == std::string::npos &&
        nat.find("CONE") != std::string::npos;
      results.nat = {cone};
    }
    catch (reactor::Terminate const&)
    {
      throw;
    }
    catch (...)
    {
      results.nat = {elle::exception_string()};
    }
  }
  {
    auto upnp = reactor::network::UPNP::make();
    try
    {
      results.upnp.sane(true);
      results.upnp.available = false;
      upnp->initialize();
      results.upnp.available = upnp->available();
      results.upnp.external = upnp->external_ip();
      typedef reporting::NetworkingResults::UPNPResult::RedirectionResult::Address
        Address;
      auto redirect = [&] (std::string type,
                           reactor::network::Protocol protocol,
                           uint16_t port)
        {
          reporting::store(results.upnp.redirections, type);
          auto& res = results.upnp.redirections[type];
          try
          {
            auto convert = [] (std::string const& port)
              {
                int i = std::stoi(port);
                uint16_t narrow = i;
                if (narrow != i)
                  elle::err("invalid port: %s", i);
                return narrow;
              };
            auto pm = upnp->setup_redirect(protocol, port);
            res.sane(true);
            res.internal = Address{pm.internal_host, convert(pm.internal_port)};
            res.external = Address{pm.external_host, convert(pm.external_port)};
          }
          catch (reactor::Terminate const&)
          {
            throw;
          }
          catch (...)
          {
            res = {false, elle::exception_string()};
          }
        };
      redirect("tcp", reactor::network::Protocol::tcp, 5678);
      redirect("udt", reactor::network::Protocol::udt, 5679);
    }
    catch (reactor::Terminate const&)
    {
      throw;
    }
    catch (...)
    {
      // UPNP is always considered sane.
      results.upnp.warning(true);
      results.upnp.reason = elle::exception_string();
    }
  }
}

// Return the current user permissions on a given path.
static
std::pair<bool, bool>
permissions(boost::filesystem::path const& path)
{
  if (!boost::filesystem::exists(path))
    throw elle::Error(elle::sprintf("%s doesn't exist", path));
  auto s = boost::filesystem::status(path);
  bool read = s.permissions() & boost::filesystem::perms::owner_read;
  bool write = s.permissions() & boost::filesystem::perms::owner_write;
  return std::make_pair(read, write);
}

// Return the permissions as a string:
// r, w, rw, None, ...
static
std::string
permissions_string(boost::filesystem::path const& path)
{
  try
  {
    auto perms = permissions(path);
    std::string res = "";
    if (perms.first)
      res += "r";
    if (perms.second)
      res += "w";
    if (res.length() == 0)
      res = "None";
    return res;
  }
  catch (...)
  {
    return elle::exception_string();
  }
}

//
static
std::pair<bool, std::string>
has_permission(boost::filesystem::path const& path,
               bool mandatory = true)
{
  auto res = permissions_string(path);
  auto good = (res == "rw");
  auto sane = (good || !mandatory);
  return std::make_pair(sane, res);
}

static
bool
fuse(bool /*verbose*/)
{
#if 0
  try
  {
    elle::system::Process p({"fusermount", "-V"});
    if (verbose)
      std::cout << "fuse: ok" << std::endl;
    return true;
  }
  catch (...)
  {
    return false;
  }
#else
  return true;
#endif
}

static
void
_sanity(boost::program_options::variables_map const& args,
        reporting::SanityResults& result)
{
  // User name.
  try
  {
    auto self_name = self_user_name();
    result.user = {self_name};
  }
  catch (...)
  {
    result.user = {};
  }
  // Space left
  {
    size_t min = 50 * 1024 * 1024;
    double min_ratio = 0.02;
    auto f = boost::filesystem::space(infinit::xdg_data_home());
    result.space_left = {min, min_ratio, f.available, f.capacity};
  }
  // Env.
  {
    auto env = infinit_related_environment();
    result.environment = {env};
  }
  // Permissions.
  {
    auto test_permissions = [&] (boost::filesystem::path const& path)
      {
        if (!boost::filesystem::exists(path))
          reporting::store(result.permissions, path.string(), false, false, false);
        else
        {
          auto s = boost::filesystem::status(path);
          bool read = s.permissions() & boost::filesystem::perms::owner_read;
          bool write = s.permissions() & boost::filesystem::perms::owner_write;
          reporting::store(result.permissions, path.string(), true, read, write);
        }
      };

    test_permissions(elle::system::home_directory());
    test_permissions(infinit::xdg_cache_home());
    test_permissions(infinit::xdg_config_home());
    test_permissions(infinit::xdg_data_home());
    test_permissions(infinit::xdg_state_home());
  }
}

template <typename T>
std::map<std::string, std::pair<T, bool>>
                   parse(std::vector<T> container)
{
  std::map<std::string, std::pair<T, bool>> output;
  for (auto& item: container)
  {
    auto name = item.name;
    output.emplace(std::piecewise_construct,
                   std::forward_as_tuple(name),
                   std::forward_as_tuple(std::move(item), false));
  }
  return output;
}

template <typename T>
std::map<std::string, std::pair<std::unique_ptr<T>, bool>>
                   parse(std::vector<std::unique_ptr<T>> container)
{
  std::map<std::string, std::pair<std::unique_ptr<T>, bool>> output;
  for (auto& item: container)
  {
    auto name = item->name;
    output.emplace(std::piecewise_construct,
                   std::forward_as_tuple(name),
                   std::forward_as_tuple(std::move(item), false));
  }
  return output;
}

static
void
_integrity(boost::program_options::variables_map const& args,
           reporting::IntegrityResults& results)
{
  auto users = parse(ifnt.users_get());
  auto aws_credentials = ifnt.credentials_aws();
  auto gcs_credentials = ifnt.credentials_gcs();
  auto storage_resources = parse(ifnt.storages_get());
  auto drives = parse(ifnt.drives_get());
  auto volumes = parse(ifnt.volumes_get());
  auto networks = parse(ifnt.networks_get());
  for (auto& elem: storage_resources)
  {
    auto& storage = elem.second.first;
    auto& status = elem.second.second;
    if (auto s3config = dynamic_cast<infinit::storage::S3StorageConfig const*>(storage.get()))
    {
      auto it =
        std::find_if(
          aws_credentials.begin(),
          aws_credentials.end(),
          [&s3config] (std::unique_ptr<infinit::AWSCredentials, std::default_delete<infinit::Credentials>> const& credentials)
          {
#define COMPARE(field) (credentials->field == s3config->credentials.field())
            return COMPARE(access_key_id) && COMPARE(secret_access_key);
#undef COMPARE
          });
      status = (it != aws_credentials.end());
      if (status)
        reporting::store(results.storage_resources, storage->name, status, "S3");
      else
        reporting::store(results.storage_resources, storage->name, status, "S3", std::string("Missing credentials"));
    }
    if (auto fsconfig = dynamic_cast<infinit::storage::FilesystemStorageConfig const*>(storage.get()))
    {
      auto perms = has_permission(fsconfig->path);
      status = perms.first;
      reporting::store(results.storage_resources, storage->name, status, "filesystem", perms.second);
    }
    if (auto gcsconfig = dynamic_cast<infinit::storage::GCSConfig const*>(storage.get()))
    {
      auto it =
        std::find_if(
          gcs_credentials.begin(),
          gcs_credentials.end(),
          [&gcsconfig] (std::unique_ptr<infinit::OAuthCredentials, std::default_delete<infinit::Credentials>> const& credentials)
          {
            return credentials->refresh_token == gcsconfig->refresh_token;
          });
      status = (it != gcs_credentials.end());
      if (status)
        reporting::store(results.storage_resources, storage->name, status, "GCS");
      else
        reporting::store(results.storage_resources, storage->name, status, "GCS", std::string("Missing credentials"));
    }
#ifndef INFINIT_WINDOWS
    if (/* auto ssh = */
      dynamic_cast<infinit::storage::SFTPStorageConfig const*>(storage.get()))
    {
      // XXX:
    }
#endif
  }
  for (auto& elem: networks)
  {
    auto const& network = elem.second.first;
    auto& status = elem.second.second;
    std::vector<std::string> storage_names;
    if (network.model)
    {
      if (network.model->storage)
      {
        if (auto strip = dynamic_cast<infinit::storage::StripStorageConfig*>(
              network.model->storage.get()))
          for (auto const& s: strip->storage)
            storage_names.push_back(s->name);
        else
          storage_names.push_back(network.model->storage->name);
      }
    }
    std::vector<std::string> faulty;
    status = storage_names.size() == 0 || std::all_of(
      storage_names.begin(),
      storage_names.end(),
      [&] (std::string const& name) -> bool
      {
        auto it = storage_resources.find(name);
        auto res = (it != storage_resources.end() && it->second.second);
        if (!res)
          faulty.push_back(name);
        return res;
      });
    if (status)
      reporting::store(results.networks, network.name, status);
    else
      reporting::store(results.networks, network.name, status, faulty);
  }
  for (auto& elems: volumes)
  {
    auto const& volume = elems.second.first;
    auto& status = elems.second.second;
    auto network = networks.find(volume.network);
    auto network_presents = network != networks.end();
    status = network_presents && network->second.second;
    if (status)
      reporting::store(results.volumes, volume.name, status);
    else
      reporting::store(results.volumes, volume.name, status, volume.network);
  }
  for (auto& elems: drives)
  {
    auto const& drive = elems.second.first;
    auto& status = elems.second.second;
    auto volume = volumes.find(drive.volume);
    auto volume_presents = volume != volumes.end();
    auto volume_ok = volume_presents && volume->second.second;
    auto network = networks.find(drive.network);
    auto network_presents = network != networks.end();
    auto network_ok = network_presents && network->second.second;
    status = network_ok && volume_ok;
    if (status)
      reporting::store(results.drives, drive.name, status);
    else
      reporting::store(results.drives, drive.name, status, drive.volume);
  }
}

static
void
report_error(std::ostream& out, bool sane, bool warning = false)
{
  if (!sane)
    throw elle::Error("Please refer to each individual error message");
  else if (!script_mode)
  {
    if (warning)
      out << "If you encounter any issues, try fixing the problems indicated by the warning messages";
    else
      out << "All good!";
    out  << std::endl;
  }
}

template <typename Report>
void
output(std::ostream& out,
       Report const& results,
       bool verbose)
{
  if (script_mode)
    infinit::save(out, results);
  else
    results.print(out, verbose);
}


COMMAND(integrity)
{
  reporting::IntegrityResults results;
  _integrity(args, results);
  output(std::cout, results, flag(args, "verbose"));
  report_error(std::cout, results.sane(), results.warning());
}

COMMAND(networking)
{
  reporting::NetworkingResults results;
  _networking(args, results);
  output(std::cout, results, flag(args, "verbose"));
  report_error(std::cout, results.sane(), results.warning());
}

COMMAND(sanity)
{
  reporting::SanityResults results;
  _sanity(args, results);
  output(std::cout, results, flag(args, "verbose"));
  report_error(std::cout, results.sane(), results.warning());
}

COMMAND(run_all)
{
  reporting::All a;
  _sanity(args, a.sanity);
  _integrity(args, a.integrity);
  _networking(args, a.networking);
  output(std::cout, a, flag(args, "verbose"));
  report_error(std::cout, a.sane(), a.warning());
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Mode::OptionDescription verbose =
    { "verbose,v", bool_switch(), "output everything" };

  Modes modes {
    {
      "all",
        "Perform all possible checks",
        &run_all,
        "",
        {
          verbose
            }
    },
    {
      "networking",
        "Perform networking checks",
        &networking,
        "",
        {
          verbose
            }
    },
    {
      "sanity",
        "Perform sanity checks",
        &sanity,
        "",
        {
          verbose
            }
    },
    {
      "integrity",
        "Perform integrity checks",
        &integrity,
        "",
        {
          verbose
            }
    }
  };
  return infinit::main("Infinit diagnostic utility", modes, argc, argv,
                       std::string("path"), boost::none);
}
