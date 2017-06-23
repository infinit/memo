#pragma once

#include <elle/serialization/json.hh>
#include <elle/serialization/binary.hh>
#include <elle/os/environ.hh>
#include <elle/log.hh>
#include <elle/bench.hh>

#include <elle/cryptography/SecretKey.hh>

#include <elle/reactor/network/Error.hh>
#include <elle/reactor/network/socket.hh>
#include <elle/reactor/Scope.hh>
#include <elle/reactor/semaphore.hh>
#include <elle/reactor/storage.hh>

#include <elle/protocol/ChanneledStream.hh>
#include <elle/protocol/Serializer.hh>

#include <infinit/model/doughnut/Passport.hh>

namespace infinit
{
  /// Exception representing the attempt of invoking an unknown RPC.
  class UnknownRPC
    : public elle::Error
  {
  public:
    /// Construct an UnknownRPC with the given name.
    ///
    /// @param name The name of the RPC.
    UnknownRPC(std::string name)
      : elle::Error(elle::sprintf("unknown RPC: %s", name))
      , _name(std::move(name))
    {}

    /// Deserialize an UnknownRPC.
    ///
    /// @param s The serializer to deserialize the UnknownRPC from.
    UnknownRPC(elle::serialization::SerializerIn& s)
      : elle::Error(s)
      , _name()
    {
      this->_serialize(s);
    }

    /// Serialize or deserialize an UnknownRPC.
    ///
    /// @param The serializer to serialize to or deserialize from.
    /// @param version The serialization version.
    void
    serialize(elle::serialization::Serializer& s,
              elle::Version const& version) override
    {
      this->elle::Error::serialize(s, version);
      this->_serialize(s);
    }

    /// Serialize or deserialize an UnknownRPC.
    ///
    /// @param The serializer to serialize to or deserialize from.
    void
    _serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->_name);
    }

    ELLE_ATTRIBUTE_R(std::string, name);
  };

  /*-------.
  | Server |
  `-------*/

  /// Handler for a RPC.
  ///
  /// XXX
  class RPCHandler
  {
  public:
    /// Construct
    RPCHandler(std::string name)
      : _name(std::move(name))
    {}
    virtual
    ~RPCHandler() = default;
    virtual
    void
    handle(elle::serialization::SerializerIn& input,
           elle::serialization::SerializerOut& output) = 0;
    ELLE_ATTRIBUTE_R(std::string, name);
  };

  std::ostream&
  operator <<(std::ostream& output, RPCHandler const& rpc);

  /*-------.
  | List.  |
  `-------*/

  template <int I, typename ... Args>
  struct ListImpl;

  template <typename ... Args>
  using List = ListImpl<sizeof...(Args), Args...>;

  template <typename ... Args>
  struct ListImpl<0, Args...>
  {
    static constexpr bool empty = true;
  };

  template <int I, typename Head_, typename ... Tail_>
  struct ListImpl<I, Head_, Tail_ ...>
  {
    using Head = Head_;
    using Tail = List<Tail_...>;
    static constexpr bool empty = false;
  };

  /*---------------------.
  | ConcreteRPCHandler.  |
  `---------------------*/

  template <typename R, typename ... Args>
  class ConcreteRPCHandler
    : public RPCHandler
  {
  public:
    using Self = ConcreteRPCHandler;
    using Function = std::function<R (Args...)>;
    ConcreteRPCHandler(std::string name, Function const& fun)
      : RPCHandler(std::move(name))
      , _function(fun)
    {}

    ELLE_ATTRIBUTE_R(Function, function);

    void
    handle(elle::serialization::SerializerIn& input,
           elle::serialization::SerializerOut& output) override
    {
      this->_handle<List<Args...>>(0, input, output);
    }

  private:
    template <typename Remaining, typename ... Parsed>
    std::enable_if_t<
      !Remaining::empty &&
      !elle::serialization::virtually<
        std::remove_reference_t<typename Remaining::Head>>()>
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      using Head = std::remove_cv_reference_t<typename Remaining::Head>;
      auto arg = input.deserialize<Head>(elle::sprintf("arg%s", n));
      ELLE_DUMP("got argument: %s", arg);
      this->_handle<typename Remaining::Tail,
                    Parsed..., typename Remaining::Head>(
        n + 1, input, output, std::forward<Parsed>(parsed)..., std::move(arg));
    }

    template <typename Remaining, typename ... Parsed>
    std::enable_if_t<
      !Remaining::empty &&
      elle::serialization::virtually<
        std::remove_reference_t<typename Remaining::Head>>()>
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      using Head = std::remove_cv_reference_t<typename Remaining::Head>;
      auto arg =
        input.deserialize<std::unique_ptr<Head>>(elle::sprintf("arg%s", n));
      ELLE_DUMP("got argument: %s", *arg);
      this->_handle<typename Remaining::Tail,
                    Parsed..., typename Remaining::Head>(
        n + 1, input, output, std::forward<Parsed>(parsed)..., std::move(*arg));
    }

    template <typename Remaining, typename ... Parsed>
    std::enable_if_t<Remaining::empty && std::is_void<R>::value>
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      try
      {
        ELLE_TRACE_SCOPE("%s: run", *this);
        this->_function(std::forward<Parsed>(parsed)...);
        ELLE_TRACE("%s: success", *this);
        output.serialize("success", true);
      }
      catch (elle::Error& e)
      {
        ELLE_TRACE_SCOPE("{}: exception escaped: {}",
                         *this, elle::exception_string());
        ELLE_DUMP("{}", e.backtrace());
        output.serialize("success", false);
        output.serialize("exception", std::current_exception());
      }
      catch (...)
      {
        ELLE_TRACE("%s: exception escaped: %s",
                   *this, elle::exception_string());
        output.serialize("success", false);
      }
    }

    template <typename Remaining, typename ... Parsed>
    std::enable_if_t<Remaining::empty && !std::is_void<R>::value>
    _handle(int n,
            elle::serialization::SerializerIn& input,
            elle::serialization::SerializerOut& output,
            Parsed&& ... parsed)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      try
      {
        ELLE_TRACE_SCOPE("%s: run", *this);
        R res = this->_function(std::forward<Parsed>(parsed)...);
        ELLE_TRACE("%s: success: %s", *this, res);
        output.serialize("success", true);
        output.serialize("value", res);
      }
      catch (elle::Error& e)
      {
        ELLE_TRACE("%s: exception escaped: %s",
                   *this, elle::exception_string());
        output.serialize("success", false);
        output.serialize("exception", std::current_exception());
      }
      catch (...)
      {
        ELLE_TRACE("%s: exception escaped: %s",
                   *this, elle::exception_string());
        output.serialize("success", false);
      }
    }
  };

  /// Answer to RPCs.
  class RPCServer
  {
  public:
    using Passport = infinit::model::doughnut::Passport;

    RPCServer();
    RPCServer(elle::Version version);

    ~RPCServer()
    {
      this->_destroying();
    }

    /// Add a RPC to the server.
    ///
    /// @tparam R The return type of the RPC.
    /// @tparam Args The arguments of the RPC.
    /// @param name The name of the RPC.
    /// @param f The method to call.
    template <typename R, typename ... Args>
    void
    add(std::string const& name, std::function<R (Args...)> f)
    {
      this->_rpcs[name] =
        std::make_unique<ConcreteRPCHandler<R, Args...>>(name, f);
    }

    /// Add an RPC to the server.
    ///
    /// @tparam Fun The signature of the RPC.
    /// @param name The name of the RPC.
    /// @param fun The method to call.
    template <typename Fun>
    void
    add(std::string const& name, Fun fun)
    {
      add(name, std::function<std::get_signature<Fun>>(fun));
    }

    /// An umbrella to cover common exceptions related to RPCs, such as
    /// closed sockets, etc.
    ///
    /// @tparam F The type of the function to wrap.
    /// @tparam Args The types of the arguments of the function.
    /// @param f The function to wrap.
    /// @param args The arguments.
    template <typename F, typename ... Args>
    static
    void
    umbrella(F const& f, Args&& ... args)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      try
      {
        f(std::forward<Args>(args)...);
      }
      catch (elle::protocol::Serializer::EOF const&)
      {}
      catch (elle::reactor::network::ConnectionClosed const& e)
      {}
      catch (elle::reactor::network::SocketClosed const& e)
      {
        ELLE_TRACE("unexpected SocketClosed: %s", e.backtrace());
      }
    }

    /// Start serving RPCs on the given input stream, wrapped on the umbrella
    /// method.
    ///
    /// @param s The stream.
    void
    serve(std::iostream& s)
    {
      umbrella(
        [&]
        {
          elle::protocol::Serializer serializer(
            s, elle_serialization_version(this->_version), false);
          this->_serve(serializer);
        });
    }

    /// Start serving RPCs on the given serializer, wrapped on the umbrella
    /// method.
    ///
    /// @param serializer The serializer.
    void
    serve(elle::protocol::Serializer& serializer)
    {
      umbrella([&] { this->_serve(serializer); });
    }

    /// Start serving RPCs on the given serializer after building a
    /// ChanneledStream, wrapped on the umbrella method.
    ///
    /// @param serializer The serializer.
    void
    _serve(elle::protocol::Serializer& serializer)
    {
      auto&& chans = elle::protocol::ChanneledStream{serializer};
      this->serve(chans);
    }

    /// Start serving RPCs on the given ChanneledStream, wrapped on the umbrella
    /// method.
    ///
    /// @param channels The channeled stream.
    void
    serve(elle::protocol::ChanneledStream& channels)
    {
      umbrella([&] { this->_serve(channels); });
    }

    /// Start serving RPCs on the given ChanneledStream.
    void
    _serve(elle::protocol::ChanneledStream& channels)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      auto nthreads = elle::os::getenv("INFINIT_RPC_SERVE_THREADS", 1);
      elle::reactor::Semaphore sem(nthreads? nthreads+1 : 1000000000);
      elle::With<elle::reactor::Scope>() << [&] (elle::reactor::Scope& s)
      {
        while (true)
        {
          elle::reactor::Lock l(sem);
          auto channel = channels.accept();
          if (nthreads == 1)
            this->_serve(channel);
          else
          {
            auto schannel = std::make_shared<decltype(channel)>(
              std::move(channel));
            s.run_background(elle::sprintf("serve %s", *schannel),
              [&, schannel]
              {
                elle::reactor::Lock l(sem);
                this->_serve(*schannel);
              });
          }
        }
      };
    }

    /// Start serving RPCs on a specific channel.
    void
    _serve(elle::protocol::Channel& channel)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      auto request = channel.read();
      ELLE_TRACE_SCOPE("%s: process RPC", this);
      bool had_key = !!_key;
      if (had_key)
      {
        ELLE_DEBUG_SCOPE("decipher RPC");
        try
        {
          static auto bench = elle::Bench("bench.rpcserve.decipher",
                                          std::chrono::seconds(10000));
          auto bs = elle::Bench::BenchScope(bench);
          if (request.size() > 262144)
          {
            auto& key = this->_key.get();
            elle::With<elle::reactor::Thread::NonInterruptible>() << [&] {
              elle::reactor::background([&] {
                  request = key.decipher(request);
              });
            };
          }
          else
            request = this->_key->decipher(request);
        }
        catch(std::exception const& e)
        {
          ELLE_ERR("decypher request: %s", e.what());
          throw;
        }
      }
      elle::IOStream ins(request.istreambuf());
      auto const versions = elle::serialization::get_serialization_versions
        <infinit::serialization_tag>(this->_version);
      elle::serialization::binary::SerializerIn input(ins, versions, false);
      input.set_context(this->_context);
      std::string name;
      input.serialize("procedure", name);
      elle::Buffer response;
      {
        auto it = this->_rpcs.find(name);
        elle::IOStream outs(response.ostreambuf());
        auto output = elle::serialization::binary::SerializerOut(
          outs, versions, false);
        if (it == this->_rpcs.end())
        {
          ELLE_WARN("%s: unknown RPC: %s", *this, name);
          output.serialize("success", false);
          output.serialize(
            "exception", std::make_exception_ptr<UnknownRPC>(name));
        }
        else
        {
          ELLE_TRACE_SCOPE("%s: run procedure %s", *this, name);
          {
            output.set_context(this->_context);
            try
            {
              it->second->handle(input, output);
            }
            catch (elle::Error const& e)
            {
              ELLE_WARN("%s: deserialization error: %s",
                        *this, e);
              throw;
            }
          }
        }
      }
      if (had_key)
      {
        static auto bench =
          elle::Bench("bench.rpcserve.encipher", std::chrono::seconds(10000));
        auto bs = elle::Bench::BenchScope(bench);
        if (response.size() >= 262144)
        {
          auto& key = this->_key.get();
          elle::With<elle::reactor::Thread::NonInterruptible>() << [&] {
            elle::reactor::background([&] {
                response = key.encipher(
	          elle::ConstWeakBuffer(response.contents(),
	            response.size()));
              });
          };
        }
        else
          response = _key->encipher(
            elle::ConstWeakBuffer(response.contents(), response.size()));
      }
      channel.write(response);
    }

    /// Upsert a value of type `T` to the context.
    ///
    /// @tparam T The type of the value to add.
    /// @param value The value to add.
    template <typename T>
    void
    set_context(T value)
    {
      this->_context.set<T>(value);
    }

    std::unordered_map<std::string, std::unique_ptr<RPCHandler>> _rpcs;
    elle::serialization::Context _context;
    boost::optional<elle::cryptography::SecretKey> _key;
    boost::signals2::signal<void()> _destroying;
    ELLE_ATTRIBUTE(elle::Version, version);
  };

  /*-------.
  | Client |
  `-------*/

  /// Base class of RPCs.
  ///
  ///
  class BaseRPC
  {
  public:
    using Passport = infinit::model::doughnut::Passport;
    /// Construct a new RPC.
    ///
    /// @param name The name of the method.
    /// @param channels The channels to perform the RPC.
    /// @param version The version used.
    /// @param key An optional key, validated by the other side.
    BaseRPC(std::string name,
            elle::protocol::ChanneledStream* channels,
            elle::Version const& version,
            boost::optional<elle::cryptography::SecretKey> key = {})
      : _name(std::move(name))
      , _channels(channels)
      , _key(std::move(key))
      , _version(version)
    {}

    /// Return the credentials, if applicable.
    ///
    /// @returns A buffer, empty or containing a secret key.
    elle::Buffer
    credentials()
    {
      if (this->_key)
        return this->_key->password();
      else
        return {};
    }

    /// Upsert a value of type `T` to the context.
    ///
    /// @tparam T The type of the value to add.
    /// @param value The value to add.
    template <typename T>
    void
    set_context(T value)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      ELLE_DUMP("%s: set context for %s: %s",
                *this, elle::type_info<T>(), value);
      this->_context.template set<T>(value);
    }

    elle::serialization::Context _context;
    ELLE_ATTRIBUTE_R(std::string, name);
    ELLE_ATTRIBUTE_R(elle::protocol::ChanneledStream*, channels, protected);
    ELLE_ATTRIBUTE_RX(
      boost::optional<elle::cryptography::SecretKey>, key, protected);
    ELLE_ATTRIBUTE_R(elle::Version, version, protected);
  };

  template <typename Proto>
  class RPC;

  /// A remote procedure call.
  ///
  /// @tparam R The return type of the RPC.
  /// @tparam Args The types of the arguments of the RPC.
  template <typename R, typename ... Args>
  class RPC<R (Args...)>
    : public BaseRPC
  {
  public:
    using Self = RPC;
    /// Construct an RPC.
    ///
    /// @see BaseRPC::BaseRPC.
    RPC(std::string name,
        elle::protocol::ChanneledStream* channels,
        elle::Version const& version,
        boost::optional<elle::cryptography::SecretKey> key = {})
      : BaseRPC(std::move(name), channels, version, std::move(key))
    {}

    /// Construct an RPC.
    ///
    /// @see BaseRPC::BaseRPC.
    RPC(std::string name,
        elle::protocol::ChanneledStream& channels,
        elle::Version const& version,
        boost::optional<elle::cryptography::SecretKey> key = {})
      : Self{name, &channels, version, key}
    {}

    /// Construct an RPC.
    ///
    /// @see BaseRPC::BaseRPC, except for the argument credentials.
    ///
    /// @param credentials A pointer to buffer, that could contain credential.
    RPC(std::string name,
        elle::protocol::ChanneledStream* channels,
        elle::Version const& version,
        elle::Buffer* credentials)
      : Self{
          std::move(name),
          channels,
          version,
          credentials && !credentials->empty() ?
            boost::optional<elle::cryptography::SecretKey>(elle::Buffer(*credentials)) :
            boost::optional<elle::cryptography::SecretKey>()}
    {}

    /// Call the RPC.
    ///
    /// @param args The arguments of the RPC.
    /// @return The response, as an instance of type R.
    R
    operator ()(Args const& ... args);
    using result_type = R;
  };

  template <typename T>
  struct RPCCall;

  template <typename R, typename ... Args>
  struct RPCCall<R (Args...)>
  {
    template <typename Head, typename ... Tail>
    static
    std::enable_if_t<
      elle::serialization::virtually<std::remove_cv_reference_t<Head>>()>
    call_arguments(int n,
                   elle::serialization::SerializerOut& output,
                   Head&& head,
                   Tail&& ... tail)
    {
      using RawHead = std::remove_cv_reference_t<Head>;
      RawHead* ptr = const_cast<RawHead*>(&head);
      output.serialize(elle::sprintf("arg%s", n), ptr);
      call_arguments(n + 1, output, std::forward<Tail>(tail)...);
    }

    template <typename Head, typename ... Tail>
    static
    std::enable_if_t<
      !elle::serialization::virtually<std::remove_reference_t<Head>>()>
    call_arguments(int n,
                   elle::serialization::SerializerOut& output,
                   Head&& head,
                   Tail&& ... tail)
    {
      output.serialize(elle::sprintf("arg%s", n), head);
      call_arguments(n + 1, output, std::forward<Tail>(tail)...);
    }

    static
    void
    call_arguments(int, elle::serialization::SerializerOut&)
    {}

    template <typename Res>
    static
    std::enable_if_t<std::is_void<Res>::value>
    get_result(elle::serialization::SerializerIn&)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      ELLE_TRACE_SCOPE("get result");
      ELLE_DUMP("result: void");
    }

    template <typename Res>
    static
    std::enable_if_t<!std::is_void<Res>::value, R>
    get_result(elle::serialization::SerializerIn& input)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      ELLE_TRACE_SCOPE("get result");
      auto res = input.deserialize<R>("value");
      ELLE_DUMP("result: %s", res);
      return res;
    }

    static
    R
    _call(elle::Version const& version,
          RPC<R (Args...)>& self,
          Args const&... args)
    {
      ELLE_LOG_COMPONENT("infinit.RPC");
      ELLE_TRACE_SCOPE("%s: call", self);
      auto versions = elle::serialization::get_serialization_versions
        <infinit::serialization_tag>(version);
      auto channel = elle::protocol::Channel{*ELLE_ENFORCE(self.channels())};
      {
        elle::Buffer call;
        elle::IOStream outs(call.ostreambuf());

        ELLE_DEBUG("build request")
        {
          auto output = elle::serialization::binary::SerializerOut(outs, versions, false);
          output.set_context(self._context);
          output.serialize("procedure", self.name());
          call_arguments(0, output, args...);
        }
        outs.flush();
        if (self.key())
        {
          static auto bench =
            elle::Bench("bench.rpcclient.encipher", std::chrono::seconds(10000));
          auto bs = elle::Bench::BenchScope(bench);
          // FIXME: scheduler::run?
          ELLE_DEBUG("encipher request")
            if (call.size() > 262144)
            {
              elle::With<elle::reactor::Thread::NonInterruptible>() << [&] {
                elle::reactor::background([&] {
                    call = self.key()->encipher(
                      elle::ConstWeakBuffer(call.contents(), call.size()));
                  });
              };
            }
            else
              call = self.key()->encipher(
                elle::ConstWeakBuffer(call.contents(), call.size()));
        }
        ELLE_DEBUG("send request")
          channel.write(call);
      }
      ELLE_DEBUG("read response request")
      {
        auto response = channel.read();
        if (self.key())
        {
          static auto bench
            = elle::Bench("bench.rpcclient.decipher", std::chrono::seconds(10000));
          auto bs = elle::Bench::BenchScope(bench);
          if (response.size() > 262144)
          {
            elle::With<elle::reactor::Thread::NonInterruptible>() << [&] {
              elle::reactor::background([&] {
                  response = self.key()->decipher(
                    elle::ConstWeakBuffer(response.contents(), response.size()));
              });
            };
          }
          else
            response = self.key()->decipher(
              elle::ConstWeakBuffer(response.contents(), response.size()));
        }
        auto ins = elle::IOStream(response.istreambuf());
        auto input
          = elle::serialization::binary::SerializerIn(ins, versions, false);
        input.set_context(self._context);
        if (input.deserialize<bool>("success"))
          return get_result<R>(input);
        else
        {
          ELLE_TRACE_SCOPE("call failed, get exception");
          auto e = input.deserialize<std::exception_ptr>("exception");
          std::rethrow_exception(e);
        }
      }
    }
  };

  std::ostream&
  operator <<(std::ostream& o, BaseRPC const& rpc);
}

#include <infinit/RPC.hxx>
