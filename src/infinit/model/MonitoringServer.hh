#ifndef INFINIT_MODEL_MONITORING_SERVER_HH

# include <elle/json/json.hh>
# include <elle/Printable.hh>

# include <reactor/network/Server.hh>
# include <reactor/network/Socket.hh>

# include <infinit/model/doughnut/fwd.hh>
# include <infinit/serialization.hh>

namespace infinit
{
  namespace model
  {
    class MonitoringServer
    {
    public:
      struct MonitorQuery
        : public elle::Printable
      {
        enum class Query
          : std::int8_t
        {
          Consensus = 1,  // Consensus information.
          Overlay,        // Overlay information.
          Peers,          // Peer information.
          Redundancy,     // Data redundancy information.
          Status,         // Check if the network is running.
        };

        MonitorQuery(Query query);
        MonitorQuery(elle::serialization::SerializerIn& s);

        Query query;

        void
        serialize(elle::serialization::Serializer& s);
        void
        print(std::ostream& stream) const override;
      };

    public:
      struct MonitorResponse
        : public elle::serialization::VirtuallySerializable<false>
        , public elle::Printable
      {
        MonitorResponse(bool success,
                        boost::optional<std::string> error = {});
        MonitorResponse(elle::serialization::SerializerIn& s);

        bool success;
        boost::optional<std::string> error;

        typedef infinit::serialization_tag serialization_tag;
        static constexpr char const* virtually_serializable_key = "type";

        virtual
        void
        serialize(elle::serialization::Serializer& s) override;
        void
        print(std::ostream& stream) const override;
        virtual
        void
        pretty_print(std::ostream& stream) const;
      };

    public:
      struct MonitorResponseGeneric
        : public MonitorResponse
      {
        MonitorResponseGeneric(elle::json::Json result);
        MonitorResponseGeneric(elle::serialization::SerializerIn& s);

        elle::json::Json result;

        void
        serialize(elle::serialization::Serializer& s) override;
        void
        pretty_print(std::ostream& stream) const override;
      };

    public:
      struct MonitorResponsePeers
        : public MonitorResponse
      {
        /// {identifier, endpoint}
        typedef std::pair<std::string, std::string> Peer;
        typedef std::vector<Peer> Peers;

        MonitorResponsePeers(Peers peers);
        MonitorResponsePeers(elle::serialization::SerializerIn& s);

        Peers peers;

        void
        serialize(elle::serialization::Serializer& s) override;
        void
        pretty_print(std::ostream& stream) const override;
      };

    public:
      struct MonitorResponseRedundancy
        : public MonitorResponse
      {
        MonitorResponseRedundancy(std::string description);
        MonitorResponseRedundancy(elle::serialization::SerializerIn& s);

        std::string description;

        void
        serialize(elle::serialization::Serializer& s) override;
        void
        pretty_print(std::ostream& stream) const override;
      };

      /*-------------.
      | Construction |
      `-------------*/
    public:
      MonitoringServer(std::unique_ptr<reactor::network::Server> server,
                       doughnut::Doughnut& owner);
      ~MonitoringServer();

      ELLE_ATTRIBUTE(std::unique_ptr<reactor::network::Server>, server);
      ELLE_ATTRIBUTE(std::unique_ptr<reactor::Thread>, accepter);

    private:
      ELLE_ATTRIBUTE(doughnut::Doughnut&, owner);

      /*-------.
      | Server |
      `-------*/
    private:
      void
      _accept();
      void
      _serve(std::shared_ptr<reactor::network::Socket> socket);
    };
  }
}

#endif // INFINIT_MODEL_MONITORING_SERVER_HH
