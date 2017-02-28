#include <infinit/grpc/serializer.hh>

#include <elle/serialization/json/MissingKey.hh>
#include <grpc++/grpc++.h>
#include <google/protobuf/message.h>

namespace infinit
{
  namespace grpc
  {

    bool
    SerializerIn::_enter(std::string const& name)
    {
      ELLE_ASSERT(!_field);
      auto* cur = _message_stack.back();
      auto* ref = cur->GetReflection();
      auto* desc = cur->GetDescriptor();
      _field = desc->FindFieldByName(name);
      if (!_field)
        elle::err("field %s does not exist in %s", name, desc->name());
      if (!ref->HasField(*cur, _field))
        throw elle::serialization::MissingKey(name);
      if (!_field->is_repeated() && _field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE)
      {
        auto* child = &ref->GetMessage(*cur, _field);
        _message_stack.push_back(child);
        _field = nullptr;
      }
      return true;
    }

    void
    SerializerIn::_leave(std::string const& name)
    {
      if (_field != nullptr)
        _field = nullptr;
      else
        _message_stack.pop_back();
    }

    void
    SerializerIn::_serialize(std::string& v)
    {
      ELLE_ASSERT(_field);
      if (_field->type() != google::protobuf::FieldDescriptor::TYPE_STRING)
        elle::err<elle::serialization::Error>(
          "field %s is of type %s not string", _field->name(), _field->type());
      auto* cur = _message_stack.back();
      auto str = cur->GetReflection()->GetString(*cur, _field);
      v = std::move(str);
    }

    void
    SerializerIn::_serialize(elle::Buffer& b)
    {
      std::string s;
      _serialize(s);
      b = s;
    }

    template<typename T, typename E>
    T get_small_int(E v)
    {
      if (v > std::numeric_limits<T>::max())
        elle::err<elle::serialization::Error>("%s does not fit in %s bytes", v, sizeof(T));
      if (v < std::numeric_limits<T>::min())
        elle::err<elle::serialization::Error>("underflow: %s does not fit in %s bytes or unsigned", v, sizeof(T));
      return static_cast<T>(v);
    }
 
    template<typename T>
    void
    SerializerIn::_serialize_int(T& v)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      {
        // FIXME will this call pass for other int types
        int64_t bigval = cur->GetReflection()->GetInt64(*cur, _field);
        v = get_small_int<T>(bigval);
      }
    }

    void SerializerIn::_serialize(int8_t  & v) { _serialize_int(v);}
    void SerializerIn::_serialize(uint8_t & v) { _serialize_int(v);}
    void SerializerIn::_serialize(int16_t & v) { _serialize_int(v);}
    void SerializerIn::_serialize(uint16_t& v) { _serialize_int(v);}
    void SerializerIn::_serialize(int32_t & v) { _serialize_int(v);}
    void SerializerIn::_serialize(uint32_t& v) { _serialize_int(v);}
    void SerializerIn::_serialize(int64_t & v) { _serialize_int(v);}

