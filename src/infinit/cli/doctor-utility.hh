#pragma once

// NO INCLUDES!!!
//
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

  /// A stream, and some information about the formatting.
  struct Output
  {
    std::ostream& out;
    bool verbose;
    bool color;

    /// General forwarding.
    template <typename T>
    Output& operator<<(T&& t)
    {
      out << std::forward<T>(t);
      return *this;
    }

    /// Help resolving std::endl and the like, which are actually
    /// function templates, not functions.
    Output& operator<<(std::ostream& (*func)(std::ostream&))
    {
      out << func;
      return *this;
    }
  };

  void
  section(Output& out,
          std::string const& name)
  {
    if (out.color)
      out << "[1m";
    out << boost::algorithm::to_upper_copy(name) << ":";
    if (out.color)
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

  Output&
  status(Output& out,
         bool sane, bool warn = false)
  {
    if (!sane)
    {
      if (out.color)
        out << "[33;00;31m";
      out << "[ERROR]";
    }
    else if (warn)
    {
      if (out.color)
        out << "[33;00;33m";
      out << "[WARNING]";
    }
    else
    {
      if (out.color)
        out << "[33;00;32m";
      out << "[OK]";
    }
    if (out.color)
      out << "[0m";
    return out;
  }

  Output&
  print_reason(Output& out,
               std::string const& reason, int indent = 2)
  {
    out << std::string(indent, ' ');
    if (out.color)
      out << "[1m";
    out << "Reason:";
    if (out.color)
      out << "[0m";
    out << " " << reason;
    return out;
  }

  template <typename C>
  void
  print_(Output& out,
         std::string const& name,
         C& container)
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
    if (out.verbose || broken)
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
        if (out.verbose || !item.sane() || item.warning())
        {
          status(out << "  ",
                 item.sane(), item.warning()) << " " << item.name();
          item.print(out);
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
  public:
    Result()
      : Result{"XXXX", false, std::string("default constructed"), true}
    {}

  public:
    Result(std::string const& name,
           bool sane,
           Reason const& reason = {},
           bool warning = false)
      : _name(name)
      , _sane(sane)
      , reason(reason)
      , _warning(warning)
    {}

    virtual
    ~Result() = default;

    /*--------------.
    | Serialization |
    `--------------*/
    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->_name);
      s.serialize("sane", this->_sane);
      s.serialize("reason", this->reason);
      s.serialize("warning", this->_warning);
    }

    /*---------.
    | Printing |
    `---------*/
    virtual
    void
    _print(Output& out) const
    {
      if (this->show(out.verbose) && this->sane())
        out << " OK";
    }

    Output&
    print(Output& out) const
    {
      status(out,
             this->sane(), this->warning()) << " " << this->name();
      if (this->show(out.verbose))
        out << ":";
      this->_print(out);
      if (this->show(out.verbose) && this->reason)
        print_reason(out << std::endl, *this->reason, 2);
      return out << std::endl;
    }

  public:
    bool
    show(bool verbose) const
    {
      return (verbose || !this->sane() || this->warning())
        && this->_show(verbose);
    }
  protected:
    virtual
    bool
    _show(bool) const
    {
      return true;
    }

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
      Output&
      print(Output& out) const
      {
        if (!this->sane())
          out << " is faulty because";
        this->_print(out);
        if (!this->sane() && this->reason)
          out << " " << *this->reason;
        return out;
      }
    };

    struct UserResult
      : public BasicResult
    {
      /*-------------.
      | Construction |
      `-------------*/
      UserResult() = default;
      UserResult(std::string const& name,
                 bool sane,
                 Reason const& reason = Reason{})
        : Result("User", sane, reason)
        , _user_name(name)
      {}

      /*---------.
      | Printing |
      `---------*/
      void
      _print(Output& out) const override
      {
        if (this->show(out.verbose) && this->sane())
          elle::fprintf(out.out,
                        " \"%s\" exists and has a private key", this->user_name());
      }

      /*-----------.
      | Attributes |
      `-----------*/
      ELLE_ATTRIBUTE_RW(std::string, user_name);
    };

    // Use inheritance maybe?
    struct SilosResult
      : public Result
    {
      using Super = Result;
      /*-------------.
      | Construction |
      `-------------*/
      SilosResult() = default;
      SilosResult(std::string const& name,
                  bool sane,
                  std::string const& type,
                  BasicResult::Reason const& reason = {})
        : Super(name, sane, reason)
        , type(type)
      {}

      /*---------.
      | Printing |
      `---------*/
      void
      _print(Output& out) const override
      {}

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("type", this->type);
      }


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
      using FaultySilos = boost::optional<
        std::vector<SilosResult>
      >;

      /*-------------.
      | Construction |
      `-------------*/
      NetworkResult() = default;
      NetworkResult(std::string const& name,
                    bool sane,
                    bool ignore_non_linked,
                    std::string const& username,
                    FaultySilos silos = {},
                    Result::Reason extra_reason = {},
                    bool linked = true)
        : Super(name, sane, extra_reason, !linked)
        , silos(silos)
        , linked(linked)
        , ignore_non_linked(ignore_non_linked)
        , username(username)
      {}

      /*---------.
      | Printing |
      `---------*/
      void
      _print(Output& out) const override
      {
        if (!this->linked)
          elle::fprintf(out.out, "is not linked [for user \"%s\"]", this->username);
        if (this->silos)
          if (auto num = this->silos->size())
          {
            char const* sep = "";
            for (auto const& s: *silos)
            {
              out << sep;
              sep = ", ";
              out << " \"" << s.name() << '"';
              if (s.reason)
                out << " (" << *s.reason << ')';
            }
            out << (num == 1 ? " silo is faulty" : " silos are faulty");
          }
      }

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
        s.serialize("linked", this->linked);
        s.serialize("silos", this->silos);
      }

      /*----------.
      | Interface |
      `----------*/
      bool
      warning() const override
      {
        return Super::warning()
          || (!this->linked && !this->ignore_non_linked);
      }

      /*-----------.
      | Attributes |
      `-----------*/
      FaultySilos silos;
      bool linked;
      bool ignore_non_linked;
      std::string username;
    };

    struct LeftoversResult
      :  public Result
    {
      /*-------------.
      | Construction |
      `-------------*/
      LeftoversResult() = default;
      LeftoversResult(std::string const& name,
                      Result::Reason r = Result::Reason{"should not be there"})
        : Result(name, true, r, true)
      {}

      /*--------------.
      | Serialization |
      `--------------*/
      void
      serialize(elle::serialization::Serializer& s)
      {
        Result::serialize(s);
      }
    };

    /*-------------.
    | Construction |
    `-------------*/
    ConfigurationIntegrityResults(bool only = true)
      : _only(only)
    {}

    /*----------.
    | Interface |
    `----------*/
    bool
    sane() const override
    {
      return this->user.sane()
        && sane_(this->silos)
        && sane_(this->networks)
        ;
      // Leftovers is always sane.
    }

    bool
    warning() const override
    {
      return this->user.warning()
        || warning_(this->silos)
        || warning_(this->networks)
        || warning_(this->leftovers)
      ;
    }

    /*---------.
    | Printing |
    `---------*/
    void
    print(Output& out) const
    {
      if (!this->only())
        section(out, "Configuration integrity");
      this->user.print(out);
      print_(out, "Silos", silos);
      print_(out, "Networks", networks);
      print_(out, "Leftovers", leftovers);
      out << std::endl;
    }

    /*--------------.
    | Serialization |
    `--------------*/
    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("silos", this->silos);
      s.serialize("networks", this->networks);
      s.serialize("leftovers", this->leftovers);
      if (s.out())
      {
        bool sane = this->sane();
        s.serialize("sane", sane);
      }
    }

    /*-----------.
    | Attributes |
    `-----------*/
    ELLE_ATTRIBUTE_R(bool, only);
    UserResult user;
    std::vector<SilosResult> silos;
    std::vector<NetworkResult> networks;
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
                 std::string const &name)
        : BasicResult("Username", true, std::get<1>(validity), !std::get<0>(validity))
        , _user_name(name)
      {}

      UserResult(std::string const& name)
        : UserResult(valid(name), name)
      {}

      /*----------.
      | Interface |
      `----------*/
      std::tuple<bool, Result::Reason>
      valid(std::string const& name) const
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


      /*---------.
      | Printing |
      `---------*/
      void
      _print(Output& out) const override;

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

      /*---------.
      | Printing |
      `---------*/
      void
      _print(Output& out) const override;

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

      /*---------.
      | Printing |
      `---------*/
      bool
      _show(bool) const override
      {
        return !this->environ.empty();
      }
      void
      _print(Output& out) const override;

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

      /*---------.
      | Printing |
      `---------*/
      void
      print(Output& out) const;

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

      /*---------.
      | Printing |
      `---------*/
      void
      _print(Output& out) const override;

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
    print(Output& out) const;

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
      BeyondResult(BasicResult::Reason const& r = {});

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

      /*---------.
      | Printing |
      `---------*/
      void
      _print(Output& out) const override;

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
      print(Output& out) const;

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

      /*---------.
      | Printing |
      `---------*/
      void
      _print(Output& out) const override;

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
          Address(std::string host, uint16_t port)
            : host(host)
            , port(port)
          {}
          Address(elle::serialization::SerializerIn& s)
          {
            this->serialize(s);
          }

          /*---------.
          | Printing |
          `---------*/
          void
          print(std::ostream& out) const override
          {
            out << this->host << ":" << this->port;
          }

          /*--------------.
          | Serialization |
          `--------------*/
          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("host", this->host);
            s.serialize("port", this->port);
          }

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

        /*---------.
        | Printing |
        `---------*/
        void
        print(Output& out) const;

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
      _print(Output& out) const override;

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

    /*---------.
    | Printing |
    `---------*/
    void
    print(Output& out) const;

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
    print(Output& out) const;

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

  void
  SystemSanityResults::UserResult::_print(Output& out) const
  {
    if (this->show(out.verbose) && this->sane())
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

  void
  SystemSanityResults::SpaceLeft::_print(Output& out) const
  {
    if (this->show(out.verbose))
    {
      if (!this->sane() || this->warning())
        out << std::endl << "  - " << "low";
      elle::fprintf(
        out.out, " %s available (~%.1f%%)",
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

  void
  SystemSanityResults::EnvironResult::_print(Output& out) const
  {
    if (this->show(out.verbose))
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

  void
  SystemSanityResults::PermissionResult::print(Output& out) const
  {
    auto result = [](bool value)
      {
        return value ? "OK" : "Bad";
      };

    if (this->show(out.verbose))
      out << " exists: " << result(this->exists)
          << ", readable: " << result(this->read)
          << ", writable: " << result(this->write);
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

  void
  SystemSanityResults::FuseResult::_print(Output& out) const
  {
    if (out.verbose && this->sane() && !this->warning())
      out << " you will be able to mount a filesystem interface through FUSE";
  }

  void
  SystemSanityResults::FuseResult::serialize(elle::serialization::Serializer& s)
  {
    Result::serialize(s);
  }


  SystemSanityResults::SystemSanityResults(bool only)
    : _only(only)
  {}

  void
  SystemSanityResults::print(Output& out) const
  {
    if (!this->only())
      section(out, "System sanity");
    this->user.print(out);
    this->space_left.print(out);
    this->environ.print(out);
    print_(out, "Permissions", this->permissions);
    this->fuse.print(out);
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

  ConnectivityResults::BeyondResult::BeyondResult(Result::Reason const& r)
    : BasicResult(
      elle::sprintf("Connection to %s", infinit::beyond()), !r, r)
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

  void
  ConnectivityResults::InterfaceResults::_print(Output& out) const
  {
    if (this->show(out.verbose))
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
  ConnectivityResults::ProtocolResult::print(Output& out) const
  {
    if (this->show(out.verbose))
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

  void
  ConnectivityResults::NATResult::_print(Output& out) const
  {
    if (this->show(out.verbose) && this->sane())
      out << " OK (" << (this->cone ? "CONE" : "NOT CONE") << ")";
  }

  void
  ConnectivityResults::NATResult::serialize(elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("cone", cone);
  }


  ConnectivityResults::UPnPResult::RedirectionResult::RedirectionResult(
    std::string const& name,
    bool sane,
    Result::Reason const& reason)
    : Result(name, true, reason, !sane)
  {}

  void
  ConnectivityResults::UPnPResult::RedirectionResult::serialize(
    elle::serialization::Serializer& s)
  {
    Result::serialize(s);
    s.serialize("internal", this->internal);
    s.serialize("external", this->external);
  }

  void
  ConnectivityResults::UPnPResult::RedirectionResult::print(Output& out) const
  {
    if (this->show(out.verbose))
    {
      status(out << std::endl << "  ", this->sane(), this->warning())
        << " " << this->name() << ": ";
      if (internal && external)
        out << "local endpoint (" << this->internal
            << ") successfully mapped (to " << this->external << ")";
      if (this->warning() && this->reason)
        out << std::endl
            << "    Reason: " << *this->reason;
    }
  }

  ConnectivityResults::UPnPResult::UPnPResult(bool available)
    : Result("UPnP", available)
    , available(available)
  {}

  void
  ConnectivityResults::UPnPResult::_print(Output& out) const
  {
    if (this->show(out.verbose))
    {
      if (this->external)
        out << " external IP address: " << this->external;
      for (auto const& redirection: redirections)
        redirection.print(out);
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

  void
  ConnectivityResults::print(Output& out) const
  {
    if (!this->only())
      section(out, "Connectivity");
    this->beyond.print(out);
    this->interfaces.print(out);
    this->nat.print(out);
    this->upnp.print(out);
    print_(out, "Protocols", this->protocols);
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

  void
  All::print(Output& out) const
  {
    configuration_integrity.print(out);
    system_sanity.print(out);
    connectivity.print(out);
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

  /// The Infinit related part of the environment.
  Environ
  infinit_related_environ()
  {
    using boost::algorithm::starts_with;
    return elle::os::environ([](auto const& k, auto const& v) {
        return ((starts_with(k, "MEMO_") || starts_with(k, "ELLE_"))
                && v != "reactor.network.UTPSocket:NONE");
      });
  }

  uint16_t
  get_port(boost::optional<uint16_t> const& port)
  {
    namespace random = elle::cryptography::random;
    return port.value_or(random::generate<uint16_t>(10000, 65535));
  }


  void
  _connectivity(infinit::cli::Memo& cli,
                boost::optional<std::string> const& server,
                boost::optional<uint16_t> upnp_tcp_port,
                boost::optional<uint16_t> upnp_utp_port,
                ConnectivityResults& results)
  {
    ELLE_TRACE("contact beyond")
    {
      try
      {
        elle::reactor::http::Request r(infinit::beyond(),
                                 elle::reactor::http::Method::GET, {10_sec});
        elle::reactor::wait(r);
        if (r.status() != elle::reactor::http::StatusCode::OK)
          results.beyond = {elle::sprintf("%s", r.status())};
      }
      catch (elle::reactor::http::RequestError const&)
      {
        results.beyond = {
          elle::sprintf("Couldn't connect to %s", infinit::beyond())
        };
      }
      catch (elle::Error const&)
      {
        results.beyond = {elle::exception_string()};
      }
    }
    auto const public_ips = []
      {
        ELLE_TRACE("list interfaces")
        {
          auto res = std::vector<std::string>{};
          auto interfaces = elle::network::Interface::get_map(
            elle::network::Interface::Filter::no_loopback);
          for (auto const& i: interfaces)
          {
            auto add = [&res](auto const& addrs){
              res.insert(res.end(), addrs.begin(), addrs.end());
            };
            add(i.second.ipv4_address);
            add(i.second.ipv6_address);
          }
          return res;
        }
      }();
    results.interfaces = {public_ips};
    using ConnectivityFunction
      = std::function<elle::reactor::connectivity::Result
                      (std::string const& host, uint16_t port)>;
    uint16_t port = 5456;
    auto run = [&] (std::string const& name,
                    ConnectivityFunction const& function,
                    int deltaport = 0)
      {
        static const elle::reactor::Duration timeout = 3_sec;
        ELLE_TRACE("connect using %s to %s:%s", name, *server, port + deltaport);
        try
        {
          elle::reactor::TimeoutGuard guard(timeout);
          auto const address = function(*server, port + deltaport);
          bool external = !elle::contains(public_ips, address.host);
          store(results.protocols, name, *server, address.local_port,
                address.remote_port, !external);
        }
        catch (elle::reactor::Terminate const&)
        {
          throw;
        }
        catch (elle::reactor::network::ResolutionError const& error)
        {
          store(results.protocols, name,
                elle::sprintf("Couldn't connect to %s", error.host()));
        }
        catch (elle::reactor::Timeout const&)
        {
          store(results.protocols, name,
                elle::sprintf("Couldn't connect after 3 seconds"));
        }
        catch (...)
        {
          store(results.protocols, name, elle::exception_string());
        }
      };
    run("TCP", elle::reactor::connectivity::tcp);
    run("UDP", elle::reactor::connectivity::udp);
    run("UTP",
        [](auto const& host, auto const& port)
        { return elle::reactor::connectivity::utp(host, port, 0); },
        1);
    run("UTP (XOR)",
        [](auto const& host, auto const& port)
        { return elle::reactor::connectivity::utp(host, port, 0xFF); },
        2);
    run("RDV UTP",
        [](auto const& host, auto const& port)
        { return elle::reactor::connectivity::rdv_utp(host, port, 0); },
        1);
    run("RDV UTP (XOR)",
        [](auto const& host, auto const& port)
        { return elle::reactor::connectivity::rdv_utp(host, port, 0xFF); },
        2);
    ELLE_TRACE("NAT")
    {
      try
      {
        auto nat = elle::reactor::connectivity::nat(*server, port);
        // Super ugly.
        auto cone = nat.find("NOT_CONE") == std::string::npos &&
          nat.find("CONE") != std::string::npos;
        results.nat = {cone};
      }
      catch (elle::reactor::Terminate const&)
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
      auto upnp = elle::reactor::network::UPNP::make();
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
        auto redirect = [&] (elle::reactor::network::Protocol protocol,
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
            catch (elle::reactor::Terminate const&)
            {
              throw;
            }
            catch (...)
            {
              res = {type, false, elle::exception_string()};
            }
          };
        redirect(elle::reactor::network::Protocol::tcp, get_port(upnp_tcp_port));
        redirect(elle::reactor::network::Protocol::utp, get_port(upnp_utp_port));
      }
      catch (elle::reactor::Terminate const&)
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
      auto it = bfs::recursive_directory_iterator(path, code);
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
      struct NoOp : elle::reactor::filesystem::Operations
      {
        std::shared_ptr<elle::reactor::filesystem::Path>
        path(std::string const& path) override
        {
          return nullptr;
        }
      };

      elle::reactor::filesystem::FileSystem f(std::make_unique<NoOp>(), false);
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
  _system_sanity(infinit::cli::Memo& cli,
                 SystemSanityResults& result)
  {
    result.fuse = {fuse(false)};
    ELLE_TRACE("user name")
      result.user = {cli.as().value_or(cli.default_user_name())};
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

  namespace details
  {
    template <typename Container>
    auto
    parse(Container&& container,
           std::function<std::string (typename Container::value_type const& v)> f)
    {
      auto res = std::map<std::string,
                          std::pair<typename Container::value_type, bool>>{};
      for (auto& item: container)
      {
        auto name = f(item);
        res.emplace(std::piecewise_construct,
                    std::forward_as_tuple(name),
                    std::forward_as_tuple(std::move(item), false));
      }
      return res;
    }
  }

  template <typename Container>
  auto
  parse(
    Container container,
    decltype(std::declval<typename Container::value_type>()->name)* = nullptr)
  {
    return details::parse(std::move(container),
                          [] (typename Container::value_type const& v)
                          {
                            return v->name;
                          });
  }

  template <typename Container>
  auto
  parse(
    Container container,
    decltype(std::declval<typename Container::value_type>().name)* = nullptr)
  {
    return details::parse(std::move(container),
                          [] (typename Container::value_type const& v)
                          {
                            return v.name;
                          });
  }

  template <typename ExpectedType>
  void
  load(infinit::Infinit& ifnt,
       bfs::path const& path, std::string const& type)
  {
    bfs::ifstream i;
    ifnt._open_read(i, path, type, path.filename().string());
    infinit::Infinit::load<ExpectedType>(i);
  }

  void
  _configuration_integrity(infinit::cli::Memo& cli,
                           bool ignore_non_linked,
                           ConfigurationIntegrityResults& results)
  {
    auto& ifnt = cli.infinit();
    auto const& username = cli.as().value_or(cli.default_user_name());

    auto users = parse(ifnt.users_get());
    auto aws_credentials = ifnt.credentials_aws();
    auto gcs_credentials = ifnt.credentials_gcs();
    auto silos = parse(ifnt.silos_get());

    auto owner = [&]() -> boost::optional<infinit::User>
    {
      try
      {
        auto res = cli.as_user();
        if (res.private_key)
          results.user = {username, true};
        else
          results.user = {
            username,
            false,
            elle::sprintf("user \"%s\" has no private key", username)
          };
        return res;
      }
      // XXX: Catch a specific error.
      catch (...)
      {
        results.user = {
          username,
          false,
          elle::sprintf("user \"%s\" is not a local Infinit user", username)
        };
        return {};
      }
    }();

    using namespace infinit::silo;
    auto networks = parse(ifnt.networks_get(owner));

    ELLE_TRACE("verify silos")
      for (auto& elem: silos)
      {
        auto& silo = elem.second.first;
        // We really need to update this field, as when we verify
        // volumes, we will skip those with broken silos.  Likewise
        // for the other items.
        auto& status = elem.second.second;
#define COMPARE(field) (credentials->field == s3config->credentials.field())
        MEMO_ENTREPRISE(
        if (auto s3config = dynamic_cast<S3SiloConfig const*>(
              silo.get()))
        {
          status = any_of(aws_credentials,
              [&s3config] (auto const& credentials)
              {
                return COMPARE(access_key_id) && COMPARE(secret_access_key);
              });
        if (status)
          store(results.silos, silo->name, status, "S3");
        else
          store(results.silos, silo->name, status, "S3",
                std::string("credentials are missing"));
        })
#undef COMPARE
        if (auto fsconfig
            = dynamic_cast<FilesystemSiloConfig const*>(silo.get()))
        {
          auto perms = has_permission(fsconfig->path);
          status = perms.first;
          store(results.silos, silo->name, status, "filesystem",
                elle::sprintf("\"%s\" %s", fsconfig->path, perms.second));
        }
        MEMO_ENTREPRISE(
        if (auto gcsconfig = dynamic_cast<GCSConfig const*>(silo.get()))
        {
          status = any_of(gcs_credentials,
              [&gcsconfig] (auto const& credentials)
              {
                return credentials->refresh_token == gcsconfig->refresh_token;
              });
          if (status)
            store(results.silos, silo->name, status, "GCS");
          else
            store(results.silos, silo->name, status, "GCS",
                  std::string("credentials are missing"));
        })
      }

    ELLE_TRACE("verify networks")
      for (auto& elem: networks)
      {
        auto const& network = elem.second.first;
        auto& status = elem.second.second;
        auto silos_names = std::vector<std::string>{};
        bool linked = network.model != nullptr;
        if (linked && network.model->storage)
        {
          if (auto strip = dynamic_cast<StripSiloConfig*>(
                network.model->storage.get()))
            for (auto const& s: strip->storage)
              silos_names.emplace_back(s->name);
          else
            silos_names.emplace_back(network.model->storage->name);
        }
        auto faulty = ConfigurationIntegrityResults::NetworkResult
          ::FaultySilos::value_type{};
        status = silos_names.empty()
          || all_of(silos_names, [&] (std::string const& name) -> bool {
              auto it = boost::find_if(results.silos,
                                       [name] (auto const& t)
                                       {
                                         return t.name() == name;
                                       });
              auto res = it != results.silos.end() && !it->show(false);
              if (it != results.silos.end())
                faulty.emplace_back(*it);
              else
                faulty.emplace_back(name, false, "unknown",
                                    std::string{"missing"});
              return res;
            });
        if (status)
          store(results.networks, network.name, status, ignore_non_linked,
                username,
                boost::none, boost::none, linked);
        else
          store(results.networks, network.name, status, ignore_non_linked,
                username,
                faulty, boost::none, linked);
      }

    auto& leftovers = results.leftovers;
    auto is_parent_of = [](bfs::path const& dir, bfs::path const& file) -> bool
      {
        return boost::starts_with(file.parent_path(), dir);
      };
    auto do_not_recurse_deeper = [] (bfs::recursive_directory_iterator& it,
                                     bfs::path const& path)
    {
      if (it->path() == path)
        it.no_push();
    };
    for (auto it = bfs::recursive_directory_iterator(infinit::xdg_data_home());
         it != bfs::recursive_directory_iterator();
         ++it)
    {
      // Do not explore recursively the following paths.
      do_not_recurse_deeper(it, infinit::xdg_data_home() / "blocks");
      do_not_recurse_deeper(it, infinit::xdg_data_home() / "ui");
      auto p = *it;
      if (is_visible_file(p))
      {
        try
        {
          // Share.
          if (is_parent_of(ifnt._network_descriptors_path(), p.path()))
            load<infinit::NetworkDescriptor>(ifnt, p.path(), "network_descriptor");
          else if (is_parent_of(ifnt._networks_path(), p.path()))
            load<infinit::Network>(ifnt, p.path(), "network");
          else if (is_parent_of(ifnt._passports_path(), p.path()))
            load<infinit::Passport>(ifnt, p.path(), "passport");
          else if (is_parent_of(ifnt._users_path(), p.path()))
            load<infinit::User>(ifnt, p.path(), "users");
          else if (is_parent_of(ifnt._silos_path(), p.path()))
            load<std::unique_ptr<infinit::silo::SiloConfig>>(ifnt, p.path(), "silo");
          else if (is_parent_of(deprecated::storages_path(), p.path()))
            load<std::unique_ptr<infinit::silo::SiloConfig>>(ifnt, p.path(), "storage");
          else if (is_parent_of(ifnt._credentials_path(), p.path()))
            load<std::unique_ptr<infinit::Credentials>>(ifnt, p.path(), "credentials");
          else
            store(leftovers, p.path().string());
        }
        catch (...)
        {
          store(leftovers, p.path().string(), elle::exception_string());
        }
        elle::reactor::yield();
      }
    }
    for (auto const& p: bfs::recursive_directory_iterator(infinit::xdg_cache_home()))
    {
      if (is_visible_file(p))
      {
        try
        {
          store(leftovers, p.path().string());
        }
        catch (...)
        {
          store(leftovers, p.path().string(), elle::exception_string());
        }
      }
      elle::reactor::yield();
    }
    for (auto it = bfs::recursive_directory_iterator(infinit::xdg_state_home());
         it != bfs::recursive_directory_iterator();
         ++it)
    {
      do_not_recurse_deeper(it, infinit::xdg_state_home() / "cache");
      auto p = *it;
      if (is_visible_file(p))
      {
        try
        {
          if (p.path() == infinit::xdg_state_home() / "critical.log")
          {}
          else
            store(leftovers, p.path().string());
        }
        catch (...)
        {
          store(leftovers, p.path().string(), elle::exception_string());
        }
      }
      elle::reactor::yield();
    }
  }

  void
  _report_error(infinit::cli::Memo& cli,
                Output& out, bool sane, bool warning = false)
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
  _output(infinit::cli::Memo& cli,
          Output& out,
          Report const& results)
  {
    if (cli.script())
      infinit::Infinit::save(out.out, results, false);
    else
      results.print(out);
  }
}
