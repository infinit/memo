#ifndef INFINIT_OVERLAY_KOUNCIL_CONFIGURATION_HH
# define INFINIT_OVERLAY_KOUNCIL_CONFIGURATION_HH

# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace overlay
  {
    namespace kouncil
    {
      /// Serializable configuration for Kouncil.
      struct Configuration
        : public overlay::Configuration
      {
      /*------.
      | Types |
      `------*/
      public:
        /// Ourself.
        typedef Configuration Self;
        /// Parent class.
        typedef overlay::Configuration Super;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        /// Construct a configuration.
        Configuration();
      private:
        /// Check construction postconditions.
        void
        _validate() const;

      /*---------.
      | Clonable |
      `---------*/
      public:
        ELLE_CLONABLE();

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        /** Deserialize a Configuration.
         *
         *  @arg input Source serializer.
         */
        Configuration(elle::serialization::SerializerIn& input);
        /** Serialize Configuration.
         *
         *  @arg s Source or target serializer.
         */
        void
        serialize(elle::serialization::Serializer& s) override;

      /*--------.
      | Factory |
      `--------*/
      public:
        /** Construct Kouncil from this configuration.
         *
         *  @arg hosts Initial peer list.
         *  @arg local Local server, null if pure client.
         *  @arg dht   Owning Doughnut.
         *  @return The built Kouncil.
         */
        virtual
        std::unique_ptr<infinit::overlay::Overlay>
        make(std::shared_ptr<model::doughnut::Local> local,
             model::doughnut::Doughnut* doughnut) override;

        ELLE_ATTRIBUTE_R(boost::optional<int>, eviction_delay)
      };
    }
  }
}

#endif
