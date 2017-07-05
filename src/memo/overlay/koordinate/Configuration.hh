#pragma once

#include <memo/overlay/Overlay.hh>

namespace memo
{
  namespace overlay
  {
    namespace koordinate
    {
      /// Serializable configuration for Koordinate.
      struct Configuration
        : public overlay::Configuration
      {
      /*------.
      | Types |
      `------*/
      public:
        /// Ourself.
        using Self = Configuration;
        /// Parent class.
        using Super = overlay::Configuration;
        /// Underlying overlays configurations.
        using Backends = std::vector<std::unique_ptr<overlay::Configuration>>;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        /** Construct a configuration.
         *
         *  @arg   backends Underlying overlays.
         *  @throw elle::Error if \a backends is empty.
         */
        Configuration(Backends backends);
        /// Underlying overlays configurations.
        ELLE_ATTRIBUTE_R(Backends, backends);
      private:
        /// Check construction postconditions.
        void
        _validate() const;

      /*---------.
      | Clonable |
      `---------*/
      public:

        std::unique_ptr<overlay::Configuration>
        clone() const override;

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
        /** Construct Koordinate from this configuration.
         *
         *  @arg hosts Initial peer list.
         *  @arg local Local server, null if pure client.
         *  @arg dht   Owning Doughnut.
         *  @return The built Koordinate.
         */

        std::unique_ptr<memo::overlay::Overlay>
        make(std::shared_ptr<model::doughnut::Local> local,
             model::doughnut::Doughnut* doughnut) override;
      };
    }
  }
}
