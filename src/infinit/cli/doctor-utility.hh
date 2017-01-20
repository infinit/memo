#pragma once

// This file is not public, it is made to be included by Doctor.cc
// only.  It could be merged into it, it's here only to avoid
// megafiles.
//
// It is made to be include from within namespaces, so *do not include
// anything from here*.

namespace
{
  using Environ = elle::os::Environ;
  using boost::algorithm::all_of;
  using boost::algorithm::any_of;

  bool no_color = false;

  infinit::Infinit ifnt;

  std::string banished_log_level("reactor.network.UTPSocket:NONE");

  std::string username;
  bool ignore_non_linked = false;

  std::string
  result(bool value)
  {
    return value ? "OK" : "Bad";
  }

  void
  section(std::ostream& out,
          std::string name)
  {
    boost::algorithm::to_upper(name);
    if (!no_color)
      out << "[1m";
    out << name << ":";
    if (!no_color)
      out << "[0m";
    out << std::endl;
  }

  template <typename C, typename ... Args>
  typename C::reference
  store(C& container,
        std::string const& key,
        Args&&... args)
  {
    container.emplace_back(key, std::forward<Args>(args)...);
    return container.back();
  }

  template <typename C>
  bool
  sane_(C const& c, bool all = true)
  {
    auto filter = [&] (auto const& x)
      {
        return x.sane();
      };
    if (all)
      return all_of(c, filter);
    else
      return any_of(c, filter);
  }

  template <typename C>
  bool
  warning_(C const& c)
  {
    return any_of(c,
                  [&] (auto const& x)
                  {
                    return x.warning();
                  });
  }

  std::ostream&
  status(std::ostream& out,
         bool sane,
         bool warn = false)
  {
    if (!sane)
    {
      if (!no_color)
        out << "[33;00;31m";
      out << "[ERROR]";
    }
    else if (warn)
    {
      if (!no_color)
        out << "[33;00;33m";
      out << "[WARNING]";
    }
    else
    {
      if (!no_color)
        out << "[33;00;32m";
      out << "[OK]";
    }
    return out << (no_color ? "" : "[0m");
  }

  std::ostream&
  print_reason(std::ostream& out, std::string const& reason, int indent = 2)
  {
    out << std::string(indent, ' ');
    if (!no_color)
      out << "[1m";
    out << "Reason:";
    if (!no_color)
      out << "[0m";
    out << " " << reason;
    return out;
  }

  template <typename C>
  void
  print_(std::ostream& out,
         std::string const& name,
         C& container,
         bool verbose)
  {
    bool sane = true;
    bool warning = false;
    for (auto const& x: container)
    {
      sane &= x.sane();
      warning |= x.warning();
      if (!sane && warning)
        break;
    }
    bool broken = !sane || warning;
    status(out, sane, warning) << " " << name;
    if (verbose || broken)
    {
      if (!container.empty())
        out << ":";
      out << "\n";
      // Because print can be called from a const method, store indexes and sort
      // them.
      auto indexes = std::vector<size_t>(container.size());
      boost::iota(indexes, 0);
      boost::sort(indexes,
                  [&container](auto i1, auto i2)
                  {
                    auto const& l = container[i1];
                    auto const& r = container[i2];
                    return
                      std::forward_as_tuple(l.sane(), !l.warning(), l.name())
                      < std::forward_as_tuple(r.sane(), !r.warning(), r.name());
                  });
      for (auto const& index: indexes)
      {
        auto item = container[index];
        if (verbose || !item.sane() || item.warning())
        {
          status(out << "  ", item.sane(), item.warning()) << " " << item.name();
          item.print(out, verbose);
          out << std::endl;
        }
      }
    }
    else
      out << std::endl;
  }

  struct Result
  {
    /*------.
    | Types |
    `------*/
    using Reason = boost::optional<std::string>;

    /*-------------.
    | Construction |
    `-------------*/
    Result(std::string const& name = "XXX");
    Result(std::string const& name,
           bool sane,
           Reason const& reason = {},
           bool warning = false);
    Result(elle::serialization::SerializerIn& s);
    virtual
    ~Result();

    /*--------------.
    | Serialization |
    `--------------*/
    void
    serialize(elle::serialization::Serializer& s);

    /*---------.
    | Printing |
    `---------*/
    virtual
    void
    _print(std::ostream& out, bool verbose) const;
    std::ostream&
    print(std::ostream& out, bool verbose, bool rc = true) const;

  public:
    bool
    show(bool verbose) const;
  protected:
    virtual
    bool
    _show(bool verbose) const;

    /*-----------.
    | Attributes |
    `-----------*/
    ELLE_ATTRIBUTE_RW(std::string, name);
    ELLE_ATTRIBUTE_RW(bool, sane, virtual);
    Reason reason;
    ELLE_ATTRIBUTE_RW(bool, warning, virtual);
  };

  /// A means to disambiguate when we have several types named Result.
  using BasicResult = Result;

  struct ConfigurationIntegrityResults
    : public BasicResult
  {
    /*-------------.
    | Construction |
    `-------------*/
    using Super = BasicResult;
    using Super::Super;

    struct Result
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      using Super = BasicResult;
      using Super::Super;

      /*---------.
      | Printing |
      `---------*/
      std::ostream&
      print(std::ostream& out, bool verbose) const;
    };

