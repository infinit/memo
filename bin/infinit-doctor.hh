#ifndef INFINIT_BIN_INFINIT_DOCTOR_HH
# define INFINIT_BIN_INFINIT_DOCTOR_HH

# include <unordered_map>

namespace reporting
{
  typedef std::unordered_map<std::string, std::string> Environment;

  struct Result
  {
    /*------.
    | Types |
    `------*/
    typedef boost::optional<std::string> Reason;

    /*-------------.
    | Construction |
    `-------------*/
    Result(std::string const& name = "XXX");
    Result(std::string const& name,
           bool sane,
           Reason const& reason = Reason{},
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

  protected:
    bool
    show(bool verbose) const;
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

  struct ConfigurationIntegrityResults
    : public Result
  {
    /*-------------.
    | Construction |
    `-------------*/
    using Result::Result;

    struct Result:
      public reporting::Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      using reporting::Result::Result;

      /*---------.
      | Printing |
      `---------*/
      std::ostream&
      print(std::ostream& out, bool verbose) const;
    };

    // Use inheritance maybe ?
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
        reporting::Result::Reason const& reason = reporting::Result::Reason{});
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
      typedef boost::optional<std::vector<std::string>> FaultyStorageResources;

      /*-------------.
      | Construction |
      `-------------*/
      NetworkResult() = default;
      NetworkResult(
        std::string const& name,
        bool sane,
        FaultyStorageResources storage_resources = FaultyStorageResources{},
        Result::Reason extra_reason = Result::Reason{},
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
      typedef boost::optional<std::string> FaultyNetwork;

      /*-------------.
      | Construction |
      `-------------*/
      VolumeResult() = default;
      VolumeResult(std::string const& name,
                   bool sane,
                   FaultyNetwork faulty_network = FaultyNetwork{},
                   Result::Reason extra_reason = Result::Reason{});
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
      typedef boost::optional<std::string> FaultyVolume;

      /*-------------.
      | Construction |
      `-------------*/
      DriveResult() = default;
      DriveResult(std::string const& name,
                  bool sane,
                  FaultyVolume faulty_volume = FaultyVolume{},
                  Result::Reason extra_reason = Result::Reason{});
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
                      Result::Reason r = Result::Reason{"shouldn't be there"});
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
    std::vector<StorageResoucesResult> storage_resources;
    std::vector<NetworkResult> networks;
    std::vector<VolumeResult> volumes;
    std::vector<DriveResult> drives;
    std::vector<LeftoversResult> leftovers;
  };

  struct SystemSanityResults
    : public reporting::Result
  {
    /*------.
    | Types |
    `------*/
    using Result::Result;

    struct UserResult
      : public reporting::Result
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
      : public reporting::Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      SpaceLeft() = default;
      SpaceLeft(size_t minimum,
                double minimum_ratio,
                size_t available,
                size_t capacity,
                Result::Reason const& reason = Result::Reason {});
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

    struct EnvironmentResult
      : public reporting::Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      EnvironmentResult() = default;
      EnvironmentResult(Environment const& environment);
      EnvironmentResult(elle::serialization::SerializerIn& s);

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
      Environment environment;
    };

    struct PermissionResult
      : public reporting::Result
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
    typedef std::vector<PermissionResult> PermissionResults;

    struct FuseResult
      : public reporting::Result
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
    EnvironmentResult environment;
    PermissionResults permissions;
    FuseResult fuse;
  };

  struct ConnectivityResults
    : public ::reporting::Result
  {
    struct BeyondResult
      : public reporting::Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      BeyondResult();
      BeyondResult(bool sane, reporting::Result::Reason const& r = boost::none);

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s);
    };

    struct InterfaceResults
      : public reporting::Result
    {
      /*------.
      | Types |
      `------*/
      typedef std::vector<std::string> IPs;

      /*-------------.
      | Construction |
      `-------------*/
      using Result::Result;
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
      : public reporting::Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      using Result::Result;
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
    typedef std::vector<ProtocolResult> ProtocolResults;

    struct NatResult
      : public reporting::Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      NatResult() = default;
      NatResult(bool cone);
      NatResult(std::string const& error);
      NatResult(elle::serialization::SerializerIn& s);

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

    struct UPNPResult
      : public reporting::Result
    {
      struct RedirectionResult
        : public reporting::Result
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
                          Result::Reason const& reason = Result::Reason {});
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
      UPNPResult(bool available = false);
      UPNPResult(elle::serialization::SerializerIn& s);

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
    NatResult nat;
    UPNPResult upnp;
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


}

#endif
