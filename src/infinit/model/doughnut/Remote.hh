#ifndef INFINIT_MODEL_DOUGHNUT_REMOTE_HH
# define INFINIT_MODEL_DOUGHNUT_REMOTE_HH

# include <elle/utils.hh>

# include <reactor/network/tcp-socket.hh>
# include <reactor/network/utp-socket.hh>
# include <reactor/thread.hh>

# include <protocol/Serializer.hh>
# include <protocol/ChanneledStream.hh>

# include <infinit/model/doughnut/fwd.hh>
# include <infinit/model/doughnut/Doughnut.hh>
# include <infinit/model/doughnut/Peer.hh>
# include <infinit/model/doughnut/protocol.hh>

# include <infinit/RPC.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      template<typename F>
      class RemoteRPC;

      class Remote
        : virtual public Peer
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef Remote Self;
        typedef Peer Super;
        /// Challenge, token
        using Challenge = std::pair<elle::Buffer, elle::Buffer>;
        struct Auth
        {
          Auth(Address id,
               Challenge challenge,
               Passport passport);
          Address id;
          Challenge challenge;
          Passport passport;
          using Model = das::Model<
            Auth,
            decltype(elle::meta::list(
                       symbols::id,
                       symbols::challenge,
                       symbols::passport))>;
        };
        template<typename F>
        friend
        class RemoteRPC;
        friend
        class Dock;

      /*-------------.
      | Construction |
      `-------------*/
      protected:
        Remote(Doughnut& doughnut,
               std::shared_ptr<Dock::Connection> connection);
      public:
        virtual
        ~Remote();
      protected:
        void
        _cleanup() override;
        void
        connection(std::shared_ptr<Dock::Connection> connection);
        ELLE_ATTRIBUTE_R(std::shared_ptr<Dock::Connection>, connection);
        ELLE_attribute_r(Endpoints, endpoints);
        ELLE_attribute_r(elle::Buffer, credentials);
        ELLE_ATTRIBUTE(Dock::PeerCache::iterator, cache_iterator);

      /*-----------.
      | Connection |
      `-----------*/
      public:
        virtual
        void
        connect(elle::DurationOpt timeout = elle::DurationOpt());
        void
        reconnect();
        void
        disconnect();
      private:
        ELLE_ATTRIBUTE(reactor::Barrier, connected);
        ELLE_ATTRIBUTE(
          std::chrono::system_clock::time_point, connecting_since);
        ELLE_ATTRIBUTE(std::exception_ptr, disconnected_exception);
        ELLE_ATTRIBUTE(
          std::vector<boost::signals2::scoped_connection>, connections);

      /*-----------.
      | Networking |
      `-----------*/
      public:
        template<typename F>
        RemoteRPC<F>
        make_rpc(std::string const& name);
        template<typename R>
        R
        safe_perform(std::string const& name, std::function<R()> op);
      private:
        ELLE_ATTRIBUTE(Protocol, protocol);
        ELLE_ATTRIBUTE_R(EndpointsRefetcher, refetch_endpoints);

      /*-------.
      | Blocks |
      `-------*/
      public:
        void
        store(blocks::Block const& block, StoreMode mode) override;
        void
        remove(Address address, blocks::RemoveSignature rs) override;
      protected:
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
              boost::optional<int> local_version) const override;

      /*-----.
      | Keys |
      `-----*/
      protected:
        std::vector<cryptography::rsa::PublicKey>
        _resolve_keys(std::vector<int> ids) override;
        std::unordered_map<int, cryptography::rsa::PublicKey>
        _resolve_all_keys() override;
        ELLE_attribute_rx(KeyCache, key_hash_cache);
      };

      template <typename F>
      class RemoteRPC
        : public RPC<F>
      {
      public:
        using Super = RPC<F>;
        RemoteRPC(std::string name, Remote* remote);
        template<typename ...Args>
        typename Super::result_type
        operator()(Args const& ... args);
        Remote* _remote;
      };
    }
  }
}

DAS_SERIALIZE(infinit::model::doughnut::Remote::Auth);

#include <infinit/model/doughnut/Remote.hxx>

#endif
