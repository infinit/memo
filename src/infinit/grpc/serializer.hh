#pragma once

#include <elle/serialization/Serializer.hh>

namespace google
{
  namespace protobuf
  {
    class Message;
    class FieldDescriptor;
  }
}

namespace infinit
{
  namespace grpc
  {
    class SerializerIn
      : public elle::serialization::SerializerIn
    {
    public:
      SerializerIn(const google::protobuf::Message* src);
    protected:
      bool _text() const override { return false; }
      bool
      _enter(std::string const& name) override;
      void
      _leave(std::string const& name) override;
      void
      _serialize_array(int size, // -1 for in(), array size for out()
                       std::function<void ()> const& f) override;
      void _serialize(int64_t& v) override;
      void _serialize(uint64_t& v) override;
      void _serialize(int32_t& v) override;
      void _serialize(uint32_t& v) override;
      void _serialize(int16_t& v) override;
      void _serialize(uint16_t& v) override;
      void _serialize(int8_t& v) override;
      void _serialize(uint8_t& v) override;
      void _serialize(ulong& v) override;
      void
      _serialize(double& v) override;
      void
      _serialize(bool& v) override;
      void
      _serialize(std::string& v) override;
      void
      _serialize(elle::Buffer& v) override;
      void
      _serialize(boost::posix_time::ptime& v) override;
      void
      _serialize_time_duration(std::int64_t& ticks,
                               std::int64_t& num,
                               std::int64_t& denom) override;
      
      void
      _serialize_named_option(std::string const& name,
                              bool present,
                              std::function<void ()> const& f) override;
      
      void
      _serialize_option(bool present,
                        std::function<void ()> const& f) override;
      void
      _serialize_variant(std::vector<std::string> const& names,
                         int index, // out: filled, in: -1
                         std::function<void(int)> const& f) override;
      template<typename T>
      void _serialize_int(T& v);
    private:
      int _index;
      int _last_serialized_int;
      std::vector<const google::protobuf::Message*> _message_stack;
      const google::protobuf::FieldDescriptor* _field;
    };

    class SerializerOut
      : public elle::serialization::SerializerOut
    {
    public:
      SerializerOut(google::protobuf::Message* dst);
    protected:
      bool _text() const override { return false; }
      bool
      _enter(std::string const& name) override;
      void
      _leave(std::string const& name) override;
      void
      _serialize_array(int size, // -1 for in(), array size for out()
                       std::function<void ()> const& f) override;
      void _serialize(int64_t& v) override;
      void _serialize(uint64_t& v) override;
      void _serialize(int32_t& v) override;
      void _serialize(uint32_t& v) override;
      void _serialize(int16_t& v) override;
      void _serialize(uint16_t& v) override;
      void _serialize(int8_t& v) override;
      void _serialize(uint8_t& v) override;
      void _serialize(ulong& v) override;
      void
      _serialize(double& v) override;
      void
      _serialize(bool& v) override;
      void
      _serialize(std::string& v) override;
      void
      _serialize(elle::Buffer& v) override;
      void
      _serialize(boost::posix_time::ptime& v) override;
      void
      _serialize_time_duration(std::int64_t& ticks,
                               std::int64_t& num,
                               std::int64_t& denom) override;
      
      void
      _serialize_named_option(std::string const& name,
                              bool present,
                              std::function<void ()> const& f) override;
      
      void
      _serialize_option(bool present,
                        std::function<void ()> const& f) override;
      void
      _serialize_variant(std::vector<std::string> const& names,
                                     int index, // out: filled, in: -1
                                     std::function<void(int)> const& f) override;
      template<typename T>
      void
      _serialize_int(T& v);
      void
      _field_check();
    private:
      int _index;
      std::vector<google::protobuf::Message*> _message_stack;
      const google::protobuf::FieldDescriptor* _field;
      /// Name path we are currently handling arrays for.
      std::vector<std::string> _array_handler;
      int _last_serialized_int;
    };

    /// Map a C++ type name to a valid message name.
    ///
    /// We take the first type seen without the namespace, with
    /// special handling of smart pointers.
    std::string
    cxx_to_message_name(std::string name);
  }
}