    struct UserResult
      : public Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      UserResult() = default;
      UserResult(std::string const& name,
                 bool sane,
                 Reason const& reason = Reason{});

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool verbose) const override;

      /*-----------.
      | Attributes |
      `-----------*/
      ELLE_ATTRIBUTE_RW(std::string, user_name);
    };

    // Use inheritance maybe?
    struct StorageResoucesResult
      : public Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      StorageResoucesResult() = default;
      StorageResoucesResult(
        std::string const& name,
        bool sane,
        std::string const& type,
        BasicResult::Reason const& reason = {});
      StorageResoucesResult(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      std::string type;
    };

    struct NetworkResult
      : public Result
    {
      /*------.
      | Types |
      `------*/
      using Super = Result;
      using FaultyStorageResources = boost::optional<
        std::vector<StorageResoucesResult>
      >;

      /*-------------.
      | Construction |
      `-------------*/
      NetworkResult() = default;
      NetworkResult(
        std::string const& name,
        bool sane,
        FaultyStorageResources storage_resources = {},
        Result::Reason extra_reason = {},
        bool linked = true);
      NetworkResult(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool verbose) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*----------.
      | Interface |
      `----------*/
      bool
      warning() const override;

      /*-----------.
      | Attributes |
      `-----------*/
      bool linked;
      FaultyStorageResources storage_resources;
    };

    struct VolumeResult
      : public Result
    {
      /*------.
      | Types |
      `------*/
      using FaultyNetwork = boost::optional<NetworkResult>;

      /*-------------.
      | Construction |
      `-------------*/
      VolumeResult() = default;
      VolumeResult(std::string const& name,
                   bool sane,
                   FaultyNetwork faulty_network = {},
                   Result::Reason extra_reason = {});
      VolumeResult(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      FaultyNetwork faulty_network;
    };

    struct DriveResult
      : public Result
    {
      /*------.
      | Types |
      `------*/
      using FaultyVolume = boost::optional<VolumeResult>;

      /*-------------.
      | Construction |
      `-------------*/
      DriveResult() = default;
      DriveResult(std::string const& name,
                  bool sane,
                  FaultyVolume faulty_volume = {},
                  Result::Reason extra_reason = {});
      DriveResult(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      FaultyVolume faulty_volume;
    };

    struct LeftoversResult
      :  public Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      LeftoversResult() = default;
      LeftoversResult(std::string const& name,
                      Result::Reason r = Result::Reason{"should not be there"});
      LeftoversResult(elle::serialization::SerializerIn& s);

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);
    };

    /*-------------.
    | Construction |
    `-------------*/
    ConfigurationIntegrityResults(bool only = true);
    ConfigurationIntegrityResults(elle::serialization::SerializerIn& s);

    /*----------.
    | Interface |
    `----------*/
    bool
    sane() const override;
    bool
    warning() const override;

    /*---------.
    | Printing |
    `---------*/
    void
    print(std::ostream& out, bool verbose) const;

    /*--------------.
    | Serialization |
    `--------------*/
    void
    serialize(elle::serialization::Serializer& s);

    /*-----------.
    | Attributes |
    `-----------*/
    ELLE_ATTRIBUTE_R(bool, only);
    UserResult user;
    std::vector<StorageResoucesResult> storage_resources;
    std::vector<NetworkResult> networks;
    std::vector<VolumeResult> volumes;
    std::vector<DriveResult> drives;
    std::vector<LeftoversResult> leftovers;
  };

  struct SystemSanityResults
    : public BasicResult
  {
    /*------.
    | Types |
    `------*/
    using Super = BasicResult;
    using Super::Super;

    struct UserResult
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      UserResult() = default;
      UserResult(std::tuple<bool, Result::Reason> const& validity,
                 std::string const &name);
      UserResult(std::string const& name);

      /*----------.
      | Interface |
      `----------*/
      std::tuple<bool, Result::Reason>
      valid(std::string const& name) const;

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool verbose) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      ELLE_ATTRIBUTE_R(std::string, user_name);
    };

    struct SpaceLeft
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      SpaceLeft() = default;
      SpaceLeft(size_t minimum,
                double minimum_ratio,
                size_t available,
                size_t capacity,
                Result::Reason const& reason = {});
      SpaceLeft(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool verbose) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      uint64_t minimum;
      double minimum_ratio;
      uint64_t available;
      uint64_t capacity;
    };

    struct EnvironResult
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      EnvironResult() = default;
      EnvironResult(Environ const& environ);
      EnvironResult(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      bool
      _show(bool verbose) const override;
      void
      _print(std::ostream& out, bool verbose) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      Environ environ;
    };

    struct PermissionResult
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      PermissionResult() = default;
      PermissionResult(std::string const& name,
                       bool exists, bool read, bool write);
      PermissionResult(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      void
      print(std::ostream& out, bool verbose) const;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      bool exists;
      bool read;
      bool write;
    };
    using PermissionResults = std::vector<PermissionResult>;

    struct FuseResult
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      FuseResult() = default;
      FuseResult(bool sane);
      FuseResult(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool verbose) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);
    };

    /*-------------.
    | Construction |
    `-------------*/
    SystemSanityResults(bool only = true);
    SystemSanityResults(elle::serialization::SerializerIn& s);

    /*----------.
    | Interface |
    `----------*/
    bool
    sane() const override;
    bool
    warning() const override;

    /*---------.
    | Printing |
    `---------*/
    void
    print(std::ostream& out, bool verbose) const;

    /*--------------.
    | Serialization |
    `--------------*/
    void
    serialize(elle::serialization::Serializer& s);

    /*-----------.
    | Attributes |
    `-----------*/
    ELLE_ATTRIBUTE_R(bool, only);
    UserResult user;
    SpaceLeft space_left;
    EnvironResult environ;
    PermissionResults permissions;
    FuseResult fuse;
  };

  struct ConnectivityResults
    : public BasicResult
  {
    struct BeyondResult
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      BeyondResult();
      BeyondResult(bool sane, BasicResult::Reason const& r = {});

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);
    };

    struct InterfaceResults
      : public BasicResult
    {
      /*------.
      | Types |
      `------*/
      using Super = BasicResult;
      using IPs = std::vector<std::string>;

      /*-------------.
      | Construction |
      `-------------*/
      using Super::Super;

      InterfaceResults() = default;
      InterfaceResults(IPs const& ips);
      InterfaceResults(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool verbose) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      IPs entries;
    };

    struct ProtocolResult
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      using Super = BasicResult;
      using Super::Super;
      ProtocolResult() = default;
      ProtocolResult(std::string const& name,
                     std::string const& address,
                     uint16_t local_port,
                     uint16_t remote_port,
                     bool internal);
      ProtocolResult(std::string const& name,
                     std::string const& error);

      /*---------.
      | Printing |
      `---------*/
      void
      print(std::ostream& out, bool verbose) const;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      boost::optional<std::string> address;
      boost::optional<uint16_t> local_port;
      boost::optional<uint16_t> remote_port;
      boost::optional<bool> internal;
    };
    using ProtocolResults = std::vector<ProtocolResult>;

    struct NATResult
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      NATResult() = default;
      NATResult(bool cone);
      NATResult(std::string const& error);
      NATResult(elle::serialization::SerializerIn& s);

      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool verbose) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      bool cone;
    };

    struct UPnPResult
      : public BasicResult
    {
      struct RedirectionResult
        : public BasicResult
      {
        // This probably exist somewhere...
        struct Address
          : public elle::Printable
        {
          /*-------------.
          | Construction |
          `-------------*/
          Address(std::string host,
                  uint16_t port);
          Address(elle::serialization::SerializerIn& s);

          /*---------.
          | Printing |
          `---------*/
          void
          print(std::ostream& out) const override;

          /*--------------.
          | Serialization |
          `--------------*/
          void
          serialize(elle::serialization::Serializer& s);

          /*-----------.
          | Attributes |
          `-----------*/
          std::string host;
          uint16_t port;
        };

        /*-------------.
        | Construction |
        `-------------*/
        RedirectionResult(std::string const& name = "",
                          bool sane = false,
                          Result::Reason const& reason = {});
        RedirectionResult(elle::serialization::SerializerIn& s);

        /*---------.
        | Printing |
        `---------*/
        void
        print(std::ostream& out, bool verbose) const;

        /*--------------.
        | Serialization |
        `--------------*/
        void
        serialize(elle::serialization::Serializer& s);

        /*-----------.
        | Attributes |
        `-----------*/
        boost::optional<Address> internal;
        boost::optional<Address> external;
      };

      /*-------------.
      | Construction |
      `-------------*/
      UPnPResult(bool available = false);
      UPnPResult(elle::serialization::SerializerIn& s);

      /*----------.
      | Interface |
      `----------*/
      bool
      sane() const override;
      void
      sane(bool) override;
      bool
      warning() const override;
      void
      warning(bool) override;
      /*---------.
      | Printing |
      `---------*/
      void
      _print(std::ostream& out, bool verbose) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);

      /*-----------.
      | Attributes |
      `-----------*/
      bool available;
      boost::optional<std::string> external;
      std::vector<RedirectionResult> redirections;
    };

    /*-------------.
    | Construction |
    `-------------*/
    ConnectivityResults(bool only = true);
    ConnectivityResults(elle::serialization::SerializerIn& s);

    /*---------.
    | Printing |
    `---------*/
    void
    print(std::ostream& out, bool verbose) const;

    /*----------.
    | Interface |
    `----------*/
    bool
    sane() const override;
    bool
    warning() const override;

    /*--------------.
    | Serialization |
    `--------------*/
    void
    serialize(elle::serialization::Serializer& s);

    /*-----------.
    | Attributes |
    `-----------*/
    ELLE_ATTRIBUTE_R(bool, only);
    BeyondResult beyond;
    InterfaceResults interfaces;
    ProtocolResults protocols;
    NATResult nat;
    UPnPResult upnp;
  };

  struct All
  {
    /*-------------.
    | Construction |
    `-------------*/
    All();
    All(elle::serialization::SerializerIn& s);

    /*----------.
    | Interface |
    `----------*/
    bool
    sane() const;
    bool
    warning() const;

    /*---------.
    | Printing |
    `---------*/
    void
    print(std::ostream& out, bool verbose) const;

    /*--------------.
    | Serialization |
    `--------------*/
    void
    serialize(elle::serialization::Serializer& s);

    /*-----------.
    | Attributes |
    `-----------*/
    ConfigurationIntegrityResults configuration_integrity;
    SystemSanityResults system_sanity;
    ConnectivityResults connectivity;
  };

  Result::Result(std::string const& name)
    : _name(name)
    , _sane(false)
    , reason()
    , _warning(false)
  {}

  Result::Result(std::string const& name,
                 bool sane,
                 Reason const& reason,
                 bool warning)
    : _name(name)
    , _sane(sane)
    , reason(reason)
    , _warning(warning)
  {}

  Result::Result(elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  Result::~Result()
  {}

  void
  Result::_print(std::ostream& out, bool verbose) const
  {
    if (this->show(verbose) && this->sane())
      out << " OK";
  }

  bool
  Result::show(bool verbose) const
  {
    return (!this->sane() || verbose || this->warning()) && this->_show(verbose);
  }

  bool
  Result::_show(bool verbose) const
  {
    return true;
  }

  std::ostream&
  Result::print(std::ostream& out, bool verbose, bool rc) const
  {
    status(out, this->sane(), this->warning()) << " " << this->name();
    if (this->show(verbose))
      out << ":";
    this->_print(out, verbose);
    if (this->show(verbose) && this->reason)
      print_reason(out << std::endl, *this->reason, 2);
    return out << std::endl;
  }

  void
  Result::serialize(elle::serialization::Serializer& s)
  {
    s.serialize("name", this->_name);
    s.serialize("sane", this->_sane);
    s.serialize("reason", this->reason);
    s.serialize("warning", this->_warning);
  }

  std::ostream&
  ConfigurationIntegrityResults::Result::print(
    std::ostream& out, bool verbose) const
  {
    if (!this->sane())
      out << " is faulty because";
    this->_print(out, verbose);
    if (!this->sane() && this->reason)
      out << " " << *this->reason;
    return out;
  }

  ConfigurationIntegrityResults::UserResult::UserResult(std::string const& name,
                                                        bool sane,
                                                        Reason const& reason)
    : Result("User", sane, reason)
    , _user_name(name)
  {}

  void
  ConfigurationIntegrityResults::UserResult::_print(std::ostream& out,
                                                    bool verbose) const
  {
    if (this->show(verbose) && this->sane())
      elle::fprintf(out,
                    " \"%s\" exists and has a private key", this->user_name());
  }

  ConfigurationIntegrityResults::StorageResoucesResult::StorageResoucesResult(
    std::string const& name,
    bool sane,
    std::string const& type,
    Result::Reason const& reason)
    : Result(name, sane, reason)
    , type(type)
  {}

  ConfigurationIntegrityResults::StorageResoucesResult::StorageResoucesResult(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConfigurationIntegrityResults::StorageResoucesResult::_print(
    std::ostream& out, bool) const
  {}

  void
  ConfigurationIntegrityResults::StorageResoucesResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("type", this->type);
  }

  ConfigurationIntegrityResults::NetworkResult::NetworkResult(
    std::string const& name,
    bool sane,
    FaultyStorageResources storage_resources,
    Result::Reason extra_reason,
    bool linked)
    : Result(name, sane, extra_reason, !linked)
    , linked(linked)
    , storage_resources(storage_resources)
  {}

  ConfigurationIntegrityResults::NetworkResult::NetworkResult(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConfigurationIntegrityResults::NetworkResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("linked", this->linked);
    s.serialize("storage_resources", this->storage_resources);
  }

  bool
  ConfigurationIntegrityResults::NetworkResult::warning() const
  {
    return Super::warning() || (!this->linked && !ignore_non_linked);
  }

  void
  ConfigurationIntegrityResults::NetworkResult::_print(
    std::ostream& out, bool verbose) const
  {
    if (!this->linked)
      elle::fprintf(out, "is not linked [for user \"%s\"]", username);
    if (this->storage_resources)
      if (auto s = this->storage_resources->size())
      {
        out << " " << elle::join(
          *storage_resources,
          "], [",
          [] (auto const& t)
          {
            std::stringstream out;
            out << "\"" << t.name() << "\"";
            if (t.reason)
              out << " (" << *t.reason << ")";
            return out.str();
          });
        if (s == 1)
          out << " storage resource is faulty";
        else
          out << " storage resources are faulty";
      }
  }

  ConfigurationIntegrityResults::VolumeResult::VolumeResult(
    std::string const& name,
    bool sane,
    FaultyNetwork faulty_network,
    Result::Reason extra_reason)
    : Result(name, sane, extra_reason, faulty_network && faulty_network->warning())
    , faulty_network(faulty_network)
  {}

  ConfigurationIntegrityResults::VolumeResult::VolumeResult(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConfigurationIntegrityResults::VolumeResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("network", this->faulty_network);
  }

  void
  ConfigurationIntegrityResults::VolumeResult::_print(
    std::ostream& out, bool) const
  {
    if ((!this->sane() || this->warning()) && this->faulty_network)
    {
      out << " ";
      if (this->sane())
        out << "(";
      out << "network \"" << this->faulty_network->name() << "\" is ";
      if (!this->faulty_network->linked)
        out << "not linked";
      else
        out << "faulty";
      if (this->sane())
        out << ")";
    }
  }

  ConfigurationIntegrityResults::DriveResult::DriveResult(
    std::string const& name,
    bool sane,
    FaultyVolume faulty_volume,
    Result::Reason extra_reason)
    : Result(name, sane, extra_reason)
    , faulty_volume(faulty_volume)
  {}

  ConfigurationIntegrityResults::DriveResult::DriveResult(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConfigurationIntegrityResults::DriveResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("volume", this->faulty_volume);
  }

  void
  ConfigurationIntegrityResults::DriveResult::_print(
    std::ostream& out, bool) const
  {
    if (!this->sane())
    {
      if (this->faulty_volume)
      {
        out << " volume \"" << this->faulty_volume->name() << "\" is ";
        if (this->faulty_volume->reason)
          out << this->faulty_volume->reason;
        else
          out << "faulty";
      }
    }
  }

  ConfigurationIntegrityResults::LeftoversResult::LeftoversResult(
    std::string const& name,
    Result::Reason r)
    : Result(name, true, r, true)
  {}

  ConfigurationIntegrityResults::LeftoversResult::LeftoversResult(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConfigurationIntegrityResults::LeftoversResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
  }

  ConfigurationIntegrityResults::ConfigurationIntegrityResults(bool only)
    : _only(only)
  {}

  ConfigurationIntegrityResults::ConfigurationIntegrityResults(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  bool
  ConfigurationIntegrityResults::sane() const
  {
    return this->user.sane()
      && sane_(this->storage_resources)
      && sane_(this->networks)
      && sane_(this->volumes)
      && sane_(this->drives);
    // Leftovers is always sane.
  }

  bool
  ConfigurationIntegrityResults::warning() const
  {
    return this->user.warning()
      || warning_(this->storage_resources)
      || warning_(this->networks)
      || warning_(this->volumes)
      || warning_(this->drives)
      || warning_(this->leftovers);
  }

  void
  ConfigurationIntegrityResults::serialize(elle::serialization::Serializer& s)
  {
    s.serialize("storage resources", this->storage_resources);
    s.serialize("networks", this->networks);
    s.serialize("volumes", this->volumes);
    s.serialize("drives", this->drives);
    s.serialize("leftovers", this->leftovers);
    if (s.out())
    {
      bool sane = this->sane();
      s.serialize("sane", sane);
    }
  }

  void
  ConfigurationIntegrityResults::print(std::ostream& out, bool verbose) const
  {
    if (!this->only())
      section(out, "Configuration integrity");
    this->user.print(out, verbose);
    print_(out, "Storage resources", storage_resources, verbose);
    print_(out, "Networks", networks, verbose);
    print_(out, "Volumes", volumes, verbose);
    print_(out, "Drives", drives, verbose);
    print_(out, "Leftovers", leftovers, verbose);
    out << std::endl;
  }

  SystemSanityResults::UserResult::UserResult(
    std::tuple<bool, Result::Reason> const& validity,
    std::string const &name)
    : Result("Username", true, std::get<1>(validity), !std::get<0>(validity))
    , _user_name(name)
  {}

  SystemSanityResults::UserResult::UserResult(std::string const& name)
    : UserResult(valid(name), name)
  {}

  std::tuple<bool, Result::Reason>
  SystemSanityResults::UserResult::valid(std::string const& name) const
  {
    static const auto allowed = std::regex(infinit::User::name_regex());
    if (std::regex_match(name, allowed))
      return std::make_tuple(true, Result::Reason{});
    else
      return std::make_tuple(
        false,
        Result::Reason{
          elle::sprintf(
            "default system user name \"%s\" is not compatible with Infinit "
            "naming policies, you'll need to use --as <other_name>", name)});
  }

  void
  SystemSanityResults::UserResult::_print(std::ostream& out, bool verbose) const
  {
    if (this->show(verbose) && this->sane())
      out << " " << this->_user_name;
  }

  void
  SystemSanityResults::UserResult::serialize(elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("user_name", this->_user_name);
  }

  SystemSanityResults::SpaceLeft::SpaceLeft(size_t minimum,
                                            double minimum_ratio,
                                            size_t available,
                                            size_t capacity,
                                            Result::Reason const& reason)
    : Result("Space left",
             capacity != 0,
             reason,
             ((available / (double) capacity) < minimum_ratio)
             || available < minimum)
    , minimum(minimum)
    , minimum_ratio(minimum_ratio)
    , available(available)
    , capacity(capacity)
  {}

  SystemSanityResults::SpaceLeft::SpaceLeft(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  SystemSanityResults::SpaceLeft::_print(std::ostream& out, bool verbose) const
  {
    if (this->show(verbose))
    {
      if (!this->sane() || this->warning())
        out << std::endl << "  - " << "low";
      elle::fprintf(
        out, " %s available (~%.1f%%)",
        elle::human_data_size(this->available, false),
        100 * this->available / (double) this->capacity);
    }
  }

  void
  SystemSanityResults::SpaceLeft::serialize(elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("minimum", this->minimum);
    s.serialize("minimum_ratio", this->minimum_ratio);
    s.serialize("available", this->available);
    s.serialize("capacity", this->capacity);
  }

  SystemSanityResults::EnvironResult::EnvironResult(
    Environ const& environ)
    : Result("Environment",
             true,
             (environ.empty()
              ? Result::Reason{}
              : Result::Reason{
               "your environment contains variables that will modify Infinit "
               "default behavior. For more details visit "
               "https://infinit.sh/documentation/environment-variables"}),
             !environ.empty())
    , environ(environ)
  {}

  SystemSanityResults::EnvironResult::EnvironResult(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  bool
  SystemSanityResults::EnvironResult::_show(bool verbose) const
  {
    return !this->environ.empty();
  }

  void
  SystemSanityResults::EnvironResult::_print(
    std::ostream& out, bool verbose) const
  {
    if (this->show(verbose))
    {
      for (auto const& entry: this->environ)
        out << std::endl << "  " << entry.first << ": " << entry.second;
    }
  }

  void
  SystemSanityResults::EnvironResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("entries", this->environ);
  }

  SystemSanityResults::PermissionResult::PermissionResult(
    std::string const& name,
    bool exists, bool read, bool write)
    : Result(name, exists && read && write)
    , exists(exists)
    , read(read)
    , write(write)
  {}

  SystemSanityResults::PermissionResult::PermissionResult(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  SystemSanityResults::PermissionResult::print(
    std::ostream& out, bool verbose) const
  {
    if (this->show(verbose))
    {
      out
        << " exists: " << result(this->exists)
        << ", readable: " << result(this->read)
        << ", writable: " << result(this->write);
    }
  }

  void
  SystemSanityResults::PermissionResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("exists", this->exists);
    s.serialize("read", this->read);
    s.serialize("write", this->write);
  }

  SystemSanityResults::FuseResult::FuseResult(bool sane)
    : Result("FUSE",
             true,
             (sane
              ? Result::Reason{}
              : Result::Reason{
               "Unable to find or interact with the driver. You won't be able "
               "to mount a filesystem interface through FUSE "
               "(visit https://infinit.sh/get-started for more details)"}),
             !sane)
  {}

  SystemSanityResults::FuseResult::FuseResult(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  SystemSanityResults::FuseResult::_print(std::ostream& out, bool verbose) const
  {
    if (verbose)
    {
      if (this->sane() && !this->warning())
        out << " you will be able to mount a filesystem interface through FUSE";
    }
  }

  void
  SystemSanityResults::FuseResult::serialize(elle::serialization::Serializer& s)
  {
    Result::serialize(s);
  }


  SystemSanityResults::SystemSanityResults(bool only)
    : _only(only)
  {}

  SystemSanityResults::SystemSanityResults(elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  SystemSanityResults::print(std::ostream& out, bool verbose) const
  {
    if (!this->only())
      section(out, "System sanity");
    this->user.print(out, verbose);
    this->space_left.print(out, verbose);
    this->environ.print(out, verbose, false);
    print_(out, "Permissions", this->permissions, verbose);
    this->fuse.print(out, verbose);
    out << std::endl;
  }

  bool
  SystemSanityResults::sane() const
  {
    return this->user.sane()
      && this->space_left.sane()
      && this->environ.sane()
      && this->fuse.sane()
      && sane_(this->permissions);
  }

  bool
  SystemSanityResults::warning() const
  {
    return this->user.warning()
      || this->space_left.warning()
      || this->environ.warning()
      || this->fuse.warning()
      || warning_(this->permissions);
  }

  void
  SystemSanityResults::serialize(elle::serialization::Serializer& s)
  {
    s.serialize("user name", this->user);
    s.serialize("space left", this->space_left);
    s.serialize("environment", this->environ);
    s.serialize("permissions", this->permissions);
    s.serialize("fuse", this->fuse);
    if (s.out())
    {
      bool sane = this->sane();
      s.serialize("sane", sane);
    }
  }

  ConnectivityResults::BeyondResult::BeyondResult(
    bool sane, Result::Reason const& r)
    : BasicResult(
      elle::sprintf("Connection to %s", infinit::beyond()), sane, r)
  {}

  ConnectivityResults::BeyondResult::BeyondResult()
    : BasicResult(elle::sprintf("Connection to %s", infinit::beyond()))
  {}

  void
  ConnectivityResults::BeyondResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
  }

  ConnectivityResults::InterfaceResults::InterfaceResults(IPs const& ips)
    : Result("Local interfaces", !ips.empty())
    , entries(ips)
  {}

  ConnectivityResults::InterfaceResults::InterfaceResults(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConnectivityResults::InterfaceResults::_print(
    std::ostream& out, bool verbose) const
  {
    if (this->show(verbose))
      for (auto const& entry: this->entries)
        out << std::endl << "  " << entry;
  }

  void
  ConnectivityResults::InterfaceResults::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("entries", this->entries);
  }

  ConnectivityResults::ProtocolResult::ProtocolResult(std::string const& name,
                                                      std::string const& address,
                                                      uint16_t local_port,
                                                      uint16_t remote_port,
                                                      bool internal)
    : Result(name, true)
    , address(address)
    , local_port(local_port)
    , remote_port(remote_port)
    , internal(internal)
  {}

  ConnectivityResults::ProtocolResult::ProtocolResult(std::string const& name,
                                                      std::string const& error)
    : Result(name, false, error)
  {}

  void
  ConnectivityResults::ProtocolResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("address", this->address);
    s.serialize("local_port", this->local_port);
    s.serialize("remote_port", this->remote_port);
    s.serialize("internal", this->internal);
  }

  void
  ConnectivityResults::ProtocolResult::print(
    std::ostream& out, bool verbose) const
  {
    if (this->show(verbose))
    {
      if (this->sane())
      {
        if (this->address)
          out << std::endl << "    Address: " << *this->address;
        if (this->local_port)
          out << std::endl << "    Local port: " << *this->local_port;
        if (this->remote_port)
          out << std::endl << "    Remote port: " << *this->remote_port;
        if (this->internal)
          out << std::endl << "    Internal: " << (*this->internal ? "Yes" : "No");
      }
      if (this->reason)
        print_reason(out << std::endl, *this->reason, 4);
    }
  }

  ConnectivityResults::NATResult::NATResult(bool cone)
    : Result("NAT", true)
    , cone(cone)
  {}

  ConnectivityResults::NATResult::NATResult(std::string const& error)
    : Result("NAT", false, error)
  {}

  ConnectivityResults::NATResult::NATResult(elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConnectivityResults::NATResult::_print(
    std::ostream& out, bool verbose) const
  {
    if (this->show(verbose) && this->sane())
      out << " OK (" << (this->cone ? "CONE" : "NOT CONE") << ")";
  }

  void
  ConnectivityResults::NATResult::serialize(elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("cone", cone);
  }

  ConnectivityResults::UPnPResult::RedirectionResult::Address::Address(
    std::string host,
    uint16_t port)
    : host(host)
    , port(port)
  {}

  ConnectivityResults::UPnPResult::RedirectionResult::Address::Address(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConnectivityResults::UPnPResult::RedirectionResult::Address::print(
    std::ostream& out) const
  {
    out << this->host << ":" << this->port;
  }

  void
  ConnectivityResults::UPnPResult::RedirectionResult::Address::serialize(
    elle::serialization::Serializer& s)
  {
    s.serialize("host", this->host);
    s.serialize("port", this->port);
  }

  ConnectivityResults::UPnPResult::RedirectionResult::RedirectionResult(
    std::string const& name,
    bool sane,
    Result::Reason const& reason)
    : Result(name, true, reason, !sane)
  {}

  ConnectivityResults::UPnPResult::RedirectionResult::RedirectionResult(
    elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConnectivityResults::UPnPResult::RedirectionResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("internal", this->internal);
    s.serialize("external", this->external);
  }

  void
  ConnectivityResults::UPnPResult::RedirectionResult::print(
    std::ostream& out, bool verbose) const
  {
    if (this->show(verbose))
    {
      status(out << std::endl << "  ", this->sane(), this->warning())
        << " " << this->name() << ": ";
      if (internal && external)
        out << "local endpoint (" << this->internal
            << ") successfully mapped (to " << this->external << ")";
      if (this->warning() && this->reason)
        out << std::endl << "    Reason: " << *this->reason;
    }
  }

  ConnectivityResults::UPnPResult::UPnPResult(bool available)
    : Result("UPnP", available)
    , available(available)
  {}

  ConnectivityResults::UPnPResult::UPnPResult(elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConnectivityResults::UPnPResult::_print(std::ostream& out, bool verbose) const
  {
    if (this->show(verbose))
    {
      if (this->external)
        out << " external IP address: " << this->external;
      for (auto const& redirection: redirections)
        redirection.print(out, verbose);
    }
  }

  bool
  ConnectivityResults::UPnPResult::sane() const
  {
    return Result::sane() && this->available && sane_(this->redirections);
  }

  void
  ConnectivityResults::UPnPResult::sane(bool s)
  {
    Result::sane(s);
  }

  bool
  ConnectivityResults::UPnPResult::warning() const
  {
    return Result::warning() || warning_(this->redirections);
  }

  void
  ConnectivityResults::UPnPResult::warning(bool w)
  {
    Result::warning(w);
  }

  void
  ConnectivityResults::UPnPResult::serialize(elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("available", this->available);
    s.serialize("external", this->external);
    s.serialize("redirections", this->redirections);
  }

  ConnectivityResults::ConnectivityResults(bool only)
    : _only(only)
  {}

  ConnectivityResults::ConnectivityResults(elle::serialization::SerializerIn& s)
  {
    this->serialize(s);
  }

  void
  ConnectivityResults::print(std::ostream& out, bool verbose) const
  {
    if (!this->only())
      section(out, "Connectivity");
    this->beyond.print(out, verbose);
    this->interfaces.print(out, verbose, false);
    this->nat.print(out, verbose);
    this->upnp.print(out, verbose, false);
    print_(out, "Protocols", this->protocols, verbose);
    out << std::endl;
  }

  bool
  ConnectivityResults::sane() const
  {
    return this->beyond.sane()
      && this->interfaces.sane()
      && this->nat.sane()
      && this->upnp.sane()
      && sane_(this->protocols, false);
  }

  bool
  ConnectivityResults::warning() const
  {
    return this->beyond.warning()
      || this->interfaces.warning()
      || this->nat.warning()
      || this->upnp.warning()
      || warning_(this->protocols);
  }

  void
  ConnectivityResults::serialize(elle::serialization::Serializer& s)
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

  All::All()
    : configuration_integrity(false)
    , system_sanity(false)
    , connectivity(false)
  {}

  All::All(elle::serialization::SerializerIn& s)
    : configuration_integrity(
      s.deserialize<ConfigurationIntegrityResults>("configuration_integrity"))
    , system_sanity(s.deserialize<SystemSanityResults>("system_sanity"))
    , connectivity(s.deserialize<ConnectivityResults>("connectivity"))
  {}

  void
  All::print(std::ostream& out, bool verbose) const
  {
    configuration_integrity.print(out, verbose);
    system_sanity.print(out, verbose);
    connectivity.print(out, verbose);
  }

  bool
  All::sane() const
  {
    return this->configuration_integrity.sane()
      && this->system_sanity.sane()
      && this->connectivity.sane();
  }

  bool
  All::warning() const
  {
    return this->configuration_integrity.warning()
      || this->system_sanity.warning()
      || this->connectivity.warning();
  }

  void
  All::serialize(elle::serialization::Serializer& s)
  {
    s.serialize("configuration_integrity", this->configuration_integrity);
    s.serialize("system_sanity", this->system_sanity);
    s.serialize("connectivity", this->connectivity);
    if (s.out())
    {
      bool sane = this->sane();
      s.serialize("sane", sane);
    }
  }

  // Return the infinit related environment.
  Environ
  infinit_related_environ()
  {
    using boost::algorithm::starts_with;
    return elle::os::environ([](auto const& k, auto const& v) {
        return ((starts_with(k, "INFINIT_") || starts_with(k, "ELLE_"))
                && v != banished_log_level);
      });
  }

  uint16_t
  get_port(boost::optional<uint16_t> const& port)
  {
    namespace random = infinit::cryptography::random;
    return port.value_or(random::generate<uint16_t>(10000, 65535));
  }


  void
  _connectivity(infinit::cli::Infinit& cli,
                boost::optional<std::string> const& server,
                boost::optional<uint16_t> upnp_tcp_port,
                boost::optional<uint16_t> upnp_udt_port,
                ConnectivityResults& results)
  {
    ELLE_TRACE("contact beyond")
    {
      try
      {
        reactor::http::Request r(infinit::beyond(),
                                 reactor::http::Method::GET, {10_sec});
        reactor::wait(r);
        if (auto status = r.status() == reactor::http::StatusCode::OK)
          results.beyond = {status};
        else
          results.beyond = {status, elle::sprintf("%s", r.status())};
      }
      catch (reactor::http::RequestError const&)
      {
        results.beyond =
          {false, elle::sprintf("Couldn't connect to %s", infinit::beyond())};
      }
      catch (elle::Error const&)
      {
        results.beyond = {false, elle::exception_string()};
      }
    }
    auto public_ips = std::vector<std::string>{};
    ELLE_TRACE("list interfaces")
    {
      auto interfaces = elle::network::Interface::get_map(
        elle::network::Interface::Filter::no_loopback);
      for (auto i: interfaces)
        if (!i.second.ipv4_address.empty())
          public_ips.emplace_back(i.second.ipv4_address);
      results.interfaces = {public_ips};
    }
    using ConnectivityFunction
      = std::function<reactor::connectivity::Result
                      (std::string const& host, uint16_t port)>;
    uint16_t port = 5456;
    auto run = [&] (std::string const& name,
                    ConnectivityFunction const& function,
                    int deltaport = 0)
      {
        static const reactor::Duration timeout = 3_sec;
        ELLE_TRACE("connect using %s to %s:%s", name, *server, port + deltaport);
        std::string result = elle::sprintf("  %s: ", name);
        try
        {
          reactor::TimeoutGuard guard(timeout);
          auto address = function(*server, port + deltaport);
          bool external = !std::contains(public_ips, address.host);
          store(results.protocols, name, *server, address.local_port,
                address.remote_port, !external);
        }
        catch (reactor::Terminate const&)
        {
          throw;
        }
        catch (reactor::network::ResolutionError const& error)
        {
          store(results.protocols, name,
                elle::sprintf("Couldn't connect to %s", error.host()));
        }
        catch (reactor::Timeout const&)
        {
          store(results.protocols, name,
                elle::sprintf("Couldn't connect after 3 seconds"));
        }
        catch (...)
        {
          store(results.protocols, name, elle::exception_string());
        }
      };
    run("TCP", reactor::connectivity::tcp);
    run("UDP", reactor::connectivity::udp);
    run("UTP",
        [](auto const& host, auto const& port)
        { return reactor::connectivity::utp(host, port, 0); },
        1);
    run("UTP (XOR)",
        [](auto const& host, auto const& port)
        { return reactor::connectivity::utp(host, port, 0xFF); },
        2);
    run("RDV UTP",
        [](auto const& host, auto const& port)
        { return reactor::connectivity::rdv_utp(host, port, 0); },
        1);
    run("RDV UTP (XOR)",
        [](auto const& host, auto const& port)
        { return reactor::connectivity::rdv_utp(host, port, 0xFF); },
        2);
    ELLE_TRACE("NAT")
    {
      try
      {
        auto nat = reactor::connectivity::nat(*server, port);
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
    ELLE_TRACE("UPnP")
    {
      auto upnp = reactor::network::UPNP::make();
      try
      {
        results.upnp.sane(true);
        results.upnp.available = false;
        upnp->initialize();
        results.upnp.available = upnp->available();
        results.upnp.warning(true);
        results.upnp.external = upnp->external_ip();
        results.upnp.warning(false);
        using Address =
          ConnectivityResults::UPnPResult::RedirectionResult::Address;
        auto redirect = [&] (reactor::network::Protocol protocol,
                             uint16_t port)
          {
            auto type = elle::sprintf("%s", protocol);
            auto& res = store(results.upnp.redirections, type);
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
              res.warning(false);
              res.internal = Address{pm.internal_host, convert(pm.internal_port)};
              res.external = Address{pm.external_host, convert(pm.external_port)};
            }
            catch (reactor::Terminate const&)
            {
              throw;
            }
            catch (...)
            {
              res = {type, false, elle::exception_string()};
            }
          };
        redirect(reactor::network::Protocol::tcp, get_port(upnp_tcp_port));
        redirect(reactor::network::Protocol::udt, get_port(upnp_udt_port));
      }
      catch (reactor::Terminate const&)
      {
        throw;
      }
      catch (...)
      {
        // UPnP is always considered sane.
        results.upnp.warning(true);
        results.upnp.reason = elle::exception_string();
      }
    }
  }

  // Return the current user permissions on a given path.
  std::pair<bool, bool>
  permissions(bfs::path const& path)
  {
    if (!bfs::exists(path))
      elle::err("doesn't exist");
    auto s = bfs::status(path);
    bool read = (s.permissions() & bfs::perms::owner_read)
      && (s.permissions() & bfs::perms::others_read);
    bool write = (s.permissions() & bfs::perms::owner_write)
      && (s.permissions() & bfs::perms::others_write);
    if (!read)
    {
      boost::system::error_code code;
      bfs::recursive_directory_iterator it(path, code);
      if (!code)
        read = true;
    }
    if (!write)
    {
      auto p = path / ".tmp";
      boost::system::error_code code;
      elle::SafeFinally remove([&] { bfs::remove(p, code); });
      bfs::ofstream f;
      {
        f.open(p, std::ios_base::out);
        f << "test";
        f.flush();
      }
      write = f.good();
    }
    return std::make_pair(read, write);
  }

  namespace
  {
    constexpr auto read_only = "read only";
    constexpr auto read_write = "readable and writable";
    constexpr auto write_only = "write only";
    constexpr auto not_accessible = "not accessible (permissions denied)";
  }

  /// Return the permissions as a human readable string:
  /// "is read only"
  /// "is write only"
  /// "is readable and writable"
  /// "is not accessible (permissions denied)
  std::string
  permissions_string(bfs::path const& path)
  {
    try
    {
      auto perms = permissions(path);
      std::string res = "is ";
      if (perms.first && !perms.second)
        res += read_only;
      else if (perms.second && !perms.first)
        res += write_only;
      else if (perms.second && perms.first)
        res += read_write;
      else
        res += not_accessible;
      return res;
    }
    catch (...)
    {
      return elle::exception_string();
    }
  }

  std::pair<bool, std::string>
  has_permission(bfs::path const& path,
                 bool mandatory = true)
  {
    auto res = permissions_string(path);
    auto good = res.find(read_write) != std::string::npos;
    auto sane = good || !mandatory;
    return std::make_pair(sane, res);
  }

  bool
  fuse(bool /*verbose*/)
  {
    try
    {
      struct NoOp : reactor::filesystem::Operations
      {
        std::shared_ptr<reactor::filesystem::Path>
        path(std::string const& path) override
        {
          return nullptr;
        }
      };

      reactor::filesystem::FileSystem f(std::make_unique<NoOp>(), false);
      auto d = elle::filesystem::TemporaryDirectory{};
      f.mount(d.path(), {});
      f.unmount();
      f.kill();
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  void
  _system_sanity(infinit::cli::Infinit& cli,
                 SystemSanityResults& result)
  {
    result.fuse = {fuse(false)};
    ELLE_TRACE("user name")
      try
      {
        auto owner = cli.as_user();
        result.user = {owner.name};
      }
      catch (...)
      {
        result.user = {};
      }
    ELLE_TRACE("calculate space left")
    {
      size_t min = 50 * 1024 * 1024;
      double min_ratio = 0.02;
      auto f = bfs::space(infinit::xdg_data_home());
      result.space_left = {min, min_ratio, f.available, f.capacity};
    }
    ELLE_TRACE("look for Infinit related environment")
    {
      auto env = infinit_related_environ();
      result.environ = {env};
    }
    ELLE_TRACE("check permissions")
    {
      auto test_permissions = [&] (bfs::path const& path)
        {
          if (bfs::exists(path))
          {
            auto perms = permissions(path);
            store(result.permissions, path.string(), true, perms.first,
                  perms.second);
          }
          else
            store(result.permissions, path.string(), false, false, false);
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
    auto res = std::map<std::string, std::pair<T, bool>>{};
    for (auto& item: container)
    {
      auto name = item.name;
      res.emplace(std::piecewise_construct,
                  std::forward_as_tuple(name),
                  std::forward_as_tuple(std::move(item), false));
    }
    return res;
  }

  template <typename T>
  std::map<std::string, std::pair<std::unique_ptr<T>, bool>>
  parse(std::vector<std::unique_ptr<T>> container)
  {
    auto res = std::map<std::string, std::pair<std::unique_ptr<T>, bool>>{};
    for (auto& item: container)
    {
      auto name = item->name;
      res.emplace(std::piecewise_construct,
                  std::forward_as_tuple(name),
                  std::forward_as_tuple(std::move(item), false));
    }
    return res;
  }

  template <typename ExpectedType>
  void
  load(bfs::path const& path, std::string const& type)
  {
    bfs::ifstream i;
    ifnt._open_read(i, path, type, path.filename().string());
    infinit::Infinit::load<ExpectedType>(i);
  }

  void
  _configuration_integrity(infinit::cli::Infinit& cli,
                           bool ignore_non_linked,
                           ConfigurationIntegrityResults& results)
  {
    auto owner = cli.as_user();
    auto users = parse(ifnt.users_get());
    auto aws_credentials = ifnt.credentials_aws();
    auto gcs_credentials = ifnt.credentials_gcs();
    auto storage_resources = parse(ifnt.storages_get());
    auto drives = parse(ifnt.drives_get());
    auto volumes = parse(ifnt.volumes_get());
    username = owner.name;
    try
    {
      if (owner.private_key)
        results.user = {username, true};
      else
        results.user = {
          username,
          false,
          elle::sprintf("user \"%s\" has no private key", username)
        };
    }
    // XXX: Catch a specific error.
    catch (...)
    {
      results.user = {
        username,
        false,
        elle::sprintf("user \"%s\" is not an Infinit user", username)
      };
    }
    using namespace infinit::storage;
    auto networks = parse(ifnt.networks_get(owner));
    ELLE_TRACE("verify storage resources")
      for (auto& elem: storage_resources)
      {
        auto& storage = elem.second.first;
        auto& status = elem.second.second;
        if (auto s3config = dynamic_cast<S3StorageConfig const*>(
              storage.get()))
        {
          auto status = any_of(aws_credentials,
              [&s3config] (auto const& credentials)
              {
#define COMPARE(field) (credentials->field == s3config->credentials.field())
                return COMPARE(access_key_id) && COMPARE(secret_access_key);
#undef COMPARE
              });
        if (status)
          store(results.storage_resources, storage->name, status, "S3");
        else
          store(results.storage_resources, storage->name, status, "S3",
                std::string("credentials are missing"));
        }
        if (auto fsconfig
            = dynamic_cast<FilesystemStorageConfig const*>(storage.get()))
        {
          auto perms = has_permission(fsconfig->path);
          status = perms.first;
          store(results.storage_resources, storage->name, status, "filesystem",
                elle::sprintf("\"%s\" %s", fsconfig->path, perms.second));
        }
        if (auto gcsconfig = dynamic_cast<GCSConfig const*>(storage.get()))
        {
          auto status = any_of(gcs_credentials,
              [&gcsconfig] (auto const& credentials)
              {
                return credentials->refresh_token == gcsconfig->refresh_token;
              });
          if (status)
            store(results.storage_resources, storage->name, status, "GCS");
          else
            store(results.storage_resources, storage->name, status, "GCS",
                  std::string{"credentials are missing"});
        }
#ifndef INFINIT_WINDOWS
        if (dynamic_cast<SFTPStorageConfig const*>(storage.get()))
        {
          // XXX:
        }
#endif
      }
    ELLE_TRACE("verify networks")
      for (auto& elem: networks)
      {
        auto const& network = elem.second.first;
        auto& status = elem.second.second;
        auto storage_names = std::vector<std::string>{};
        bool linked = network.model != nullptr;
        if (linked && network.model->storage)
        {
          if (auto strip = dynamic_cast<StripStorageConfig*>(
                network.model->storage.get()))
            for (auto const& s: strip->storage)
              storage_names.emplace_back(s->name);
          else
            storage_names.emplace_back(network.model->storage->name);
        }
        auto faulty = ConfigurationIntegrityResults::NetworkResult
          ::FaultyStorageResources::value_type{};
        status = storage_names.empty()
          || all_of(storage_names, [&] (std::string const& name) -> bool {
              auto it = boost::find_if(results.storage_resources,
                                       [name] (auto const& t)
                                       {
                                         return name == t.name();
                                       });
              auto res = (it == results.storage_resources.end()
                          || it->show(false));
              if (it != results.storage_resources.end())
                faulty.emplace_back(*it);
              else
                faulty.emplace_back(name, false, "unknown",
                                    std::string{"missing"});
              return !res;
            });
        if (status)
          store(results.networks, network.name, status, boost::none,
                boost::none, linked);
        else
          store(results.networks, network.name, status, faulty,
                boost::none, linked);
      }
    ELLE_TRACE("verify volumes")
      for (auto& elems: volumes)
      {
        auto const& volume = elems.second.first;
        auto& status = elems.second.second;
        auto network = networks.find(volume.network);
        auto network_presents = network != networks.end();
        status = network_presents && network->second.second;
        auto network_result = boost::find_if(results.networks,
                                             [volume] (auto const& n)
                                             {
                                               return volume.network == n.name();
                                             });
        if (network_result != results.networks.end())
          status &= !network_result->warning();
        if (status)
          store(results.volumes, volume.name, status);
        else
        {
          store(results.volumes, volume.name, status,
                (network_result != results.networks.end())
                ? *network_result
                : ConfigurationIntegrityResults::NetworkResult(
                  volume.network, false, {}, std::string{"missing"}
                )
            );
        }
      }
    ELLE_TRACE("verify drives")
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
          store(results.drives, drive.name, status);
        else
        {
          auto it = boost::find_if(results.volumes,
                                   [drive] (auto const& v)
                                   {
                                     return drive.volume == v.name();
                                   });
          store(results.drives, drive.name, status,
                (it != results.volumes.end())
                ? *it
                : ConfigurationIntegrityResults::VolumeResult(
                  drive.volume, false, {}, std::string{"missing"})
            );
        }
      }
    namespace bf = bfs;
    auto& leftovers = results.leftovers;
    auto path_contains_file = [](bfs::path dir, bfs::path file) -> bool
      {
        file.remove_filename();
        return boost::equal(dir, file);
      };
    for (auto it = bfs::recursive_directory_iterator(infinit::xdg_data_home());
         it != bfs::recursive_directory_iterator();
         ++it)
      if (is_regular_file(it->status()) && !infinit::is_hidden_file(it->path()))
      {
        try
        {
          // Share.
          if (path_contains_file(ifnt._network_descriptors_path(), it->path()))
            load<infinit::NetworkDescriptor>(it->path(), "network_descriptor");
          else if (path_contains_file(ifnt._networks_path(), it->path()))
            load<infinit::Network>(it->path(), "network");
          else if (path_contains_file(ifnt._volumes_path(), it->path()))
            load<infinit::Volume>(it->path(), "volume");
          else if (path_contains_file(ifnt._drives_path(), it->path()))
            load<infinit::Drive>(it->path(), "drive");
          else if (path_contains_file(ifnt._passports_path(), it->path()))
            load<infinit::Passport>(it->path(), "passport");
          else if (path_contains_file(ifnt._users_path(), it->path()))
            load<infinit::User>(it->path(), "users");
          else if (path_contains_file(ifnt._storages_path(), it->path()))
            load<std::unique_ptr<infinit::storage::StorageConfig>>(it->path(), "storage");
          else if (path_contains_file(ifnt._credentials_path(), it->path()))
            load<std::unique_ptr<infinit::Credentials>>(it->path(), "credentials");
          else if (path_contains_file(infinit::xdg_data_home() / "blocks", it->path()))
            {}
          else if (path_contains_file(infinit::xdg_data_home() / "ui", it->path()))
            {}
          else
            store(leftovers, it->path().string());
        }
        catch (...)
        {
          store(leftovers, it->path().string(), elle::exception_string());
        }
      }
    for (auto it = bfs::recursive_directory_iterator(infinit::xdg_cache_home());
         it != bfs::recursive_directory_iterator();
         ++it)
      if (is_regular_file(it->status()) && !infinit::is_hidden_file(it->path()))
      {
        try
        {
          if (!path_contains_file(ifnt._user_avatar_path(), it->path())
              && !path_contains_file(ifnt._drive_icon_path(), it->path()))
            store(leftovers, it->path().string());
        }
        catch (...)
        {
          store(leftovers, it->path().string(), elle::exception_string());
        }
      }
    for (auto const& p: bfs::recursive_directory_iterator(infinit::xdg_state_home()))
      if (is_regular_file(p.status()) && !infinit::is_hidden_file(p.path()))
      {
        try
        {
          if (path_contains_file(infinit::xdg_state_home() / "cache", p.path()));
          else if (p.path() == infinit::xdg_state_home() / "critical.log");
          else if (p.path().filename() == "root_block")
          {
            // The root block path is:
            // <qualified_network_name>/<qualified_volume_name>/root_block
            auto network_volume = p.path().parent_path().lexically_relative(
              infinit::xdg_state_home());
            auto name = network_volume.begin();
            std::advance(name, 1); // <network_name>
            auto network = *network_volume.begin() / *name;
            std::advance(name, 1);
            auto volume_owner = name;
            std::advance(name, 1); // <volume_name>
            auto volume = *volume_owner / *name;
            if (volumes.find(volume.string()) == volumes.end())
              store(leftovers, p.path().string(),
                    Result::Reason{"volume is gone"});
            if (networks.find(network.string()) == networks.end())
              store(leftovers, p.path().string(),
                    Result::Reason{"network is gone"});
          }
          else
            store(leftovers, p.path().string());
        }
        catch (...)
        {
          store(leftovers, p.path().string(), elle::exception_string());
        }
      }
  }

  void
  _report_error(infinit::cli::Infinit& cli,
                std::ostream& out, bool sane, bool warning = false)
  {
    if (!sane)
      elle::err("Please refer to each individual error message. "
                "If you cannot figure out how to fix your issues, "
                "please visit https://infinit.sh/faq.");
    else if (!cli.script())
    {
      if (warning)
        out <<
          "Doctor has detected minor issues but nothing that should prevent "
          "Infinit from working.";
      else
        out << "All good, everything should work.";
      out << std::endl;
    }
  }

  template <typename Report>
  void
  _output(infinit::cli::Infinit& cli,
          std::ostream& out,
          Report const& results,
          bool verbose)
  {
    if (cli.script())
      infinit::Infinit::save(out, results, false);
    else
      results.print(out, verbose);
  }
}
