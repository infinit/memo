#pragma once

#include <memory>

#include <boost/signals2.hpp>

#include <elle/Duration.hh>

#include <memo/model/blocks/Block.hh>
#include <memo/model/doughnut/fwd.hh>
#include <memo/model/Model.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      class Peer
        : public elle::Printable
        , public std::enable_shared_from_this<Peer>
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Peer(Doughnut& dht, Address id);
        virtual
        ~Peer();
        /// Stop all activity requiring other components (e.g. RPC server).
        void
        cleanup();
        /// Owner doughnut.
        ELLE_ATTRIBUTE_R(Doughnut&, doughnut, protected);
        /// Target peer id.
        ELLE_ATTRIBUTE_R(Address, id, protected);
        ELLE_ATTRIBUTE_RX(boost::signals2::signal<void()>, connected);
        ELLE_ATTRIBUTE_RX(boost::signals2::signal<void()>, disconnected);
      protected:
        virtual
        void
        _cleanup();

      /*-------.
      | Blocks |
      `-------*/
      public:
        virtual
        void
        store(blocks::Block const& block, StoreMode mode) = 0;
        std::unique_ptr<blocks::Block>
        fetch(Address address,
              boost::optional<int> local_version) const;
        virtual
        void
        remove(Address address, blocks::RemoveSignature rs) = 0;
      protected:
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const = 0;

      /*-----.
      | Keys |
      `-----*/
      public:
        elle::cryptography::rsa::PublicKey
        resolve_key(int);
        std::vector<elle::cryptography::rsa::PublicKey>
        resolve_keys(std::vector<int> const& ids);
        std::unordered_map<int, elle::cryptography::rsa::PublicKey>
        resolve_all_keys();
      protected:
        virtual
        std::vector<elle::cryptography::rsa::PublicKey>
        _resolve_keys(std::vector<int> const&) = 0;
        virtual
        std::unordered_map<int, elle::cryptography::rsa::PublicKey>
        _resolve_all_keys() = 0;

      /*----------.
      | Printable |
      `----------*/
      public:
        /// Print pretty representation to @a stream.
        void
        print(std::ostream& stream) const override;
      };
    }
  }
}