    void
    SerializerIn::_serialize(uint64_t& v)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      uint64_t bigval = cur->GetReflection()->GetUInt64(*cur, _field);
      v = get_small_int<uint64_t>(bigval);
    }

    void
    SerializerIn::_serialize(double& v)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      v = cur->GetReflection()->GetDouble(*cur, _field);
    }

    void
    SerializerIn::_serialize(boost::posix_time::ptime& v)
    {
      std::string s;
      _serialize(s);
      v = boost::posix_time::from_iso_string(s);
    }

    void
    SerializerIn::_serialize(bool& b)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      b = cur->GetReflection()->GetBool(*cur, _field);
    }

    void
    SerializerIn::_serialize_time_duration(std::int64_t& ticks,
                                           std::int64_t& num,
                                           std::int64_t& denom)
    {
      elle::err("not implemented");
    }

    void
    SerializerIn::_serialize_named_option(std::string const& name,
                                          bool present,
                                          std::function<void ()> const& f)
    {
      ELLE_ASSERT(!_field);
      auto* cur = _message_stack.back();
      auto* ref = cur->GetReflection();
      auto* desc = cur->GetDescriptor();
      auto* field = desc->FindFieldByName(name);
      if (!field)
        return;
      if (!field->is_repeated() && !ref->HasField(*cur, field))
        return;
      f();
    }

    void
    SerializerIn::_serialize_option(bool present,
                                    std::function<void ()> const& f)
    {
      elle::err<elle::serialization::Error>("not implemented");
    }

    void
    SerializerIn::_serialize_array(int, std::function<void ()> const& f)
    {
      /*
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      auto* ref = cur->GetReflection();
      int count = ref->FieldSize(*cur, _field);
      */
      elle::err("not implemented");
    }


    bool
    SerializerOut::_enter(std::string const& name)
    {
      ELLE_ASSERT(!_field);
      auto* cur = _message_stack.back();
      auto* ref = cur->GetReflection();
      auto* desc = cur->GetDescriptor();
      _field = desc->FindFieldByName(name);
      if (_field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE)
      {
        auto* child = ref->MutableMessage(cur, _field);
        _message_stack.push_back(child);
        _field = nullptr;
      }
      return true;
    }

    void
    SerializerOut::_leave(std::string const& name)
    {
      if (_field != nullptr)
        _field = nullptr;
      else
        _message_stack.pop_back();
    }

    void
    SerializerOut::_serialize(std::string& v)
    {
      ELLE_ASSERT(_field);
      if (_field->type() != google::protobuf::FieldDescriptor::TYPE_STRING)
        elle::err<elle::serialization::Error>(
          "field %s is of type %s not string", _field->name(), _field->type());
      auto* cur = _message_stack.back();
      cur->GetReflection()->SetString(cur, _field, v);
    }

    void
    SerializerOut::_serialize(elle::Buffer& b)
    {
      std::string s = b.string();
      _serialize(s);
    }

    template<typename T>
    void
    SerializerOut::_serialize_int(T& v)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      cur->GetReflection()->SetInt64(cur, _field, v);
    }

    void SerializerOut::_serialize(int8_t  & v) { _serialize_int(v);}
    void SerializerOut::_serialize(uint8_t & v) { _serialize_int(v);}
    void SerializerOut::_serialize(int16_t & v) { _serialize_int(v);}
    void SerializerOut::_serialize(uint16_t& v) { _serialize_int(v);}
    void SerializerOut::_serialize(int32_t & v) { _serialize_int(v);}
    void SerializerOut::_serialize(uint32_t& v) { _serialize_int(v);}
    void SerializerOut::_serialize(int64_t & v) { _serialize_int(v);}

    void
    SerializerOut::_serialize(uint64_t& v)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      cur->GetReflection()->SetUInt64(cur, _field, v);
    }

    void
    SerializerOut::_serialize(double& v)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      cur->GetReflection()->SetDouble(cur, _field, v);
    }

    void
    SerializerOut::_serialize(boost::posix_time::ptime& v)
    {
      std::string s = boost::posix_time::to_iso_string(v);
      _serialize(s);
    }

    void
    SerializerOut::_serialize(bool& b)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      cur->GetReflection()->SetBool(cur, _field, b);
    }

    void
    SerializerOut::_serialize_time_duration(std::int64_t& ticks,
                                           std::int64_t& num,
                                           std::int64_t& denom)
    {
      elle::err("not implemented");
    }

    void
    SerializerOut::_serialize_named_option(std::string const& name,
                                          bool present,
                                          std::function<void ()> const& f)
    {
      ELLE_ASSERT(!_field);
      _enter(name);
      f();
    }

    void
    SerializerOut::_serialize_option(bool present,
                                    std::function<void ()> const& f)
    {
      elle::err<elle::serialization::Error>("not implemented");
    }

    void
    SerializerOut::_serialize_array(int, std::function<void ()> const& f)
    {
      /*
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      auto* ref = cur->GetReflection();
      int count = ref->FieldSize(*cur, _field);
      */
      elle::err("not implemented");
    }
  }
}